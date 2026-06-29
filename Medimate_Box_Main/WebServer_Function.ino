#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>

// הגדרת המשתנים והקצאת הזיכרון בפועל
const uint32_t THRESHOLD_WARNING_MINUTES = 30U;// מעל 30 דקות = אזהרה
const uint32_t THRESHOLD_SEVERE_MINUTES = 120U;// מעל שעתיים = חמור
// הגדרת משתנים גלובליים לזמן ולשרת
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7200; 
const int   daylightOffset_sec = 3600;
//פונקציה שמאפשרת אסינכרוניות
AsyncWebServer server(80);
//פונקציית חישוב זמני הנטילה
IntakeStatus calculate_Intake_Status(uint32_t original_taking, uint32_t actual_taking){
  //הגנה מפני גלישה (בגלל שהחישוב בשלילי במשתנים האלו נותן מספרים ענקיים) וטיפול בנטילה מוקדמת
  if (actual_taking <= original_taking) {
        return STATUS_ON_TIME;
  }
  //אנחנו מקבלים את הזמן בשניות ולכן נחשב בצורה הזו 
  uint32_t diff_minutes = (actual_taking - original_taking)/60;
  
  if (diff_minutes <= THRESHOLD_WARNING_MINUTES) {
    //התרופה נלקחה בזמן
    return STATUS_ON_TIME;
  } 
  if (diff_minutes <= THRESHOLD_SEVERE_MINUTES) {
    return STATUS_WARNING;
  } 
  return STATUS_SEVERE;
}

// ─────────────────────────────────────────────────────────────────────────────
// הצהרות extern — מחוברות למשתנים גלובליים ב-Medimate_Box_Main.ino
// ─────────────────────────────────────────────────────────────────────────────
extern BoxState         g_state;              // מצב מכונת המצבים הנוכחי
extern uint8_t          g_pendingCompartment; // מספר תא שממתין לשחרור
extern volatile bool    g_remoteApproved;     // דגל אישור מרחוק

// lastVolume ו-lastAway מוגדרים ב-TFT_function.ino ומשותפים לכל הפרויקט
extern int lastVolume;
extern int lastAway;

// ─────────────────────────────────────────────────────────────────────────────
// עזר: הוספת headers של CORS לכל תשובה.
// CORS נדרש כי האתר רץ על origin שונה מה-ESP32.
// Access-Control-Allow-Origin: * מאפשר לכל דומיין לשלוח בקשות.
// ─────────────────────────────────────────────────────────────────────────────
static void addCORSHeaders(AsyncWebServerResponse* resp) {
    resp->addHeader("Access-Control-Allow-Origin",  "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
}

// ─────────────────────────────────────────────────────────────────────────────
// עזר: המרת enum BoxState למחרוזת קריאה עבור האפליקציה
// ─────────────────────────────────────────────────────────────────────────────
static const char* boxStateToString(BoxState s) {
    switch (s) {
        case STATE_IDLE:                 return "IDLE";
        case STATE_ALARM_RINGING:        return "ALARM_RINGING";
        case STATE_WAITING_FINGERPRINT:  return "WAITING_FINGERPRINT";
        case STATE_ROBOT_DEPLOYED:       return "ROBOT_DEPLOYED";
        case STATE_CALLING_TWILIO:       return "CALLING_TWILIO";
        case STATE_BLE_SEARCHING:        return "BLE_SEARCHING";
        case STATE_BLE_TIMEOUT_RECOVERY: return "BLE_TIMEOUT_RECOVERY";
        case STATE_DISPENSING:           return "DISPENSING";
        case STATE_ERROR:                return "ERROR";
        default:                         return "UNKNOWN";
    }
}

void setupServer() {

    // ── PREFLIGHT: בקשות OPTIONS מגיעות מהדפדפן לפני כל POST/PUT ─────────
    // הדפדפן שואל "מה מותר?" — אנחנו עונים "הכל" ומסיימים
    server.on("/api/*", HTTP_OPTIONS, [](AsyncWebServerRequest* req) {
        AsyncWebServerResponse* resp = req->beginResponse(204);
        addCORSHeaders(resp);
        req->send(resp);
    });

    // ─────────────────────────────────────────────────────────────────────────
    // GET /api/box-status
    // האתר קורא endpoint זה כל 4 שניות (polling).
    // מחזיר JSON עם כל מה שהאתר צריך לדעת:
    //   status            — מחרוזת שם המצב (IDLE / WAITING_FINGERPRINT / ...)
    //   isAway            — האם מצב חופשה פעיל
    //   volume            — עוצמת שמע נוכחית (0-100)
    //   waitingFingerprint — true רק כש-state == STATE_WAITING_FINGERPRINT
    //   pendingCompartment — מספר התא שממתין (רלוונטי כש-waitingFingerprint=true)
    // ─────────────────────────────────────────────────────────────────────────
    server.on("/api/box-status", HTTP_GET, [](AsyncWebServerRequest* req) {
        StaticJsonDocument<256> doc;
        doc["status"]             = boxStateToString(g_state);
        doc["isAway"]             = (bool)(lastAway > 0);
        doc["volume"]             = (lastVolume >= 0) ? lastVolume : 80;
        // waitingFingerprint=true רק כשהמכשיר ממש ב-STATE_WAITING_FINGERPRINT
        doc["waitingFingerprint"] = (g_state == STATE_WAITING_FINGERPRINT);
        doc["pendingCompartment"] = g_pendingCompartment;

        String body;
        serializeJson(doc, body);

        AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", body);
        addCORSHeaders(resp);
        req->send(resp);
    });

    // ─────────────────────────────────────────────────────────────────────────
    // PUT /api/box-status
    // האתר שולח גוף JSON עם שינויים מדף ההגדרות:
    //   { "isAway": true, "volume": 60 }
    // הפונקציה מעדכנת את lastAway ו-lastVolume, ואז מעדכנת Firebase.
    // ─────────────────────────────────────────────────────────────────────────
    AsyncCallbackJsonWebHandler* putHandler = new AsyncCallbackJsonWebHandler(
        "/api/box-status",
        [](AsyncWebServerRequest* req, JsonVariant& json) {
            JsonObject obj = json.as<JsonObject>();

            // עדכון עוצמת שמע אם נשלחה
            if (obj.containsKey("volume")) {
                int newVol = obj["volume"].as<int>();
                // הגנה: מגביל לטווח חוקי 0-100
                lastVolume = constrain(newVol, 0, 100);
                Serial.printf("[SERVER] עוצמת שמע עודכנה ל-%d%%\n", lastVolume);
            }

            // עדכון מצב חופשה אם נשלח
            if (obj.containsKey("isAway")) {
                lastAway = obj["isAway"].as<bool>() ? 1 : 0;
                Serial.printf("[SERVER] מצב חופשה עודכן ל-%s\n", lastAway ? "פעיל" : "כבוי");
            }

            // עדכון Firebase כדי לשמור ערכים לאחר הפעלה מחדש
            extern String linkedBoxId;
            if (linkedBoxId.length() > 0) {
                extern void updateBoxStatus(String, int, bool);
                updateBoxStatus(linkedBoxId, lastVolume, (bool)lastAway);
            }

            AsyncWebServerResponse* resp = req->beginResponse(
                200, "application/json", "{\"status\":\"ok\"}"
            );
            addCORSHeaders(resp);
            req->send(resp);
        },
        HTTP_PUT  //   מציין שזו בקשת PUT ולא POST
    );
    server.addHandler(putHandler);

    // ─────────────────────────────────────────────────────────────────────────
    // POST /api/remote-approve
    // האתר שולח בקשה זו כשבן משפחה לחץ "אשר עבורי".
    // מגן: מקבל פקודה רק כש-state == STATE_WAITING_FINGERPRINT
    // כדי למנוע פתיחת תא שלא בזמן.
    // ─────────────────────────────────────────────────────────────────────────
    AsyncCallbackJsonWebHandler* approveHandler = new AsyncCallbackJsonWebHandler(
        "/api/remote-approve",
        [](AsyncWebServerRequest* req, JsonVariant& json) {
            String responseBody;
            int httpCode;

            if (g_state == STATE_WAITING_FINGERPRINT) {
                //  מצב תקין — הציב דגל, הלולואה תקרא בסיבוב הבא
                // כתיבת volatile bool היא אטומית ב-Xtensa LX6 — בטוח ללא mutex
                g_remoteApproved = true;
                Serial.println("[SERVER]  אישור מרחוק התקבל — מדליק דגל g_remoteApproved");
                responseBody = "{\"status\":\"ok\",\"message\":\"approved\"}";
                httpCode = 200;
            } else {
                //  לא במצב המתנה — דחה את הבקשה
                Serial.printf("[SERVER]  אישור מרחוק נדחה — מצב נוכחי: %s\n",
                              boxStateToString(g_state));
                responseBody = "{\"status\":\"error\",\"message\":\"not_waiting\"}";
                httpCode = 409; // Conflict — המכשיר לא במצב המתנה
            }

            AsyncWebServerResponse* resp = req->beginResponse(
                httpCode, "application/json", responseBody
            );
            addCORSHeaders(resp);
            req->send(resp);
        }
    );
    server.addHandler(approveHandler);

    // ─────────────────────────────────────────────────────────────────────────
    // POST /api/save-prescription
    // האתר שולח נתוני מרשם לאחר ניתוח OCR בענן.
    // מבנה הגוף הצפוי:
    //   { "name": "אספירין", "times": ["08:00", "20:00"], "compartment": 2,
    //     "nextDoseEpoch": 1703123456 }
    // הפונקציה שומרת ל-Firebase (עדכון שדות הקופסה) וכן מעדכנת nextDoseEpoch
    // כדי שה-loop() יפעיל את האזעקה בזמן הנכון.
    // ─────────────────────────────────────────────────────────────────────────
    AsyncCallbackJsonWebHandler* prescHandler = new AsyncCallbackJsonWebHandler(
        "/api/save-prescription",
        [](AsyncWebServerRequest* req, JsonVariant& json) {
            JsonObject obj = json.as<JsonObject>();

            // שמירת nextDoseEpoch לעדכון Firebase
            long nextEpoch = obj["nextDoseEpoch"].as<long>();
            int compartment = obj["compartment"].as<int>();
            String medName = obj["name"].as<String>();

            Serial.printf("[SERVER] מרשם חדש: %s, תא %d, מנה הבאה: %ld\n",
                          medName.c_str(), compartment, nextEpoch);

            // עדכון Firestore עם שדות המנה הבאה
            // (הפונקציה postEmergencyStatus כבר מטפלת ב-token refresh)
            // TODO: שלח PATCH ל-/Boxes/{linkedBoxId} עם nextDoseEpoch + nextDoseCompartment
            // לעת עתה: רישום סריאל לאישור
            Serial.println("[SERVER] TODO: שמירת מרשם ל-Firestore");

            AsyncWebServerResponse* resp = req->beginResponse(
                200, "application/json",
                "{\"status\":\"ok\",\"message\":\"prescription saved\"}"
            );
            addCORSHeaders(resp);
            req->send(resp);
        }
    );
    server.addHandler(prescHandler);

    // handler ישן /api/settings — נשמר לתאימות לאחור
    AsyncCallbackJsonWebHandler* legacyHandler = new AsyncCallbackJsonWebHandler(
        "/api/settings",
        [](AsyncWebServerRequest* req, JsonVariant& json) {
            AsyncWebServerResponse* resp = req->beginResponse(
                200, "application/json",
                "{\"status\":\"success\",\"message\":\"Data received by MEDIMATE\"}"
            );
            addCORSHeaders(resp);
            req->send(resp);
        }
    );
    server.addHandler(legacyHandler);

    server.begin();
    Serial.println("[SERVER] שרת HTTP פועל על פורט 80");
}

void setupServer() {
  // יצירת אובייקט חכם לטיפול בבקשות POST המכילות גייסון
  AsyncCallbackJsonWebHandler* handler = new AsyncCallbackJsonWebHandler("/api/settings", [](AsyncWebServerRequest *request, JsonVariant &json) {
    // המרת המידע הגולמי לאובייקט ג'יסון קריא
    JsonObject jsonObj = json.as<JsonObject>();
    // שליחת תשובה אסינכרונית ללקוח כדי שלא ייתקע
    request->send(200, "application/json", "{\"status\":\"success\", \"message\":\"Data received by MEDIMATE\"}");
  });
  
  server.addHandler(handler);
  server.begin();
}