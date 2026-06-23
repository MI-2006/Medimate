#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "TFT9341Touch.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <Adafruit_Fingerprint.h>

enum IntakeStatus {
    STATUS_ON_TIME = 0,
    STATUS_WARNING = 1,
    STATUS_SEVERE  = 2
};
//משתנים שמחזיקים את תהליך האימות מול פייר בייס
String projectId = "medimate-d248e";
String apiKey = "AIzaSyB6bYkZgrUFK7Ed8hyyYzqUI7pHFqu9wDc";
String USER_EMAIL = "esp32@test.com";
String USER_PASSWORD = "123456";
// משתנה חדש שיחזיק את תעודת הזהות הזמנית שלנו מול גוגל
String idToken = "";

//חיבור לאינטרנט
 const char* ssid ="Kita-5";//"Hots"; //
const char* password ="Xnhbrrfxho";// "0548105650";//
//משתנה שיראה אם אנחנו מחוברים לאינטרנט
unsigned long lastSuccessfulSync = 0; // זמן הסנכרון האחרון במילי-שניות

//טיפול בחיבור לאפליקציה
//הגדרת סוגי הפקודות האפשריות שיכולות להגיע מהאפליקציה
enum CommandType {
  CMD_SET_VOLUME,//טיפול בעוצמת הווליום
  CMD_SET_VACATION,//הדלקה וכיבוי מצב חופשה
  CMD_DISPENSE_PILL//לשחרר תא תרופה כל שהו
};

// 2. המבנה החכם והחסכוני בזיכרון שיעבור בתור ההודעות
struct AppMessage {
  CommandType cmd;        // איזה סוג פקודה קיבלנו
  
  // כאן מגיע קסם של מהנדסים - union. 
  // כל המשתנים בפנים תופסים את אותו מקום בדיוק בזיכרון, 
  // כי פקודה יכולה להיות רק דבר אחד בכל רגע נתון.
struct {
     uint8_t volume;       // ערך בין 0 ל-100 (עבור CMD_SET_VOLUME)
    bool isVacation;      // מצב חופשה כן/לא (עבור CMD_SET_VACATION)
    uint8_t pillCompartment; // מזהה התא ממנו יש לשחרר כדור (עבור CMD_DISPENSE_PILL)
  } payload;              // זהו "המטען" שהפקודה נושאת
};

// 3. הגדרת המצביע לתור של מערכת ההפעלה (ייווצר ב-setup)
QueueHandle_t appMessageQueue;

  //שם משתמש שבהמשך נקבל בצורה דינאמית
  String currentUserId = "6QJ1ik2hinRlqBfTBt1A"; 
  //משתנה שיחזיק את שם הקופסא
  String linkedBoxId = "";

  //הגדרות עבור חיישן טביעת האצבע
  // Serial2 הגדרת ערוץ  (RX=16, TX=17)
  HardwareSerial mySerial(2);
  // יצירת אובייקט החיישן
  Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
  
  //הכרזה על פונקציות המסך
  void setupDisplay();
  void updateDisplay(int volume, bool isAway);
  // הצהרה על פונקציות הפיירבייס
  void authenticateUser();
  void updateBoxStatus(String id, int currentVolume, bool isUserAway);
  String getUserLinkedBox(String uId);
  void getAndPrintBoxData(String id);
// הצהרה מוקדמת על פונקציות ניהול זמן ושרת 
bool setupTime(); 
void setupServer();

  //הצגת הווליום על המסך
  unsigned long lastUpdate = 0;
  const unsigned long updateInterval = 10000; // עדכון כל 10 שניות למניעת עומס על ה-API
  //הגדרה לבקר שיש משתנים כאן שאולי הוא לא מכיר אבל נשתמש בהם בהשך אז שירגע
  extern tft9341touch tft;
  extern int lastVolume;
  extern int lastAway;

// Hardware_Actuators.ino — public API forward declarations
void setupHardwareActuators();
void runStateMachine();
void triggerAlarm(uint8_t compartmentId);
void openCompartment(uint8_t compId);

void setup() {
  Serial.begin(115200);
  delay(1000);

  // 1. אתחול מסך
  Serial.println("Starting TFT...");
  setupDisplay();
  Serial.println("TFT Started.");

  // 2. חיבור ל-WiFi
  Serial.println("\nConnecting to WiFi...");
  WiFi.begin(ssid, password); 
  
  int timeout_counter = 0;
  while (WiFi.status() != WL_CONNECTED && timeout_counter < 20) {
    delay(1000);
    Serial.print(".");
    timeout_counter++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected!");
    authenticateUser();
    
    // קריאה לפונקציית הזמן המשוכתבת ובדיקת הצלחה
    if (setupTime()) {
       Serial.println("System clock is accurate.");
    } else {
       Serial.println("Warning: System clock is inaccurate. Alarms might be delayed.");
    }
  } else {
    Serial.println("\nFailed to connect.");
  }

  // 3. הגדרת קופסה ופיירבייס
  String linkedBox = getUserLinkedBox(currentUserId);
  Serial.println("User is linked to Box ID: " + linkedBox);
  
  if (linkedBox != "") {
    updateBoxStatus(linkedBox, 90, false);
  } else {
    Serial.println("Error: No linked box found.");
  }
  getAndPrintBoxData(linkedBox);

  // 4. אתחול חיישן טביעת אצבע (על Serial2)
  mySerial.begin(57600, SERIAL_8N1, 16, 17);
  setupFingerprint(); // קריאה לפונקציית האתחול שגם מעדכנת את הדגל!

  // 5. אתחול חומרה חדשה (PIR, MP3, Servo Mock)
  setupHardwareActuators();
}

void loop() {
  // מכונת המצבים: רצה בכל מחזור, חוזרת מיד כשהמצב הוא IDLE
  runStateMachine();

  // 1. קריאת נתונים מהענן - רץ פעם ב-10 שניות
  if (millis() - lastUpdate >= updateInterval) {
    lastUpdate = millis();
    getAndPrintBoxData(linkedBoxId); 
  } 

  // 2. סימולציה של קבלת פקודה מהאפליקציה דרך הסריאל מוניטור
  if (Serial.available() > 0) {
    char incomingChar = Serial.read();
    
    // אם הקלדנו 'E' או 'e' נתחיל תהליך למידת אצבע
    if (incomingChar == 'E' || incomingChar == 'e') {
       Serial.println("\n--- App Command Received: ENROLL FINGERPRINT ---");
       
       // מבקשים מהחיישן לדעת כמה אצבעות כבר שמורות אצלו כדי למצוא ID פנוי
       finger.getTemplateCount();
       uint8_t nextId = finger.templateCount + 1; 
       
       // שינוי תצוגת המסך להדרכת המשתמש
       tft.fillScreen(BLACK);
       tft.setCursor(20, 100);
       tft.setTextColor(YELLOW);
       tft.setTextSize(2);
       tft.println("Place finger on sensor");
       
       // קריאה לפונקציית הלמידה שכתבנו
       bool success = enrollNewFingerprint(nextId);
       
       // חיווי סופי על המסך
       tft.fillScreen(BLACK);
       tft.setCursor(20, 100);
       if (success) {
           tft.setTextColor(GREEN);
           tft.println("Finger Added!");
       } else {
           tft.setTextColor(RED);
           tft.println("Failed!");
       }
       
       // השהיה קצרה לקריאת ההודעה
       delay(2000); 
       
       // טריק: מאפסים את הזיכרון כדי לאלץ את המערכת לצייר את המסך הראשי מחדש
       lastVolume = -1; 
    }

    // אם נקליד 'A' (Alarm), נזריק אזעקה מזויפת לתא מספר 1
    if (incomingChar == 'A' || incomingChar == 'a') {
        Serial.println("\n--- Debug Command: TRIGGERING FAKE ALARM ---");
        triggerAlarm(1); 
    }

  }

  //wakeUpServer(); 
}