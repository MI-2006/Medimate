#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "TFT9341Touch.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <Adafruit_Fingerprint.h>

// ספריות BLE — נטענות כאן אבל BLEDevice::init() נקרא רק לאחר כיבוי WiFi
// הסיבה: טעינת ה-BLE stack שומרת ~80KB DRAM למאגרי HCI.
// ספריית WiFi-LwIP דורשת ~90KB DRAM. השתיים יחד גובלות בגבול ה-RAM
// לכן: לעולם אל תפעיל WiFi ו-BLE במקביל — ראה shutdownWiFiForBLE()
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// מכונת מצבים — רשימת המצבים האפשריים של הקופסא
enum BoxState : uint8_t {
    STATE_IDLE,                  // ממתין; סקירת Firebase תקופתית
    STATE_ALARM_RINGING,         // זמן מנה: התראה קולית+חזותית, ממתין לחיישן התנועה
    STATE_WAITING_FINGERPRINT,   // הופעל: חלון 15 שניות לאימות אצבע
    STATE_ROBOT_DEPLOYED,        // הקופסא ממשיכה לבדוק אצבע
    STATE_CALLING_TWILIO,        // POST ל-/api/notifications/call
    STATE_BLE_SEARCHING,         // חיבור לאינטרנט כבוי; סריקת בלוטוס לטלפון המשתמש
    STATE_BLE_TIMEOUT_RECOVERY,  // פג זמן הבלוטוס — הפעלת חיבור לאינטרנט מחדש + עדכון ענן
    STATE_DISPENSING,            // אימות הצליח: פתיחת סרוו + אישור חיישן מרחק 
    STATE_ERROR                  // תקלה — ניסיון מחדש כל 30 שניות
};

enum DispenseResult : uint8_t {
    DISPENSE_PENDING,
    DISPENSE_SENDING,
    DISPENSE_AWAITING_SLAVE,
    DISPENSE_SUCCESS,
    DISPENSE_PILL_STUCK,    
    DISPENSE_FAILED
};

enum IntakeStatus {
    STATUS_ON_TIME = 0,
    STATUS_WARNING = 1,
    STATUS_SEVERE  = 2
};

// משתנים שמחזיקים את תהליך האימות מול פייר בייס
String projectId    = "medimate-d248e";
String apiKey       = "AIzaSyB6bYkZgrUFK7Ed8hyyYzqUI7pHFqu9wDc";
String USER_EMAIL   = "esp32@test.com";
String USER_PASSWORD = "123456";
// משתנה  שיחזיק את תעודת הזהות הזמנית שלנו מול גוגל
String idToken      = "";
// חותמת זמן לניהול תוקף הטוקן (תקף שעה אחת, מרענן 5 דקות לפני פקיעה)
unsigned long tokenObtainedAt  = 0;
const unsigned long TOKEN_EXPIRY_MS = 3300000UL; // 55 דקות במילישניות

// חיבור לאינטרנט
const char* ssid     = "Hots";        // "Kita-5";
const char* password = "0548105650";  // "Xnhbrrfxho";
// משתנה שיראה אם אנחנו מחוברים לאינטרנט
unsigned long lastSuccessfulSync = 0; // זמן הסנכרון האחרון במילי-שניות

// טיפול בחיבור לאפליקציה
// הגדרת סוגי הפקודות האפשריות שיכולות להגיע מהאפליקציה
enum CommandType {
    CMD_SET_VOLUME,     // טיפול בעוצמת הווליום
    CMD_SET_VACATION,   // הדלקה וכיבוי מצב חופשה
    CMD_DISPENSE_PILL   // לשחרר תא תרופה כל שהו
};

// המבנה בזיכרון שיעבור בתור ההודעות
struct AppMessage {
    CommandType cmd;    // איזה סוג פקודה קיבלנו
    // כל המשתנים בפנים תופסים את אותו מקום בדיוק בזיכרון,
    // כי פקודה יכולה להיות רק דבר אחד בכל רגע נתון.
    struct {
        uint8_t volume;          // ערך בין 0 ל-100 (עבור CMD_SET_VOLUME)
        bool isVacation;         // מצב חופשה כן/לא (עבור CMD_SET_VACATION)
        uint8_t pillCompartment; // מזהה התא ממנו יש לשחרר כדור (עבור CMD_DISPENSE_PILL)
    } payload;                 
};

// הגדרת המצביע לתור של מערכת ההפעלה (ייווצר ב-setup)
QueueHandle_t appMessageQueue;

// שם משתמש שבהמשך נקבל בצורה דינאמית
String currentUserId = "6QJ1ik2hinRlqBfTBt1A";
// משתנה שיחזיק את שם הקופסא
String linkedBoxId   = "";

// ─────────────────────────────────────────────────────────────────────────────
// הגדרות עבור חיישן טביעת האצבע
// Serial2 הגדרת ערוץ (RX=16, TX=17)
// ─────────────────────────────────────────────────────────────────────────────
HardwareSerial mySerial(2);
// יצירת אובייקט החיישן
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

// ─────────────────────────────────────────────────────────────────────────────
// הגדרות ומשתנים למכונת המצבים החדשה
// ─────────────────────────────────────────────────────────────────────────────
static BoxState  g_state = STATE_IDLE;
static uint32_t g_stateEnteredAt = 0UL; // millis() של כניסה למצב נוכחי
static uint8_t g_pendingCompartment = 1U;  // מספר תא התרופה הממתין לשחרור
static bool g_twilioCallMade  = false; // מונע שיחת התראה כפולה למשתמש
static bool g_bleUserFound = false; // מוגדר על-ידי callback של בלוטוס
static bool g_pirTriggered = false; // מוגדר על-ידי ISR, מנוקה ב-ללאה
static bool g_compartmentOpen = false; // מעקב אחר מצב פתיחת התא
// ─────────────────────────────────────────────────────────────────────────────
// משתנה חדש: דגל אישור מרחוק מהאפליקציה
// נכתב על-ידי handler של ה-WebServer (thread של ESPAsyncWebServer על core 0)
// ונקרא על-ידי loop() (core 1) — לכן volatile הכרחי.
// volatile מונע מה-compiler לשמור את הערך בregister ולא לקרוא מהזיכרון.
// ─────────────────────────────────────────────────────────────────────────────
static volatile bool g_remoteApproved = false;

// הגדרות כתובות ומפתחות
// כתובת שרת Flask
static const char* SERVER_NOTIFY_URL  = "https://medimate.onrender.com/api/notifications/call";
// מפתח סודי משותף עם השרת
static const char* ROBOT_API_KEY      = "YOUR_SECRET_KEY_HERE";
// מספר טלפון לחירום (פורמט E.164 הנדרש על-ידי Twilio)
static const char* EMERGENCY_PHONE   = "+972501234567";
// כתובת MAC של טלפון המשתמש — מוגדרת בעת ההתקנה הראשונית
static const char* USER_PHONE_BLE_MAC = "aa:bb:cc:dd:ee:ff";

// ─────────────────────────────────────────────────────────────────────────────
// קבועי זמן — כולם uint32_t כדי להתאים ל-millis() ולמנוע גלישה
// ─────────────────────────────────────────────────────────────────────────────
// 5 דקות ממתנים לאצבע לפני הזנקת הרובוט
static const uint32_t FINGERPRINT_TIMEOUT_MS = 300000UL;
// 2 דקות לסריקת BLE — אחריהן מתחיל שחזור WiFi (תיקון "נקודת אל-חזור")
static const uint32_t BLE_SCAN_TIMEOUT_MS    = 120000UL;
// 5 דקות הסלמה לשיחת Twilio אחרי שהרובוט פרוס
static const uint32_t ROBOT_ESCALATION_MS    = 300000UL;
// 15 שניות לחלון אימות אצבע
static const uint32_t FP_VERIFY_WINDOW_MS    =  15000UL;
// עדכון Firebase כל 10 שניות
static const uint32_t FIREBASE_POLL_MS       =  10000UL;
// מגבלת זמן HTTP
static const uint32_t HTTP_TIMEOUT_MS        =   8000UL;
// זמן התייצבות לאחר כיבוי WiFi לפני אתחול BLE
static const uint32_t WIFI_DEINIT_SETTLE_MS  =    200UL;
// זמן התייצבות לאחר כיבוי BLE לפני הפעלת WiFi
static const uint32_t BLE_DEINIT_SETTLE_MS   =    200UL;
// מגבלת זמן חיבור WiFi
static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000UL;

// ─────────────────────────────────────────────────────────────────────────────
// הכרזה על פונקציות המסך
// ─────────────────────────────────────────────────────────────────────────────
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

// הגדרה לבקר שיש משתנים כאן שאולי הוא לא מכיר אבל נשתמש בהם בהמשך
extern tft9341touch tft;
extern int lastVolume;
extern int lastAway;

// הצגת הווליום על המסך
unsigned long lastUpdate   = 0;
const unsigned long updateInterval = 10000; // עדכון כל 10 שניות למניעת עומס על ה-API

// Hardware_Actuators.ino — הצהרות ציבוריות
// Hardware_Actuators.ino — הצהרות ציבוריות
void setupHardwareActuators();
void runStateMachine();
void triggerAlarm(uint8_t compartmentId);
void openCompartment(uint8_t compId);
void pollInventoryBitmask();
DispenseResult pollCompartmentDispense(); 
void setupFingerprint();                  

// ─────────────────────────────────────────────────────────────────────────────
// ISR לחיישן PIR — IRAM_ATTR
// ה-ISR מגדיר רק דגל bool — כתיבת bool לכתובת מיושרת היא אטומית ב-Xtensa LX6.
// שינוי g_state ישירות מ-ISR מסוכן: enum יכול להיות קריאה קרועה (torn read)
// ─────────────────────────────────────────────────────────────────────────────
void IRAM_ATTR pirISR() {
    g_pirTriggered = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Callback של סריקת BLE — רץ על פקידת BLE ב-core 0 (לא ISR)
//
// מדוע אי-אפשר להפעיל WiFi HTTP ו-BLE סריקה אגרסיבית בו-זמנית:
//   ה-ESP32 מחזיק ממשק RF אחד בלבד המשותף ל-WiFi וגם ל-BLE,
//   בנוהל arbitration תוכנתי של Espressif.
//   במצב WiFi-priority (ברירת מחדל):
//     • WiFi שולט ב-RF עד 8ms לכל TXOP burst
//     • BLE מקבל רק ~5% זמן אוויר אפקטיבי
//     • 2% מסוגלות הסריקה → פגיעה בכתובות MAC
//   לכן: BLE רץ רק אחרי WiFi.disconnect(true) — ראה shutdownWiFiForBLE()
// ─────────────────────────────────────────────────────────────────────────────
class MediMateBLECallback : public BLEAdvertisedDeviceCallbacks {
public:
    void onResult(BLEAdvertisedDevice dev) override {
        String addr = dev.getAddress().toString();
        if (addr == String(USER_PHONE_BLE_MAC)) {
            Serial.printf("[BLE] טלפון המשתמש נמצא — RSSI=%d כתובת=%s\n",
                          dev.getRSSI(), addr.c_str());
            g_bleUserFound = true;
            // עצור סריקה מיידית: מפחית צריכת זרם מ-120mA ל-80mA
            BLEDevice::getScan()->stop();
        }
    }
};
static MediMateBLECallback g_bleCallback;

// ─────────────────────────────────────────────────────────────────────────────
// עזר: כניסה למצב חדש במכונת המצבים
// ─────────────────────────────────────────────────────────────────────────────
static void enterState(BoxState s) {
    g_state          = s;
    g_stateEnteredAt = millis();
    
    String stateName = "UNKNOWN";
    switch(s) {
        case STATE_IDLE: stateName = "IDLE (ממתין)"; break;
        case STATE_ALARM_RINGING: stateName = "ALARM_RINGING (אזעקה פועלת)"; break;
        case STATE_WAITING_FINGERPRINT:
            stateName = "WAITING_FINGERPRINT (ממתין לאצבע)";
            // עדכון Firebase שהמכשיר עכשיו ממתין לאצבע —
            // האפליקציה מאזינה לשדה זה ומציגה את כרזת האישור המרחוק
            postEmergencyStatus("WAITING_FINGERPRINT");
            break;
        case STATE_ROBOT_DEPLOYED: stateName = "ROBOT_DEPLOYED (רובוט נשלח)"; break;
        case STATE_CALLING_TWILIO: stateName = "CALLING_TWILIO (שיחת חירום)"; break;
        case STATE_BLE_SEARCHING: stateName = "BLE_SEARCHING (סריקת בלוטות')"; break;
        case STATE_BLE_TIMEOUT_RECOVERY: stateName = "BLE_TIMEOUT_RECOVERY (שחזור תקשורת)"; break;
        case STATE_DISPENSING: stateName = "DISPENSING (משחרר תרופה)"; break;
        case STATE_ERROR: stateName = "ERROR (שגיאה)"; break;
    }
    
    Serial.printf("[STATE] -> %s  (t=%lu)\n", stateName.c_str(), g_stateEnteredAt);
}

// ─────────────────────────────────────────────────────────────────────────────
// עזר: חיבור ל-WiFi — חוסם עד WIFI_CONNECT_TIMEOUT_MS
// ─────────────────────────────────────────────────────────────────────────────
static bool connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return true;
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    uint32_t deadline = millis() + WIFI_CONNECT_TIMEOUT_MS;
    while (WiFi.status() != WL_CONNECTED && millis() < deadline)
        vTaskDelay(pdMS_TO_TICKS(100));
    bool ok = (WiFi.status() == WL_CONNECTED);
    Serial.printf("[WiFi] %s\n", ok ? WiFi.localIP().toString().c_str() : "נכשל");
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// עזר: כיבוי WiFi לפני הפעלת BLE
//
// WiFi.disconnect(true):
//   הפרמטר true קורא ל-esp_wifi_stop() + esp_wifi_deinit(), שמבצע:
//   1. שליחת DEAUTH ל-AP (ניתוק מסודר)
//   2. כיבוי LwIP: משחרר ~90KB DRAM שמאגרי TCP-IP תפסו —
//      בדיוק הזיכרון שמאגרי HCI של BLE צריכים
//   3. שחרור mutex של RF coexistence ממצב WiFi-priority
//   לאחר כיבוי זה, BLE מקבל 100% זמן RF → זמן זיהוי ~200ms (במקום שניות)
//
// WiFi.mode(WIFI_OFF):
//   מונע חיבור מחדש אוטומטי של ספריית Arduino WiFi
//   שהיה תופס מחדש את ה-mutex ורעיב את BLE בחזרה
// ─────────────────────────────────────────────────────────────────────────────
static void shutdownWiFiForBLE() {
    Serial.println("[WiFi] ניתוק + deinit — שחרור RF עבור BLE");
    WiFi.disconnect(true);  // DEAUTH + esp_wifi_stop() + esp_wifi_deinit()
    WiFi.mode(WIFI_OFF);    // מניעת חיבור מחדש אוטומטי
    // 200ms יישוב: מאפשר לדרייבר ה-WiFi לסיים ניקוי ב-core 0
    vTaskDelay(pdMS_TO_TICKS(WIFI_DEINIT_SETTLE_MS));
    Serial.println("[WiFi] RF שוחרר — BLE מקבל 100% גישה ל-2.4GHz");
}

// ─────────────────────────────────────────────────────────────────────────────
// עזר: כיבוי BLE ושחרור RAM לשימוש WiFi
//
// BLEDevice::deinit(true):
//   משחרר ~80KB DRAM (מאגרי HCI TX/RX, תורי ACL, מצב GAP).
//   ספריית mbedTLS (TLS handshake של HTTPS) צריכה ~36KB DRAM רצוף;
//   ללא deinit(), malloc() של mbedTLS נכשל וכל קריאת HTTPS מחזירה -1
// ─────────────────────────────────────────────────────────────────────────────
static void shutdownBLEForWiFi() {
    Serial.println("[BLE] עצירת סריקה + deinit — שחרור RAM עבור WiFi");
    BLEDevice::getScan()->stop();
    BLEDevice::deinit(true); // שחרור מאגרי HCI + קוד דרייבר BLE
    vTaskDelay(pdMS_TO_TICKS(BLE_DEINIT_SETTLE_MS));
    Serial.println("[BLE] כיבוי BLE הושלם");
}

// ─────────────────────────────────────────────────────────────────────────────
// עזר: הפעלת סריקת BLE לאיתור טלפון המשתמש
// חייב להיקרא רק לאחר shutdownWiFiForBLE()
// ─────────────────────────────────────────────────────────────────────────────
static void startBLEScan() {
    BLEDevice::init("MediMate_Box"); // שם מכשיר BLE
    BLEScan* pScan = BLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(&g_bleCallback);
    // 160 × 0.625ms = 100ms מרווח סריקה
    pScan->setInterval(160);
    // 80 × 0.625ms = 50ms חלון = 50% duty cycle
    // 100% duty (window==interval): זיהוי מהיר יותר, 120mA
    // 50% duty: ~90mA, השהיית זיהוי מקסימלית 200ms — מספיק לאיתור אדם
    pScan->setWindow(80);
    pScan->setActiveScan(true); // שליחת SCAN_REQ לקבלת שם המכשיר
    // start(0, false): סריקה רציפה לא-חוסמת; אנחנו מנהלים את הטיים-אאוט
    // דרך millis() ב-loop() במקום לסמוך על הטיימר המובנה של ה-SDK
    pScan->start(0, false);
    Serial.println("[BLE] סריקה הופעלה (100% RF בלעדי, 50% duty cycle)");
}

// ─────────────────────────────────────────────────────────────────────────────
// עזר: רענון Token של Firebase אם פג תוקפו
// ─────────────────────────────────────────────────────────────────────────────
static bool refreshFirebaseToken() {
    if (WiFi.status() != WL_CONNECTED) return false;
    String url = "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=" + apiKey;
    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(HTTP_TIMEOUT_MS);
    StaticJsonDocument<128> req;
    req["email"] = USER_EMAIL; req["password"] = USER_PASSWORD;
    req["returnSecureToken"] = true;
    char buf[160]; serializeJson(req, buf, sizeof(buf));
    int code = http.POST((uint8_t*)buf, strlen(buf));
    bool ok = false;
    if (code == HTTP_CODE_OK) {
        String payload = http.getString(); // קריאת כל התשובה למחרוזת בטוחה
        DynamicJsonDocument resp(4096);    // הקצאת זיכרון גמיש וגדול
        if (!deserializeJson(resp, payload)) {
            idToken = resp["idToken"].as<String>();
            tokenObtainedAt = millis();
            ok = (idToken.length() > 0);
        }
    }
    http.end();
    Serial.printf("[Firebase] Token %s (HTTP %d)\n", ok ? "OK" : "נכשל", code);
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// עזר: עדכון מסמך חירום ב-Firestore
//
// זוהי הפונקציה המרכזית שמתקנת את "נקודת אל-החזור" — הבאג שבו הקופסא
// הייתה "עיוורת ומנותקת מהענן" כשסריקת BLE פגה זמן.
//
// הפונקציה שולחת PATCH ל-Firestore:
//   { "status": "<statusValue>", "timestamp": <unix_epoch> }
// אפליקציית המטפלים מאזינה ל-onSnapshot() על המסמך הזה.
// כתיבה ב-Firestore מפעילה את ה-listener תוך ~200ms
// ומציגה התראת חירום על הטלפונים של כל בני המשפחה הרשומים
//
// שימוש ב-PATCH (לא POST): כותב רק את השדות המצוינים, שאר המסמך נשמר
// ─────────────────────────────────────────────────────────────────────────────
static bool postEmergencyStatus(const char* statusValue) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Firebase] לא ניתן לעדכן — WiFi לא מחובר");
        return false;
    }
    // רענון Token אם פג תוקפו (55 דקות מהקבלה האחרונה)
    if (idToken.length() == 0 || (millis() - tokenObtainedAt) > TOKEN_EXPIRY_MS) {
        if (!refreshFirebaseToken()) return false;
    }

    // בניית URL של Firestore REST עם updateMask לפי שדה
    // updateMask מגן על שדות אחרים במסמך (כמות תרופות, לוח זמנים) מפני מחיקה
    String url = "https://firestore.googleapis.com/v1/projects/" + projectId +
                 "/databases/(default)/documents/SystemStatus/emergency"
                 "?updateMask.fieldPaths=status&updateMask.fieldPaths=timestamp";

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type",  "application/json");
    http.addHeader("Authorization", "Bearer " + idToken);
    http.setTimeout(HTTP_TIMEOUT_MS);

    // גוף ה-PATCH — Firestore REST דורש עטיפת כל ערך באובייקט טיפוס:
    // { "fields": { "status": { "stringValue": "..." } } }
    StaticJsonDocument<256> doc;
    JsonObject fields = doc.createNestedObject("fields");
    JsonObject statusF = fields.createNestedObject("status");
    statusF["stringValue"] = statusValue;
    JsonObject tsF = fields.createNestedObject("timestamp");
    tsF["integerValue"] = (long)time(nullptr); // epoch Unix — תקין אחרי NTP sync

    char bodyBuf[256];
    size_t bodyLen = serializeJson(doc, bodyBuf, sizeof(bodyBuf));

    // HTTPClient חסר מתודת .patch() — sendRequest() מקבל פועל שרירותי
    int code = http.sendRequest("PATCH", (uint8_t*)bodyBuf, (int)bodyLen);
    http.end();

    bool ok = (code == HTTP_CODE_OK || code == 200);
    Serial.printf("[Firebase] PATCH '%s' HTTP %d (%s)\n",
                  statusValue, code, ok ? "OK" : "נכשל");
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// עזר: שיחת Twilio דרך השרת
// ─────────────────────────────────────────────────────────────────────────────
static bool callTwilioViaServer() {
    if (!connectWiFi()) return false;
    StaticJsonDocument<96> doc;
    doc["to"] = EMERGENCY_PHONE;
    char buf[128]; size_t len = serializeJson(doc, buf, sizeof(buf));
    HTTPClient http;
    http.begin(SERVER_NOTIFY_URL);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Robot-Key",  ROBOT_API_KEY);
    http.setTimeout(HTTP_TIMEOUT_MS);
    int code = http.POST((uint8_t*)buf, (int)len);
    http.end();
    bool ok = (code == HTTP_CODE_OK);
    Serial.printf("[Twilio] POST HTTP %d (%s)\n", code, ok ? "OK" : "נכשל");
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// עזר: בדיקת טביעת אצבע — לא-חוסמת, ניסיון אחד לכל קריאה
//
// מחזירה ID תבנית (1-200) בהצלחה, -1 אם אין אצבע או לא הותאמה.
// ה-~90ms של חליפין UART הוא עיכוב חומרה מוגבל — לא המתנה אינסופית
// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// עזר: בדיקת טביעת אצבע — לא-חוסמת, ניסיון אחד לכל קריאה
// ─────────────────────────────────────────────────────────────────────────────
static int16_t checkFingerprint() {
    uint8_t p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) return -1; // מסלול מהיר — אין אצבע
    
    Serial.println("[FP] מזהה אצבע מונחת על החיישן...");

    if (p != FINGERPRINT_OK) {
        Serial.println("[FP] ❌ שגיאה: לא הצלחתי לצלם את האצבע (נסה לנקות את החיישן).");
        return -1;
    }
    
    p = finger.image2Tz();
    if (p != FINGERPRINT_OK) {
        Serial.println("[FP] ❌ שגיאה: איכות התמונה גרועה (אצבע לחה/רועדת מדי).");
        return -1;
    }
    
    p = finger.fingerFastSearch();
    if (p == FINGERPRINT_OK) {
        Serial.printf("[FP] ✅ הצלחה! זוהתה אצבע מורשית (ID=%d, ביטחון=%d)\n", finger.fingerID, finger.confidence);
        return (int16_t)finger.fingerID;
    } else if (p == FINGERPRINT_NOTFOUND) {
        Serial.println("[FP] ❌ סריקה תקינה, אבל האצבע לא נמצאת במאגר המורשים!");
        return -1;
    } else {
        Serial.println("[FP] ❌ שגיאת חומרה בחיפוש במאגר.");
        return -1;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// setup()
// ─────────────────────────────────────────────────────────────────────────────
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

        // רענון Token ראשוני — נדרש לפני כל קריאת Firestore
        refreshFirebaseToken();

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

    // 5. אתחול חומרה (PIR, MP3, I2C Master לבקר התאים)
    setupHardwareActuators();

    // 6. הגדרת פסיקת PIR — RISING edge = זיהוי תנועה
    // IO34 הוא פין קלט-בלבד ב-ESP32, אין pull-up פנימי
    attachInterrupt(digitalPinToInterrupt(34), pirISR, RISING);

    // מכונת המצבים מתחילה ב-IDLE
    enterState(STATE_IDLE);
}

// ─────────────────────────────────────────────────────────────────────────────
// loop() — מכונת המצבים הראשית
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
    const uint32_t now = millis();

    // קריאת דגל PIR בצורה אטומית — noInterrupts מונע torn read
    bool pirFired = false;
    noInterrupts();
    if (g_pirTriggered) { pirFired = true; g_pirTriggered = false; }
    interrupts();

    // רענון Token אוטומטי לפני פקיעה (בזמן שה-WiFi פעיל)
    if (idToken.length() > 0 &&
        (now - tokenObtainedAt) > TOKEN_EXPIRY_MS &&
        WiFi.status() == WL_CONNECTED) {
        refreshFirebaseToken();
    }

    // 1. מכונת המצבים: רצה בכל מחזור, חוזרת מיד כשהמצב הוא IDLE
    switch (g_state) {

    // ─── IDLE: ממתין, סקירת Firebase תקופתית ───────────────────────────
    case STATE_IDLE:
        // 3. קריאת נתונים מהענן - רץ פעם ב-10 שניות
        if (millis() - lastUpdate >= updateInterval) {
            lastUpdate = millis();
            getAndPrintBoxData(linkedBoxId);
        }
        break;

    // ─── ALARM_RINGING: זמן מנה — שמע + LED ──────────────────────────
    case STATE_ALARM_RINGING:
        // הבהוב מהיר (4Hz) ללא delay — חישוב מmodulo של millis()
        if (pirFired) {
            g_twilioCallMade = false;
            g_bleUserFound   = false;
            enterState(STATE_WAITING_FINGERPRINT);
        }
        break;

    // ─── WAITING_FINGERPRINT: חלון אצבע דינאמי + תמיכה באישור מרחוק ─────────
    דcase STATE_WAITING_FINGERPRINT:
        {
            // ── מסלול 1: אישור מרחוק מהאפליקציה ────────────────────────────────
            // בן המשפחה לחץ "אשר עבורי" באתר → הwebserver הציב g_remoteApproved=true
            // קריאת הדגל בצורה אטומית (volatile) לפני כל בדיקה אחרת
            if (g_remoteApproved) {
                g_remoteApproved = false; // איפוס מיידי — פעם אחת בלבד
                Serial.println("[REMOTE] ✅ אישור מרחוק מהאפליקציה — פותח תא ללא אצבע");
                // עדכון Firebase שאושר מרחוק (למעקב ויומן אירועים)
                postEmergencyStatus("REMOTE_APPROVED_BY_FAMILY");
                enterState(STATE_DISPENSING);
                break;
            }

            // ── מסלול 2: אימות טביעת אצבע רגיל ────────────────────────────────
            if (checkFingerprint() > 0) {
                Serial.println("[FP] ✅ אצבע מזוהה — פותח תא");
                enterState(STATE_DISPENSING);
                break;
            }

            // ── מסלול 3: PIR ראה תנועה — איפוס חלון הזמן ──────────────────────
            // כדי שהרובוט לא ייפרס בזמן שהמשתמש עדיין מנסה עם אצבע לחה/רועדת
            if (pirFired) {
                g_stateEnteredAt = now;
                Serial.println("[FP] PIR אופס — הרחבת חלון האימות");
            }

            // ── מסלול 4: פג זמן — פרוס רובוט ───────────────────────────────────
            if ((now - g_stateEnteredAt) >= FP_VERIFY_WINDOW_MS) {
                Serial.println("[FP] חלון פג — פורס רובוט");
                Serial1.print("DEPLOY\n"); // פקודה לבקר הרובוט
                postEmergencyStatus("ROBOT_DEPLOYED");
                enterState(STATE_ROBOT_DEPLOYED);
            }
            break;
        }

    // ─── ROBOT_DEPLOYED: ממשיך לבדוק אצבע בזמן שהרובוט מחפש ───────────
    case STATE_ROBOT_DEPLOYED:
    {
        if (checkFingerprint() > 0) {
            Serial1.print("RECALL\n");
            enterState(STATE_DISPENSING);
            break;
        }
        // הסלמה: 5 דקות ורובוט לא מצא → שיחת Twilio
        if ((now - g_stateEnteredAt) >= ROBOT_ESCALATION_MS && !g_twilioCallMade)
            enterState(STATE_CALLING_TWILIO);
        break;
    }

    // ─── CALLING_TWILIO: שיחת Twilio ואז מעבר לסריקת BLE ──────────────
    case STATE_CALLING_TWILIO:
    {
        // שלב א: שיחת Twilio (WiFi פעיל)
        const bool called = callTwilioViaServer();
        g_twilioCallMade = true;
        Serial.printf("[Twilio] %s\n", called ? "שיחה יצאה" : "נכשל");

        // עדכון Firebase בזמן ש-WiFi עדיין פעיל — לפני הכיבוי
        postEmergencyStatus("TWILIO_CALL_MADE");

        // שלב ב: כיבוי WiFi — שחרור RF עבור BLE
        // (ראה הסבר מפורט בפונקציה shutdownWiFiForBLE לעיל)
        shutdownWiFiForBLE();

        // שלב ג: הפעלת סריקת BLE — WiFi כבוי, BLE מקבל 100% RF
        g_bleUserFound = false;
        startBLEScan();

        enterState(STATE_BLE_SEARCHING);
        break;
    }

    // ─── BLE_SEARCHING: מחפש טלפון המשתמש ───────────────────────────────
    case STATE_BLE_SEARCHING:
    {
        // מסלול 1: הטלפון נמצא על-ידי ה-callback
        if (g_bleUserFound) {
            Serial.println("[BLE] טלפון נמצא — כיבוי BLE, חיבור WiFi מחדש");
            shutdownBLEForWiFi();
            connectWiFi();
            postEmergencyStatus("USER_LOCATED_VIA_BLE");
            enterState(STATE_ROBOT_DEPLOYED);
            break;
        }

        // מסלול 2: בדיקת טביעת אצבע בזמן סריקת BLE
        // UART (IO16/IO17) הוא נחושת בלבד — אינו משפיע כלל על RF 2.4GHz
        // בטוח לבצע בכל עת, גם בזמן BLE פעיל
        if (checkFingerprint() > 0) {
            shutdownBLEForWiFi();
            connectWiFi();
            Serial1.print("RECALL\n");
            enterState(STATE_DISPENSING);
            break;
        }

        // מסלול 3: פג זמן סריקת BLE — התיקון ל"נקודת אל-החזור"
        //
        // לפני גרסה זו: סריקת BLE הייתה רצה ללא הגבלה,
        // והקופסא הייתה "עיוורת ומנותקת מהענן" לנצח.
        //
        // הפתרון: אחרי 2 דקות ללא מציאת הטלפון:
        //   1. כיבוי BLE + שחרור ~80KB RAM
        //   2. חיבור WiFi מחדש
        //   3. PATCH ל-Firestore עם סטטוס חירום
        //   4. מעבר ל-ERROR_STATE לטיפול אנושי
        if ((now - g_stateEnteredAt) >= BLE_SCAN_TIMEOUT_MS) {
            Serial.println("[BLE] פסק זמן 2 דקות — טלפון לא נמצא");
            Serial.println("[BLE] מפעיל רצף שחזור WiFi...");
            enterState(STATE_BLE_TIMEOUT_RECOVERY);
            [[fallthrough]]; // ביצוע שחזור מיידי באותו מחזור loop()
        } else {
            break;
        }
    }

    // ─── BLE_TIMEOUT_RECOVERY: מצב חדש — שחזור WiFi + עדכון ענן ──────
    //
    // מצב זה הוא הפתרון המרכזי לבאג "עיוורת ומנותקת מהענן".
    // מגיעים אליו:
    //   (א) [[fallthrough]] מ-BLE_SEARCHING כשפג הזמן, או
    //   (ב) enterState(STATE_BLE_TIMEOUT_RECOVERY) ממסלול אחר
    //
    // תקציב זמן חסימה מרבי:
    //   BLE_DEINIT_SETTLE_MS       200ms   (ניקוי דרייבר BLE)
    //   WIFI_CONNECT_TIMEOUT_MS  20,000ms  (חיבור WiFi + DHCP)
    //   refreshFirebaseToken()     ~500ms   (Firebase Auth POST)
    //   postEmergencyStatus()      ~800ms   (Firestore PATCH)
    //   ────────────────────────────────────
    //   סה"כ מרבי:               ~21.5 שניות
    //
    // ה-Watchdog (TWDT) ברירת מחדל 60 שניות — בטוח.
    // connectWiFi() משתמש ב-vTaskDelay(100ms) שמאכיל את ה-WDT
    // ─────────────────────────────────────────────────────────────────────
    case STATE_BLE_TIMEOUT_RECOVERY:
    {
        // שלב 1: כיבוי BLE ושחרור RAM
        shutdownBLEForWiFi();

        // שלב 2: הפעלת WiFi מחדש
        Serial.println("[RECOVERY] מפעיל WiFi מחדש לאחר פסק זמן BLE...");
        const bool wifiOk = connectWiFi();

        if (!wifiOk) {
            // הנתב לא זמין — נכנסים ל-ERROR_STATE לטיפול אנושי
            Serial.println("[RECOVERY] חיבור WiFi נכשל -> ERROR_STATE");
            enterState(STATE_ERROR);
            break;
        }
        Serial.println("[RECOVERY] WiFi חובר מחדש בהצלחה");

        // שלב 3: רענון Token (אולי פג בזמן 2 הדקות של סריקת BLE)
        if (idToken.length() == 0 || (millis() - tokenObtainedAt) > TOKEN_EXPIRY_MS)
            refreshFirebaseToken();

        // שלב 4: עדכון סטטוס חירום ב-Firestore
        //
        // מסמך Firestore: /SystemStatus/emergency
        // שדות שנכתבים:
        //   status    : "EMERGENCY_BLE_TIMEOUT"
        //   timestamp : <unix epoch>
        //
        // אפליקציית המשפחה מאזינה:
        //   firestore().doc('SystemStatus/emergency').onSnapshot(snap => {
        //       if (snap.data().status === 'EMERGENCY_BLE_TIMEOUT') {
        //           showEmergencyAlert(); // פוש נוטיפיקיישן לכל הרשומים
        //       }
        //   });
        // ה-listener מופעל תוך ~200ms מרגע הכתיבה
        const bool posted = postEmergencyStatus("EMERGENCY_BLE_TIMEOUT");
        if (posted) {
            Serial.println("[RECOVERY] סטטוס חירום נשלח ל-Firestore — אפליקציה תתריע תוך ~200ms");
        } else {
            Serial.println("[RECOVERY] שליחה ל-Firestore נכשלה — האפליקציה לא תיודע!");
        }

        // שלב 5: מעבר ל-ERROR_STATE
        // ניסיונות WiFi כל 30 שניות ב-ERROR_STATE שומרים על הקופסא נגישה
        Serial.println("[RECOVERY] מעבר ל-ERROR_STATE — נדרשת התערבות אנושית");
        enterState(STATE_ERROR);
        break;
    }

    // ─── DISPENSING: שחרור תרופה דרך I2C ───────────────────────────────
    case STATE_DISPENSING:
    {
        if (!g_compartmentOpen) {
            openCompartment(g_pendingCompartment);
            g_compartmentOpen = true;
            Serial.printf("[DISPENSE] פתיחת תא %u (פקודת I2C)\n", g_pendingCompartment);
        }
        
        // קריאה לפונקציית ה-I2C המהירה (לא חוסמת את הלולאה!)
        DispenseResult result = pollCompartmentDispense();

        if (result == DISPENSE_SUCCESS) {
            Serial.println("[DISPENSE] אישור: תרופה נלקחה בהצלחה.");
            postEmergencyStatus("DOSE_TAKEN_OK");
            g_compartmentOpen = false;
            enterState(STATE_IDLE);
        } 
        else if (result == DISPENSE_PILL_STUCK) {
            Serial.println("[DISPENSE] כדור תקוע במשפך! מחדש אזעקה.");
            g_compartmentOpen = false;
            enterState(STATE_ALARM_RINGING);
        } 
        else if (result == DISPENSE_FAILED) {
            Serial.println("[DISPENSE] שגיאת תקשורת I2C. חוזר ל-IDLE.");
            g_compartmentOpen = false;
            enterState(STATE_IDLE);
        }
        break;
    }

    // ─── ERROR_STATE: שגיאה — ניסיון מחדש כל 30 שניות ──────────────────
    case STATE_ERROR:
        if ((now - g_stateEnteredAt) >= 30000UL) {
            if (connectWiFi()) {
                refreshFirebaseToken();
                Serial.println("[ERROR] WiFi חודש — חזרה ל-IDLE");
                enterState(STATE_IDLE);
            } else {
                g_stateEnteredAt = now;
            }
        }
        break;

    default:
        Serial.println("[WARN] מצב לא מוכר — מאפס ל-IDLE");
        enterState(STATE_IDLE);
        break;
    } // סגירת ה-switch(g_state)

    // 2. סקירת מלאי תאים (כל 30 שניות, לא בזמן שחרור תא — ראו הסבר ב-Hardware_Actuators.ino)
    pollInventoryBitmask();

    // 4. סימולציה של קבלת פקודה מהאפליקציה דרך הסריאל מוניטור
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
            g_pendingCompartment = 1;
            enterState(STATE_ALARM_RINGING);
        }
    }

    // ויתור מרצון: 1ms vTaskDelay מאפשר לפקידת ה-BLE (core 0) לרוקן
    // את תור PDU שלה ולקרוא ל-g_bleCallback.onResult().
    // ללא ויתור זה loop() על core 1 יכול לרעב את פקידת BLE ו-g_bleUserFound
    // לעולם לא יהפוך true למרות שתוצאות סריקה ממתינות
    vTaskDelay(pdMS_TO_TICKS(1));

    //wakeUpServer();
}
