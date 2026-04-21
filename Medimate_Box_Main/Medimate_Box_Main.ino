#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

enum IntakeStatus {
    STATUS_ON_TIME = 0,
    STATUS_WARNING = 1,
    STATUS_SEVERE  = 2
};

String projectId = "medimate-d248e";
String apiKey = "AIzaSyB6bYkZgrUFK7Ed8hyyYzqUI7pHFqu9wDc";
String USER_EMAIL = "esp32@test.com";
String USER_PASSWORD = "123456";
// משתנה חדש שיחזיק את תעודת הזהות הזמנית שלנו מול גוגל
String idToken = "";

//חיבור לאינטרנט
 const char* ssid ="Hots"; ;
const char* password = "0548105650";
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
  //הכרזה על פונקציות
  void setupDisplay();
  void updateDisplay(int volume, bool isAway);
void setup(){
  Serial.begin(115200);
  delay(1000);
  // הדלקת המסך
  Serial.println("Starting TFT...");
  setupDisplay();
  Serial.println("TFT Started.");
  //נסיון התחברות לאינטרנט
  Serial.println("\nConnecting to WiFi...");
  WiFi.begin(ssid, password);
  //קובעים לבקר חסם עליון לנסיון להתחברות
  //כדי שלא יתקע לנצח בחיפוש אחר חיבור לאינטרנט
  int timeout_counter = 0;
  while (WiFi.status() != WL_CONNECTED && timeout_counter < 20) {
    delay(1000);
    Serial.print(".");
    timeout_counter++;
  }
  //בדיקה למה יצאנו מהלולאה
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected!");
    //ברגע שיש אינטרנט, מתחברים עם אימייל וסיסמה 
    authenticateUser();
  } else {
    Serial.println("\nFailed to connect. Status code: ");
    //מחזיר את סוג השגיאה
    Serial.println(WiFi.status()); }
  // סטטוס 4 = WL_CONNECT_FAILED, סטטוס 6 = WL_DISCONNECTED



  //בדיקה איזה קופסה משויכת למשתמש הזה
  String linkedBox = getUserLinkedBox(currentUserId);
  Serial.println("User is linked to Box ID: " + linkedBox);
  //אם מצאנו קופסה, נעדכן אותה
  if (linkedBox != "") {
    Serial.println("--- Step B: Updating Box Status ---");
    // נעדכן שהווליום הוא 90 והמשתמש נמצא בבית (false)
    updateBoxStatus(linkedBox, 90, false); 
  } else {
    Serial.println("Error: No linked box found.");
  }
  // קריאה לפונקציה החדשה כדי להדפיס את מצב הקופסה
  getAndPrintBoxData(linkedBox);

}
//הצגת הווליום על המסך
unsigned long lastUpdate = 0;
const unsigned long updateInterval = 10000; // עדכון כל 10 שניות למניעת עומס על ה-API

void loop() {
  if (millis() - lastUpdate >= updateInterval) {
    lastUpdate = millis();
    // שליפת הנתון המעודכן מהענן ועדכון המסך
    getAndPrintBoxData(linkedBoxId); 
    //בדיקת נגיעה במסך (בכל ריצה של הלופ!)
  if (tft.getTouch()) { 
    int touchX = tft.getToucX();
    int touchY = tft.getToucY();

    // בדיקה: האם לחצו על כפתור ה"פלוס"? (לפי הקואורדינטות שציירנו)
    if (touchX > 280 && touchX < 320 && touchY > 150 && touchY < 180) {
       int newVol = constrain(lastVolume + 10, 0, 100);
       Serial.println("Volume Up Pressed!");
       updateBoxStatus(linkedBoxId, newVol, (bool)lastAway);
       delay(300); // Debounce - מניעת לחיצות כפולות
    }

    // בדיקה: האם לחצו על כפתור ה-Toggle?
    if (touchX > 20 && touchX < 300 && touchY > 200 && touchY < 230) {
       bool newAway = !lastAway;
       Serial.println("Toggle Status Pressed!");
       updateBoxStatus(linkedBoxId, lastVolume, newAway);
       delay(300); // Debounce
    }
  }
}
}