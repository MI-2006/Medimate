// ==========================================
// קובץ ראשי: Medimate_Robot_main.ino
// תפקיד: ניהול משאבים, לולאה ראשית ומכונת מצבים
// עדכון v2: הוספת לוגיקת צילום, שליחה לשרת זיהוי אנשים,
//           ומכונת מצבים לא-חוסמת מבוססת millis()
// ==========================================

// שימוש בספריית הליבה של ארדואינו
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

// ספריית המצלמה — ממשק חומרה של ESP-IDF לניהול חיישן OV2640
#include "esp_camera.h"

// כמות החיישנים האולטרסוניים המחוברים לרובוט
#define NUM_SENSORS 4

// ─────────────────────────────────────────────────────────────────────────────
// הגדרת מבנה נתונים. אנו מאגדים את כל התכונות של חיישן בודד לאובייקט אחד.
// ─────────────────────────────────────────────────────────────────────────────
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
    {27, 14, 0, 0, false}, // חיישן 1: Trig=27, Echo=14
    {32, 33, 0, 0, false}, // חיישן 2: Trig=32, Echo=33
    {5,  18, 0, 0, false}  // חיישן 3: Trig=5,  Echo=18
};

// ─────────────────────────────────────────────────────────────────────────────
// הגדרות רשת ושרת
// כל הקבועים האלה מוגדרים כאן פעם אחת — אל תשנה אותם בתוך פונקציות
// ─────────────────────────────────────────────────────────────────────────────
static const char* WIFI_SSID         = "YOUR_SSID";
static const char* WIFI_PASSWORD     = "YOUR_PASSWORD";

// כתובת שרת Flask המתארח ב-Render. הנתיב חייב להתאים לנתיב ב-@app.route
static const char* SERVER_DETECT_URL = "https://medimate.onrender.com/api/vision/detect";

// מפתח סודי משותף — נבדק על ידי require_robot_key() בשרת לפני קריאת גוף הבקשה.
// שמור אותו ב-NVS בסביבת ייצור במקום לקשור אותו בתוך הקוד
static const char* ROBOT_API_KEY     = "YOUR_SECRET_KEY_HERE";

// ─────────────────────────────────────────────────────────────────────────────
// מיפוי פינים למצלמת ESP32-CAM (AI-Thinker)
// הפינים קבועים לפי מסלולי הלוח — אין לשנות GPIO0 (strap להפעלה)
// ─────────────────────────────────────────────────────────────────────────────
static const int8_t CAM_PIN_PWDN  =  32;
static const int8_t CAM_PIN_RESET =  -1; // אין פין reset פיזי ב-AI-Thinker
static const int8_t CAM_PIN_XCLK  =   0; // שעון מאסטר 20MHz לחיישן
static const int8_t CAM_PIN_SIOD  =  26; // נתוני SCCB (תואם I²C)
static const int8_t CAM_PIN_SIOC  =  27; // שעון SCCB
static const int8_t CAM_PIN_D7    =  35;
static const int8_t CAM_PIN_D6    =  34;
static const int8_t CAM_PIN_D5    =  39;
static const int8_t CAM_PIN_D4    =  38;
static const int8_t CAM_PIN_D3    =  37;
static const int8_t CAM_PIN_D2    =  36;
static const int8_t CAM_PIN_D1    =  21;
static const int8_t CAM_PIN_D0    =  19;
static const int8_t CAM_PIN_VSYNC =  25;
static const int8_t CAM_PIN_HREF  =  23;
static const int8_t CAM_PIN_PCLK  =  22;

// ─────────────────────────────────────────────────────────────────────────────
// פינים למנועים — דרייבר L298N (גשר-H כפול)
// uint8_t: מספרי GPIO 0-39 מתאימים ל-6 ביט, uint8_t הוא הטיפוס הנכון
// ─────────────────────────────────────────────────────────────────────────────
static const uint8_t MOTOR_L_FWD = 13;
static const uint8_t MOTOR_L_IN1 = 15;
static const uint8_t MOTOR_L_IN2 = 16;
static const uint8_t MOTOR_R_FWD = 17;
static const uint8_t MOTOR_R_IN1 = 19;
static const uint8_t MOTOR_R_IN2 = 21;

// ─────────────────────────────────────────────────────────────────────────────
// קבועי זמן — כולם uint32_t כדי להתאים לסוג החזרת millis()
// שימוש ב-int יגרום לבאג: (uint32_t)now - (int)start יכול לגלוש לערך שלילי
// ─────────────────────────────────────────────────────────────────────────────
// נתונים למניעת התנגשות בין הדלקת הרובוט לבקשה מהשרת
unsigned long robotStartTime;
bool isServerReady = false;
const unsigned long SERVER_BOOT_DELAY = 30000; // 30 שניות המתנה לשרת

unsigned long lastCameraTime = 0;            // שומר את הזמן של הצילום האחרון
const unsigned long CAMERA_INTERVAL = 10000; // כל כמה זמן לצלם (במילי-שניות)

// קבוע למגבלת זמן תגובת HTTP — חמש שניות מספיקות לשרת Render לענות
static const uint32_t HTTP_TIMEOUT_MS      = 5000UL;
// קבוע למגבלת זמן חיבור WiFi
static const uint32_t WIFI_CONNECT_TIMEOUT = 20000UL;
// מרחק סף (ס"מ) לעצירת הרובוט מפני מכשול
static const uint32_t OBSTACLE_THRESHOLD_CM = 30UL;

// ─────────────────────────────────────────────────────────────────────────────
// מכונת מצבים של הרובוט
// שימוש ב-enum class (enum חזק-טיפוס) מונע השוואות שגויות כמו if(state==1)
// ─────────────────────────────────────────────────────────────────────────────
enum class RobotState : uint8_t {
    IDLE,           // ממתין למרווח הצילום הבא
    CAPTURING,      // מצלם פריים מהמצלמה ושולח לשרת
    PERSON_FOUND,   // השרת אישר זיהוי אדם — שליחת התראה לקופסא
    ERROR_RECOVERY  // שגיאת מצלמה/WiFi — ניסיון התחברות מחדש
};

static RobotState g_state          = RobotState::IDLE;
static uint32_t   g_lastScanTime   = 0UL; // millis() של הצילום האחרון
static bool       g_personDetected = false;

// HTTPClient גלובלי — שימוש חוזר בו שומר על חיבור keep-alive
// וחוסך את לחיצת יד TLS (~400ms) בכל מחזור צילום
static HTTPClient g_http;

// ─────────────────────────────────────────────────────────────────────────────
// פסיקות חומרה לחיישן הד — IRAM_ATTR
// הסבר: פונקציות ISR חייבות להימצא ב-SRAM פנימי (לא Flash).
// קריאת Flash מהירה XIP יכולה לעכב את ה-ISR ב-200-500ns — עיכוב זה
// יפגע במדידת רוחב פולס ה-Echo שדורשת דיוק של מיקרו-שניות
// ─────────────────────────────────────────────────────────────────────────────
static volatile uint32_t g_echoRiseUs    = 0UL;
static volatile uint32_t g_echoDurationUs = 0UL;
static volatile bool      g_echoDataReady = false;

// ISR עלייה: שומר את חותמת הזמן של קצה ה-RISING
void IRAM_ATTR echoRisingISR() {
    // micros() בטוח בתוך ISR ב-ESP32 — קורא ישירות מטיימר חומרה
    g_echoRiseUs = micros();
}

// ISR ירידה: מחשב רוחב הפולס ומסמן שהנתון מוכן
void IRAM_ATTR echoFallingISR() {
    // ללא חלוקה או נקודה צפה כאן — ISR חייב להיות O(1) ופחות מ-1μs
    g_echoDurationUs = micros() - g_echoRiseUs;
    g_echoDataReady  = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// אתחול מצלמה
// ─────────────────────────────────────────────────────────────────────────────
static esp_err_t initCamera() {
    camera_config_t cfg = {};
    cfg.pin_pwdn     = CAM_PIN_PWDN;
    cfg.pin_reset    = CAM_PIN_RESET;
    cfg.pin_xclk     = CAM_PIN_XCLK;
    cfg.pin_sscb_sda = CAM_PIN_SIOD;
    cfg.pin_sscb_scl = CAM_PIN_SIOC;
    cfg.pin_d7 = CAM_PIN_D7; cfg.pin_d6 = CAM_PIN_D6;
    cfg.pin_d5 = CAM_PIN_D5; cfg.pin_d4 = CAM_PIN_D4;
    cfg.pin_d3 = CAM_PIN_D3; cfg.pin_d2 = CAM_PIN_D2;
    cfg.pin_d1 = CAM_PIN_D1; cfg.pin_d0 = CAM_PIN_D0;
    cfg.pin_vsync = CAM_PIN_VSYNC;
    cfg.pin_href  = CAM_PIN_HREF;
    cfg.pin_pclk  = CAM_PIN_PCLK;

    // 20MHz — תדר השעון המומלץ לחיישן OV2640
    // ערכים נמוכים יותר חוסכים חשמל אבל גורמים לפסים תחת תאורת ניאון
    cfg.xclk_freq_hz = 20000000;
    cfg.ledc_timer   = LEDC_TIMER_0;
    cfg.ledc_channel = LEDC_CHANNEL_0;

    // פורמט JPEG: חיישן OV2640 מדחס ל-JPEG בחומרה (בלוק DSP)
    // QQVGA גולמי (160×120 × 2 בתים) = 38,400 בתים
    // JPEG באיכות 10 דוחס את אותה סצנה ל-8-20KB בלבד
    // מטען קטן יותר = POST מהיר יותר = פחות זמן אוויר WiFi
    cfg.pixel_format = PIXFORMAT_JPEG;

    // QQVGA (160×120): הגודל הקטן ביותר הזמין
    // גלאי HOG בשרת יעיל עד 64×128 חלונות, כך שאדם ב-<3 מ'
    // ממלא מספיק פיקסלים לזיהוי אמין
    cfg.frame_size   = FRAMESIZE_QQVGA;

    // איכות JPEG: 1-63, ערך נמוך יותר = איכות טובה יותר (יותר בתים)
    // איכות 10 נותנת ~15KB לפריים QQVGA
    cfg.jpeg_quality = 10;

    // מאגר פריים אחד ב-PSRAM — אנחנו לא מזרימים, צילום בודד מספיק
    // כל מאגר QQVGA שומר ~64KB ב-PSRAM
    cfg.fb_count    = 1;

    // כפה את מאגר הפריים ל-PSRAM החיצוני
    // ללא דגל זה ה-SDK ינסה DRAM פנימי ויפגע בערמת WiFi (~90KB)
    // ובמאגרי TLS (~36KB) הדרושים לחיבור HTTPS
    cfg.fb_location = CAMERA_FB_IN_PSRAM;
    cfg.grab_mode   = CAMERA_GRAB_WHEN_EMPTY;

    return esp_camera_init(&cfg);
}

// ─────────────────────────────────────────────────────────────────────────────
// חיבור ל-WiFi
// ─────────────────────────────────────────────────────────────────────────────
static bool connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return true;
    WiFi.mode(WIFI_STA); // מצב תחנה: התחבר לנקודת גישה, אל תשדר
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t deadline = millis() + WIFI_CONNECT_TIMEOUT;
    while (WiFi.status() != WL_CONNECTED && millis() < deadline)
        // vTaskDelay: מרשה לפקידת WiFi ב-core 0 לעבד תשובות DHCP
        vTaskDelay(pdMS_TO_TICKS(100));
    return (WiFi.status() == WL_CONNECTED);
}

// ─────────────────────────────────────────────────────────────────────────────
// פונקציות עזר לניהול מנועים
// כאשר מנועים מקבלים פקודה לזוז, קרא ל- updateEyeState(true)
// כאשר המנועים עוצרים, קרא ל- updateEyeState(false)
// ─────────────────────────────────────────────────────────────────────────────
inline void motorsForward() {
    digitalWrite(MOTOR_L_FWD, HIGH); digitalWrite(MOTOR_L_IN1, HIGH);
    digitalWrite(MOTOR_L_IN2, LOW);
    digitalWrite(MOTOR_R_FWD, HIGH); digitalWrite(MOTOR_R_IN1, HIGH);
    digitalWrite(MOTOR_R_IN2, LOW);
    updateEyeState(true); // הפעלת נורות הלד-העין בזמן תנועה
}
inline void motorsStop() {
    digitalWrite(MOTOR_L_IN1, LOW); digitalWrite(MOTOR_L_IN2, LOW);
    digitalWrite(MOTOR_R_IN1, LOW); digitalWrite(MOTOR_R_IN2, LOW);
    updateEyeState(false); // כיבוי נורות הלד-העין בזמן עצירה
}
inline void motorsTurnLeft() {
    // היגוי החלקה: גלגלי שמאל חוזרים, גלגלי ימין קדימה
    digitalWrite(MOTOR_L_IN1, LOW);  digitalWrite(MOTOR_L_IN2, HIGH);
    digitalWrite(MOTOR_R_IN1, HIGH); digitalWrite(MOTOR_R_IN2, LOW);
    updateEyeState(true);
}

// ─────────────────────────────────────────────────────────────────────────────
// מניעת התנגשויות — לא חוסם
// נקרא בכל מחזור loop(). שולח פולס Trig כל 50ms, קורא תוצאת ISR כשמוכנה.
// לעולם לא משתמש ב-pulseIn() שחוסם את המעבד
// ─────────────────────────────────────────────────────────────────────────────
static uint32_t g_lastTrigTime = 0UL;

static uint32_t getDistanceCm() {
    if (!g_echoDataReady) return UINT32_MAX; // עדיין אין נתון — חזור מיד
    g_echoDataReady = false;
    // מהירות הקול = 0.0343 ס"מ/μs בטמפ' 20°C
    // חלוקה ב-2 עבור מסלול הלוך-חזור (Trig → מכשול → Echo)
    return static_cast<uint32_t>(g_echoDurationUs * 0.01715f);
}

static void runUltrasonicTrigger() {
    if (millis() - g_lastTrigTime < 50UL) return; // הגבלת קצב: 20Hz
    g_lastTrigTime = millis();
    // פולס Trig: 10μs HIGH. זהו השימוש היחיד המותר ב-delayMicroseconds
    // בקוד זה: 10μs קצר מכדי לפגוע בערמת LwIP (שצריכה 1ms לפחות)
    digitalWrite(sensors[0].trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(sensors[0].trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(sensors[0].trigPin, LOW);
}

// ─────────────────────────────────────────────────────────────────────────────
// צילום פריים ושליחתו לשרת הזיהוי
//
// חוזה זיכרון (קריטי ב-PSRAM):
//   esp_camera_fb_get() מקצה camera_fb_t* ב-PSRAM.
//   המאגר שייך לדרייבר המצלמה עד שנקרא esp_camera_fb_return().
//   אי-החזרת המאגר גורמת לאגירת כל ה-PSRAM אחרי fb_count צילומים,
//   ואז esp_camera_fb_get() תחזיר NULL לנצח.
//
//   לכן: משתמשים בדגל personFound ולא ב-return מוקדם —
//   כדי שה-return של המאגר (שלב 3) תמיד יבוצע גם אם ה-HTTP נכשל
// ─────────────────────────────────────────────────────────────────────────────
static bool captureAndPost() {
    // שלב 1: צילום פריים
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb == nullptr) {
        // NULL = מאגר PSRAM מוצה (דליפה) או חיישן לא מגיב
        Serial.println("[CAM] esp_camera_fb_get() החזיר NULL — דליפת PSRAM?");
        return false;
    }
    Serial.printf("[CAM] פריים צולם: %zu בתים\n", fb->len);

    // שלב 2: שליחת POST ל-HTTP
    bool personFound = false;
    if (g_http.begin(SERVER_DETECT_URL)) {
        // כותרת X-Robot-Key: נבדקת בשרת לפני קריאת גוף הבקשה.
        // זה מונע ממשתמשים לא מורשים לגרום לגלישת זיכרון בשרת
        g_http.addHeader("Content-Type", "image/jpeg");
        g_http.addHeader("X-Robot-Key",  ROBOT_API_KEY);

        // מגבלת זמן: אם השרת לא עונה תוך 5 שניות, POST מחזיר שגיאה.
        // מאגר הפריים יוחזר בשלב 3 ללא קשר לתוצאת ה-HTTP
        g_http.setTimeout(HTTP_TIMEOUT_MS);

        // שליחת בתי ה-JPEG ישירות מ-PSRAM
        // HTTPClient מחלק לחבילות TCP בגודל MTU (~1460 בתים ב-802.11)
        int httpCode = g_http.POST(
            reinterpret_cast<uint8_t*>(fb->buf),
            fb->len
        );

        if (httpCode == HTTP_CODE_OK) {
            // ניתוח מינימלי של תשובת JSON — רק בודקים את שדה person_detected
            // ניתוח מלא עם ArduinoJson יעלה ~2KB SRAM; strstr מספיק כאן
            String body = g_http.getString();
            Serial.printf("[HTTP] תשובת שרת (%d): %s\n", httpCode, body.c_str());
            personFound = body.indexOf("\"person_detected\":true") >= 0;
        } else if (httpCode == HTTPC_ERROR_READ_TIMEOUT) {
            // Render free tier: אתחול קר לאחר חוסר פעילות לוקח עד 50 שניות
            Serial.println("[HTTP] פסק זמן — אתחול קר של שרת Render?");
        } else {
            Serial.printf("[HTTP] קוד תשובה לא צפוי: %d\n", httpCode);
        }
        g_http.end();
    } else {
        Serial.println("[HTTP] g_http.begin() נכשל — בדוק URL");
    }

    // שלב 3: החזרת מאגר PSRAM — חובה! תמיד, גם אחרי כישלון HTTP
    // אחרי הקריאה הזו fb הוא מצביע תלוי — מאפסים אותו
    esp_camera_fb_return(fb);
    fb = nullptr;

    return personFound;
}

// ─────────────────────────────────────────────────────────────────────────────
// setup() — אתחול המערכת
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    // פתיחת ערוץ תקשורת טורית במהירות גבוהה כדי שההדפסות לא יעכבו את המעבד
    Serial.begin(115200);

    // הגדרת התקשורת של המצלמה — Serial2 על IO16(RX)/IO21(TX)
    // RX של הבקר ← TX של המצלמה, TX של הבקר → RX של המצלמה
    Serial2.begin(115200, SERIAL_8N1, 16, 21);

    // אתחול פינים של המנועים
    pinMode(MOTOR_L_FWD, OUTPUT); pinMode(MOTOR_L_IN1, OUTPUT);
    pinMode(MOTOR_L_IN2, OUTPUT);
    pinMode(MOTOR_R_FWD, OUTPUT); pinMode(MOTOR_R_IN1, OUTPUT);
    pinMode(MOTOR_R_IN2, OUTPUT);
    motorsStop(); // ודא שהרובוט לא זז בזמן האתחול

    // קריאה לפונקציית האתחול שנמצאת בלשונית השנייה
    initUltrasonic();

    // הגדרת פסיקות לחיישן האולטרסוני הראשי (חיישן 0)
    // שתי ISR על אותו פין: RISING שומר זמן התחלה, FALLING מחשב רוחב הפולס
    // בקר הפסיקות של ESP32 תומך בכך ישירות
    attachInterrupt(digitalPinToInterrupt(sensors[0].echoPin), echoRisingISR,  RISING);
    attachInterrupt(digitalPinToInterrupt(sensors[0].echoPin), echoFallingISR, FALLING);

    //RAM שומר את המחרוזת בזיכרון ה-פלאש (קבוע) במקום ב
    Serial.println(F("System Status: MULTI_ULTRASONIC_INITIALIZED (TABS BUILD)+ Camera Link"));

    // הפעלת נורות הלד-העין
    setupEye();

    // אתחול מצלמה — כישלון כאן הוא קטלני (חומרה לא מגיבה)
    esp_err_t camErr = initCamera();
    if (camErr != ESP_OK) {
        Serial.printf("[CAM] אתחול נכשל: 0x%x\n", camErr);
        // לולאה אינסופית: ה-Watchdog יאפס את הבקר ויאפשר ניסיון נוסף
        while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }
    Serial.println("[CAM] מצלמה אותחלה בהצלחה");

    // חיבור ל-WiFi
    if (!connectWiFi()) {
        Serial.println("[WiFi] חיבור נכשל — יתנסה מחדש ב-loop()");
        g_state = RobotState::ERROR_RECOVERY;
    } else {
        Serial.printf("[WiFi] מחובר. IP: %s\n", WiFi.localIP().toString().c_str());
        g_state = RobotState::IDLE;
    }

    // שמירת זמן ההדלקה
    robotStartTime = millis();
}

// ─────────────────────────────────────────────────────────────────────────────
// loop() — רץ על core 1; WiFi/LwIP רצים על core 0
//
// כלל הברזל: אין delay() בשום מקום.
// delay() חוסם את פקידת ה-idle של FreeRTOS שאחראית ל:
//   א. האכלת ה-Watchdog החומרתי (TWDT) — רעב גורם לאיפוס
//   ב. עיבוד ACK של TCP ב-LwIP — רעב מ-1500ms קורע חיבורים
//   ג. ניהול כפתורי beacons של WiFi ב-core 0
// כל תזמון מיושם עם millis() ומכונת מצבים
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
    const uint32_t now = millis();

    // קריאה למתזמן החיישנים בכל מחזור. הפונקציה הזו לא חוסמת את המעבד.
    runUltrasonicScheduler();

    // מניעת התנגשויות: בדיקה רציפה ולא-חוסמת של המרחק
    uint32_t distCm = getDistanceCm();
    if (distCm != UINT32_MAX && distCm < OBSTACLE_THRESHOLD_CM) {
        // מכשול ב-30 ס"מ — עצור ופנה, בכל מצב שבו נמצאים
        motorsStop();
        motorsTurnLeft();
        // לא משנים מצב — לוגיקת הנהיגה תתחדש אחרי שהמרחק יגדל
        return; // דלג על מכונת המצבים במחזור זה
    }

    // ─── מכונת המצבים ───────────────────────────────────────────────────────
    switch (g_state) {

        case RobotState::IDLE:
            // נסיעה קדימה תוך המתנה למרווח הצילום הבא
            motorsForward();
            if (now - g_lastScanTime >= CAMERA_INTERVAL) {
                g_lastScanTime = now;
                g_state = RobotState::CAPTURING;
            }
            break;

        case RobotState::CAPTURING:
            // עצור מנועים בזמן הצילום: רטט מהמנועים גורם לארטיפקטים
            // בחיישן CMOS OV2640 עקב סדר הסריקה ה-Rolling
            motorsStop();

            // בדיקה: האם עבר מספיק זמן והאם המצלמה צריכה לפעול?
            if (!isServerReady) {
                if (now - robotStartTime > SERVER_BOOT_DELAY) {
                    isServerReady = true;
                    Serial.println("Server should be awake. Enabling camera triggers.");
                }
            }

            // הפעלת המצלמה רק אם השרת מוכן
            if (isServerReady) {
                g_personDetected = captureAndPost();
                // הערה: captureAndPost() חוסם את ה-HTTP (~200-5000ms)
                // זמן מגבלת ה-HTTP (5 שניות) קטן מ-Watchdog (8 שניות כברירת מחדל)
                // כך שאין צורך ב-esp_task_wdt_reset() מפורש
            }

            g_state = g_personDetected ? RobotState::PERSON_FOUND : RobotState::IDLE;
            break;

        case RobotState::PERSON_FOUND:
            // אדם זוהה: עצור, הדלק LED, שלח הודעה לקופסא
            motorsStop();
            // שליחת אות לקופסא הראשית דרך Serial2 (UART)
            Serial2.println("PERSON_DETECTED");
            Serial.println("[STATE] אדם זוהה — מתריע לקופסא דרך Serial2");
            // חזרה למצב IDLE: הקופסא תחליט על הפעולה הבאה
            g_state = RobotState::IDLE;
            break;

        case RobotState::ERROR_RECOVERY:
            // ניסיון חיבור WiFi מחדש כל 30 שניות
            if (now - g_lastScanTime >= 30000UL) {
                g_lastScanTime = now;
                Serial.println("[WiFi] מנסה להתחבר מחדש...");
                if (connectWiFi()) {
                    Serial.println("[WiFi] חובר מחדש בהצלחה");
                    g_state = RobotState::IDLE;
                }
            }
            break;

        default:
            g_state = RobotState::IDLE;
            break;
    }

    // קבלת תשובות מהמצלמה
    checkCameraResponse();

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

    // ויתור מרצון: 1ms vTaskDelay מאפשר לפקידת ה-WiFi (core 0) לעבד
    // ACK-ים של TCP ופריימי beacon ממתינים.
    // ללא ויתור זה loop() על core 1 עלול לרעב את הפקידה ב-core 0
    vTaskDelay(pdMS_TO_TICKS(1));
}
