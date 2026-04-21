// ==========================================
// קובץ משני: Ultrasonics.ino
// תפקיד: דרייבר חומרה, פסיקות (ISRs) ותזמון (Scheduler)
// שימי לב: אין צורך ב-#include כי הארדואינו מחבר את הלשוניות יחד
// ==========================================

// משתנה סטטי לשמירת הזמן שבו נשלח הפולס האחרון. סטטי אומר שהערך נשמר בין קריאות לפונקציה.
static unsigned long lastTriggerTime = 0;

// משתנה שעוקב איזה חיישן אמור "לצעוק" עכשיו. מתחיל מ-0.
static uint8_t currentSensorToTrigger = 0;

// זמן המתנה בין חיישן לחיישן (40 מילי-שניות). נועד לתת לגל הקול לדעוך כדי למנוע הפרעה אקוסטית 
const unsigned long TIME_BETWEEN_TRIGGERS_MS = 40;

// ==========================================
// פסיקות (Interrupt Service Routines - ISRs)
// ==========================================
//(IRAM_ATTR) הערה: בבקר, פסיקות חייבות לשבת בזיכרון ה-רם המהיר כדי למנוע קריסת מערכת.
// אי אפשר להעביר ארגומנטים לפסיקה, לכן יצרנו 4 פונקציות ייעודיות וקצרות מאוד.

void IRAM_ATTR isr0() { 
    // אם הפין עלה לגבוה, רושמים את זמן ההתחלה
    if (digitalRead(sensors[0].echoPin)) sensors[0].startTime = micros(); 
    // אם הפין ירד לנמוך, מחשבים את משך הזמן הכולל ומרימים את הדגל (True)
    else { sensors[0].duration = micros() - sensors[0].startTime; sensors[0].newDataReady = true; } 
}

void IRAM_ATTR isr1() { 
    if (digitalRead(sensors[1].echoPin)) sensors[1].startTime = micros(); 
    else { sensors[1].duration = micros() - sensors[1].startTime; sensors[1].newDataReady = true; } 
}

void IRAM_ATTR isr2() { 
    if (digitalRead(sensors[2].echoPin)) sensors[2].startTime = micros(); 
    else { sensors[2].duration = micros() - sensors[2].startTime; sensors[2].newDataReady = true; } 
}

void IRAM_ATTR isr3() { 
    if (digitalRead(sensors[3].echoPin)) sensors[3].startTime = micros(); 
    else { sensors[3].duration = micros() - sensors[3].startTime; sensors[3].newDataReady = true; } 
}

// ==========================================
//           פונקציות הדרייבר
// ==========================================

// פונקציית אתחול - נקראת פעם אחת 
void initUltrasonic() {
    // לולאה שעוברת על כל המערך ומגדירה את הפינים
    for (int i = 0; i < NUM_SENSORS; i++) {
        pinMode(sensors[i].trigPin, OUTPUT);
        // שימוש בהאק החומרתי שלך - משיכה פנימית למעלה כדי לפצות על אות חלש מ-3.3V
        pinMode(sensors[i].echoPin, INPUT_PULLUP);
        // מוודאים שפין הטריגר במצב נמוך ומוכן לעבודה
        digitalWrite(sensors[i].trigPin, LOW);
    }
    
    // רישום הפסיקות: כל שינוי במצב הפין (עלייה או ירידה) יקפיץ את הפסיקה המתאימה
    attachInterrupt(digitalPinToInterrupt(sensors[0].echoPin), isr0, CHANGE);
    attachInterrupt(digitalPinToInterrupt(sensors[1].echoPin), isr1, CHANGE);
    attachInterrupt(digitalPinToInterrupt(sensors[2].echoPin), isr2, CHANGE);
    attachInterrupt(digitalPinToInterrupt(sensors[3].echoPin), isr3, CHANGE);
}

// המתזמן הראשי - פונקציה אסינכרונית לא חוסמת
void runUltrasonicScheduler() {
    unsigned long currentTime = millis(); // קריאת זמן המערכת הנוכחי
    
    // בודק אם עבר מספיק זמן (40 מ"ש) מאז הטריגר האחרון
    if (currentTime - lastTriggerTime >= TIME_BETWEEN_TRIGGERS_MS) {
        lastTriggerTime = currentTime; // עדכון זמן הטריגר הנוכחי
        
        //S= יצירת משתנה מקומי לנוחות קריאה, האינדקס של החיישן התורן)
        uint8_t s = currentSensorToTrigger;
        
        // שליחת הפולס 
        digitalWrite(sensors[s].trigPin, LOW);
        delayMicroseconds(2);
        digitalWrite(sensors[s].trigPin, HIGH);
        delayMicroseconds(10); // ה"צעקה" נמשכת 10 מיקרו-שניות
        digitalWrite(sensors[s].trigPin, LOW);
        
        // קידום התור: משתמשים בפעולת מודולו (%) כדי ליצור סבב: 0, 1, 2, 3 ושוב 0
        currentSensorToTrigger = (currentSensorToTrigger + 1) % NUM_SENSORS;
    }
}

// פונקציית בדיקה לדגל
bool isNewDistanceAvailable(uint8_t sensorIndex) { 
    return sensors[sensorIndex].newDataReady; 
}

// פונקציית חישוב למרחק
float getLatestDistance(uint8_t sensorIndex) {
    // הורדת הדגל מיד עם הקריאה כדי שלא נקרא את אותו נתון פעמיים
    sensors[sensorIndex].newDataReady = false; 
    
    // חישוב פיזיקלי: מהירות הקול היא 0.0343 ס"מ למיקרו-שנייה. חלוקה ב-2 כי הגל עשה הלוך-חזור
    float distance = (sensors[sensorIndex].duration * 0.0343) / 2.0;
    
    // מנגנון הגנה: מרחק מעל 400 ס"מ או פחות מ-2 ס"מ הוא טעות או פסק זמן 
    if (distance > 400 || distance < 2) return -1.0; 
    
    return distance; // החזרת המרחק התקין
}


// static const int TRIG_PIN = 5;
// static const int ECHO_PIN = 17;

// static volatile unsigned long startTime = 0;
// static volatile unsigned long duration = 0;
// static volatile bool newDataReady = false;

// // פונקציית פסיקה (ISR) שרצה ישירות מה-RAM לתגובה מיידית
// void IRAM_ATTR echoISR() {
//     if (digitalRead(ECHO_PIN) == HIGH) {
//         startTime = micros(); 
//     } else {
//         duration = micros() - startTime; 
//         newDataReady = true; 
//     }
// }

// void initUltrasonic() {
//     pinMode(TRIG_PIN, OUTPUT);
    
//     // *** התיקון הקריטי ***
//     // שימוש בנגד המשיכה הפנימי כדי להתגבר על חולשת האות של החיישן
//     pinMode(ECHO_PIN, INPUT_PULLUP); 
    
//     digitalWrite(TRIG_PIN, LOW); 
    
//     // חיבור הפסיקה לפין 33
//     attachInterrupt(digitalPinToInterrupt(ECHO_PIN), echoISR, CHANGE);
// }

// void triggerUltrasonic() {
//     digitalWrite(TRIG_PIN, LOW);
//     delayMicroseconds(2);
//     digitalWrite(TRIG_PIN, HIGH);
//     delayMicroseconds(10); 
//     digitalWrite(TRIG_PIN, LOW);
// }

// bool isNewDistanceAvailable() {
//     return newDataReady;
// }

// float getLatestDistance() {
//     newDataReady = false; 
    
//     float distance = (duration * 0.0343) / 2.0;
    
//     if (distance > 400 || distance < 2) return -1.0; 
    
//     return distance;
// }