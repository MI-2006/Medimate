// ==========================================
// קובץ ראשי: Medimate_Robot_main.ino
// תפקיד: ניהול משאבים, לולאה ראשית ומכונת מצבים
// ==========================================

// שימוש בספריית הליבה של ארדואינו 

#define NUM_SENSORS 4

// הגדרת מבנה נתונים. אנו מאגדים את כל התכונות של חיישן בודד לאובייקט אחד.
struct SensorData {
    uint8_t trigPin;                  // פין שליחת האות (8 ביט ללא סימן, חוסך מקום)
    uint8_t echoPin;                  // פין קבלת האות
    volatile unsigned long startTime; // זמן תחילת הפולס. מזהיר את הקומפיילר שהערך יכול להשתנות מתוך פסיקה
    volatile unsigned long duration;  // משך זמן חזרת ההד
    volatile bool newDataReady;       // דגל שמודיע ללולאה הראשית שהנתון מוכן לקריאה
};

// יצירת מערך גלובלי של החיישנים שלנו.
SensorData sensors[NUM_SENSORS] = {
    {25, 26, 0, 0, false}, // חיישן 0: Trig=25, Echo=26
    {27, 14, 0, 0, false}, // חיישן 2: Trig=27, Echo=14
    {32, 33, 0, 0, false}, // חיישן 2: Trig=32, Echo=33 
    {5,  18, 0, 0, false}  // חיישן 3: Trig=5, Echo=18 
};
// נתונים למניעת התנגשות בין הדלקת הרובוט לבקשה מהשרת
unsigned long robotStartTime; 
bool isServerReady = false;
const unsigned long SERVER_BOOT_DELAY = 30000; // 30 שניות המתנה לשרת

unsigned long lastCameraTime = 0;           // שומר את הזמן של הצילום האחרון
const unsigned long CAMERA_INTERVAL = 5000; // כל כמה זמן לצלם (במילי-שניות)

void setup() {
    // פתיחת ערוץ תקשורת טורית במהירות גבוהה כדי שההדפסות לא יעכבו את המעבד
    Serial.begin(115200);

    //הגדרת התקשורת של המצלמה
    Serial2.begin(115200, SERIAL_8N1, 16, 21); 
    
    // קריאה לפונקציית האתחול שנמצאת בלשונית השנייה 
    initUltrasonic();
    
    //RAM שומר את המחרוזת בזיכרון ה-פלאש (קבוע) במקום ב
    Serial.println(F("System Status: MULTI_ULTRASONIC_INITIALIZED (TABS BUILD)+ Camera Link"));

    //הפעלת נורות הלד-העין 
    setupEye();

    // שמירת זמן ההדלקה
    robotStartTime = millis();
}

void loop() {
    // קריאה למתזמן החיישנים בכל מחזור. הפונקציה הזו לא חוסמת את המעבד.
    runUltrasonicScheduler();

    // בדיקה: האם עבר מספיק זמן והאם המצלמה צריכה לפעול?
    if (!isServerReady) {
        if (millis() - robotStartTime > SERVER_BOOT_DELAY) {
            isServerReady = true;
            Serial.println("Server should be awake. Enabling camera triggers.");
        }
    }
    // הפעלת המצלמה רק אם השרת מוכן
    if (isServerReady) {
        static unsigned long lastCaptureTime = 0;
        if (millis() - lastCaptureTime > 10000) { // צילום כל 10 שניות
            lastCaptureTime = millis();
            Serial2.print('C'); 
        }
    }
    
    // קבלת תשובות מהמצלמה 
    checkCameraResponse();
}

    // לוגיקת המצלמה: קבלת התשובה מהענן (אם הגיעה)
    if (Serial2.available() > 0) {      // אם יש נתונים שמחכים בערוץ של המצלמה
        String cloudResponse = Serial2.readStringUntil('\n'); // קריאת התשובה עד סוף השורה
        processDecision(cloudResponse); // העברת התשובה לפונקציית קבלת ההחלטות
    }
    // לולאת סריקה: עוברת על כל 4 החיישנים לבדוק אם מישהו מהם סיים למדוד
    for (uint8_t i = 0; i < NUM_SENSORS; i++) {
        
        // בדיקה האם הדגל של החיישן הספציפי עלה (הפסיקה הסתיימה)
        if (isNewDistanceAvailable(i)) {
            
            // משיכת המרחק המחושב. פעולה זו גם מורידה את הדגל אוטומטית
            float dist = getLatestDistance(i);
            
            Serial.print(F("Sensor [")); 
            Serial.print(i); 
            Serial.print(F("]: "));
            
            // לוגיקת החלטה: אם קיבלנו שגיאה (-1) או מרחק גדול מ-40 ס"מ
            if (dist == -1.0 || dist > 40.0) {
                Serial.println(F("CLEAR")); 
            } else {
                // אם המכשול קרוב מ-40 ס"מ, מדפיסים אזהרה
                Serial.print(F("OBSTACLE AT ")); 
                Serial.print(dist); 
                Serial.println(F(" cm!"));

            }
        }
    }
    
    delay(500);
}
//כאשר מנועים מקבלים פקודה לזוז, קרא ל- updateEyeState(true);
// כאשר המנועים עוצרים, קרא ל- updateEyeState(false);

