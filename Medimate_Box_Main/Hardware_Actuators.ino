// ===========================================================================
//  Hardware_Actuators.ino
//  פרויקט מדימייט — מיכל יזרעאלי
//
//  קובץ זה אחראי על:
//    • מכונת מצבים מערכתית (זרימת האזעקה, PIR, שמירת טביעת אצבע, שחרור תא)
//    • חיישן תנועה HC-SR501 — לא חוסם, מבוסס millis()
//    • מנוע סרוו — מוק בלבד (פלט לסריאל, ללא PWM)
//    • נגן MP3 מסוג DFPlayer Mini — ניגון מנגינה וטיפול ב-snooze
//
//  UART1 (פינים 25/26) שמור לנגן ה-MP3.
//  Serial2 (פינים 16/17) שמור אך ורק לחיישן טביעת האצבע.
//  GPIO 34 (פין קלט בלבד) משמש לחיישן ה-PIR.
// ===========================================================================

#include <DFRobotDFPlayerMini.h>

// ---------------------------------------------------------------------------
// הגדרות פינים
// ---------------------------------------------------------------------------
#define PIR_PIN         34    // פלט דיגיטלי של HC-SR501 (פין קלט בלבד)

#define MP3_RX_PIN      25    // פין קלט ESP32 — מחובר לפין TX של נגן ה-MP3
#define MP3_TX_PIN      26    // פין פלט ESP32 — מחובר לפין RX של נגן ה-MP3

// פיני סרוו — מוגדרים כ-OUTPUT לעתיד, ללא PWM כרגע
#define SERVO_PIN_1     13
#define SERVO_PIN_2     12
#define SERVO_PIN_3     14
#define SERVO_PIN_4     27
#define SERVO_PIN_5     32

// ---------------------------------------------------------------------------
// קבועי זמן — כולם במילי-שניות, סוג unsigned long כדי להתאים ל-millis()
// ---------------------------------------------------------------------------
static const unsigned long SNOOZE_INTERVAL_MS   = 120000UL; // זמן שקט בין אזעקה לאזעקה — 2 דקות
static const unsigned long PIR_WAIT_TIMEOUT_MS  =  30000UL; // מקסימום המתנה לגילוי PIR — 30 שניות
static const unsigned long VERIFY_TIMEOUT_MS    =  15000UL; // חלון זמן לאימות טביעת אצבע — 15 שניות
static const unsigned long PIR_DEBOUNCE_MS      =   2000UL; // זמן התעלמות מטריגרים חוזרים של PIR

// ---------------------------------------------------------------------------
// מכונת המצבים המערכתית — רשימת המצבים האפשריים
// ---------------------------------------------------------------------------
// זרימה:
//   IDLE ──(triggerAlarm)──► ALARM_RINGING ──(PIR)──► ACTIVE_UI
//          ▲                      │                        │
//          │               (פסק זמן/אין PIR)        (אצבע אומתה)
//          │                      ▼                        ▼
//          │                  SNOOZED ◄──────────── DISPENSING
//          │                      │                        │
//          └──────────────────────┘                        │
//                  (פג ה-snooze → חזרה לאזעקה)   (שחרור הושלם)
//                                                          └──► IDLE
enum SystemState : uint8_t {
    STATE_IDLE          = 0, // ממתין לאזעקה מהענן
    STATE_ALARM_RINGING = 1, // אזעקה פעילה, מנגינה מתנגנת, ממתין ל-PIR
    STATE_ACTIVE_UI     = 2, // גולש זוהה, חלון טביעת אצבע פתוח
    STATE_DISPENSING    = 3, // זהות אומתה — שחרור תא תרופה
    STATE_SNOOZED       = 4  // מרווח שקט לפני ניגון חוזר
};

// ---------------------------------------------------------------------------
// משתני מצב פנימיים של הקובץ
// static = נגישות מוגבלת לקובץ זה בלבד, למניעת זיהום מרחב השמות הגלובלי
// ---------------------------------------------------------------------------
static SystemState   currentState         = STATE_IDLE;
static uint8_t       pendingCompartmentId = 0;      // מזהה התא שיש לפתוח (1-5)
static unsigned long stateEnteredAt       = 0;      // חותמת זמן של כניסה למצב הנוכחי
static unsigned long lastPIREventAt       = 0;      // חותמת זמן לדבאונס של PIR

// ---------------------------------------------------------------------------
// נגן MP3 — DFPlayer Mini על UART1
// ---------------------------------------------------------------------------
static HardwareSerial mp3Serial(1); // ערוץ טורי מספר 1, נפרד מחיישן האצבע שעל ערוץ 2
static DFRobotDFPlayerMini dfPlayer;
static bool           mp3Ready = false;

// ---------------------------------------------------------------------------
// הצהרות extern — סמלים המוגדרים בקבצים אחרים, ללא הגדרה כפולה כאן
// ---------------------------------------------------------------------------
extern int  getFingerprintID();   // מוגדר ב-Fingerprint_Function.ino — לא חוסם
extern bool isFingerprintActive;  // מוגדר ב-Fingerprint_Function.ino
extern tft9341touch tft;         // מוגדר ב-TFT_function.ino

// ---------------------------------------------------------------------------
// הצהרות מוקדמות של פונקציות פרטיות
// ---------------------------------------------------------------------------
static void enterState(SystemState newState);
static void handleAlarmRinging();
static void handleActiveUI();
static void handleSnoozed();
static void playAlarmMelody();
static void stopAudio();
static void drawAlarmScreen();
static void drawFingerprintPromptScreen();
// הצהרה מוקדמת על פונקציות ניהול זמן ושרת
bool setupTime();
void setupServer();

// ===========================================================================
//  ממשק ציבורי — PUBLIC API
// ===========================================================================

// ---------------------------------------------------------------------------
// setupHardwareActuators() — אתחול כל החומרה
// קורא פעם אחת מתוך setup() ב-Medimate_Box_Main.ino
// ---------------------------------------------------------------------------
void setupHardwareActuators() {
    // הגדרת פין ה-PIR כקלט
    pinMode(PIR_PIN, INPUT);
    Serial.println("חיישן PIR הוגדר.");

    // הגדרת פיני הסרוו כפלט עם מצב בסיס מוגדר (למניעת floating)
    const uint8_t servoPins[5] = {
        SERVO_PIN_1, SERVO_PIN_2, SERVO_PIN_3, SERVO_PIN_4, SERVO_PIN_5
    };
    uint8_t i = 0;
    while (i < 5) {
        pinMode(servoPins[i], OUTPUT);
        digitalWrite(servoPins[i], LOW);
        i++;
    }
    Serial.println("פיני הסרוו הוגדרו (מצב מוק).");

    // אתחול נגן ה-MP3 על UART1
    mp3Serial.begin(9600, SERIAL_8N1, MP3_RX_PIN, MP3_TX_PIN);
    if (dfPlayer.begin(mp3Serial)) {
        mp3Ready = true;
        dfPlayer.setTimeOut(500);
        dfPlayer.volume(20);
        dfPlayer.EQ(DFPLAYER_EQ_NORMAL);
        Serial.println("נגן MP3 מוכן לפעולה.");
    } else {
        mp3Ready = false;
        Serial.println("אזהרה: נגן MP3 לא נמצא. השמע מנוטרל.");
    }

    Serial.println("אתחול חומרת האקטואטורים הושלם.");
}

// ---------------------------------------------------------------------------
// runStateMachine() — מריץ את מכונת המצבים
// חייב להיקרא בכל איטרציה של loop() ב-Medimate_Box_Main.ino
// לא חוסם — חוזר מיד אם המצב הוא IDLE
// ---------------------------------------------------------------------------
void runStateMachine() {
    if (currentState == STATE_IDLE) {
        // אין פעולה — ממתין ל-triggerAlarm() מהשכבת Firebase

    } else if (currentState == STATE_ALARM_RINGING) {
        handleAlarmRinging();

    } else if (currentState == STATE_ACTIVE_UI) {
        handleActiveUI();

    } else if (currentState == STATE_DISPENSING) {
        // פעולה חד-פעמית: שחרור תא ואז חזרה ל-IDLE
        openCompartment(pendingCompartmentId);
        enterState(STATE_IDLE);

    } else if (currentState == STATE_SNOOZED) {
        handleSnoozed();
    }
}

// ---------------------------------------------------------------------------
// triggerAlarm() — מופעל על ידי שכבת Firebase כשמגיע זמן לנטול תרופה
// מזהה התא לפתיחה: 1 עד 5 (מספר המדור לפי הגדרת Firebase)
// מתעלם אם כבר יש אזעקה פעילה
// ---------------------------------------------------------------------------
void triggerAlarm(uint8_t compartmentId) {
    if (currentState != STATE_IDLE) {
        Serial.println("triggerAlarm() התעלם — אזעקה כבר פעילה.");
        return;
    }
    if (compartmentId < 1 || compartmentId > 5) {
        Serial.printf("שגיאה ב-triggerAlarm(): מזהה תא לא חוקי — %d\n", compartmentId);
        return;
    }

    pendingCompartmentId = compartmentId;
    Serial.printf("אזעקה הופעלה עבור תא מספר %d\n", compartmentId);
    enterState(STATE_ALARM_RINGING);
}

// ---------------------------------------------------------------------------
// openCompartment() — מוק בלבד, ללא PWM אמיתי
// מספר התא: 1 עד 5
// ---------------------------------------------------------------------------
void openCompartment(uint8_t compId) {
    // ── מוק ───────────────────────────────────────────────────────────────
    Serial.printf("[סרוו מוק] פותח תא מספר %d\n", compId);
    // ── TODO — מימוש עתידי ────────────────────────────────────────────────
    // שלב 1: חיבור הסרוו הנכון:    servo[compId-1].attach(servoPins[compId-1]);
    // שלב 2: סיבוב לזווית פתיחה:   servo[compId-1].write(SERVO_OPEN_ANGLE);
    // שלב 3: המתנה לא חוסמת עם millis() עד השלמת התנועה
    // שלב 4: לאחר זיהוי הוצאת כדור ע"י חיישן IR, סגירה חזרה
    // שלב 5: ניתוק הסרוו לחיסכון בזרם: servo[compId-1].detach();
    // ─────────────────────────────────────────────────────────────────────
}


// ===========================================================================
//  מטפלי מצבים — פרטיים לקובץ זה
// ===========================================================================

// ---------------------------------------------------------------------------
// enterState() — מבצע מעבר בין מצבים ומפעיל פעולות כניסה לכל מצב
// ---------------------------------------------------------------------------
static void enterState(SystemState newState) {
    currentState   = newState;
    stateEnteredAt = millis();

    if (newState == STATE_ALARM_RINGING) {
        playAlarmMelody();
        drawAlarmScreen();
        Serial.println("מעבר למצב: ALARM_RINGING");

    } else if (newState == STATE_ACTIVE_UI) {
        // המנגינה ממשיכה לנגן כחיווי נוסף למשתמש
        drawFingerprintPromptScreen();
        Serial.println("מעבר למצב: ACTIVE_UI — חלון טביעת אצבע פתוח");

    } else if (newState == STATE_DISPENSING) {
        stopAudio();
        Serial.println("מעבר למצב: DISPENSING");

    } else if (newState == STATE_SNOOZED) {
        stopAudio();
        Serial.printf("מעבר למצב: SNOOZED — ינגן שוב בעוד %lu שניות\n",
                      SNOOZE_INTERVAL_MS / 1000UL);

    } else if (newState == STATE_IDLE) {
        stopAudio();
        pendingCompartmentId = 0;
        // ביטול מטמון התצוגה כדי לאלץ ציור מחדש של המסך הרגיל
        extern int lastVolume;
        extern int lastAway;
        lastVolume = -1;
        lastAway   = -1;
        Serial.println("מעבר למצב: IDLE");
    }
}

// ---------------------------------------------------------------------------
// handleAlarmRinging() — מנטר PIR בצורה לא חוסמת ועובר מצב בהתאם
// ---------------------------------------------------------------------------
static void handleAlarmRinging() {
    unsigned long now = millis();

    // קריאת PIR עם דבאונס — לא חוסם
    bool pirIsHigh = (digitalRead(PIR_PIN) == HIGH);
    bool debounceOk = (now - lastPIREventAt > PIR_DEBOUNCE_MS);

    if (pirIsHigh && debounceOk) {
        lastPIREventAt = now;
        Serial.println("PIR: זוהתה תנועה — מעיר ממשק משתמש");
        enterState(STATE_ACTIVE_UI);
        return;
    }

    // פסק זמן: אף אחד לא התקרב — מעבר ל-snooze
    if (now - stateEnteredAt >= PIR_WAIT_TIMEOUT_MS) {
        Serial.println("פסק זמן PIR — עובר ל-snooze");
        enterState(STATE_SNOOZED);
    }
}

// ---------------------------------------------------------------------------
// handleActiveUI() — מסקר טביעת אצבע בצורה לא חוסמת
// getFingerprintID() מחזיר -1 מיד אם אין אצבע על החיישן
// ---------------------------------------------------------------------------
static void handleActiveUI() {
    unsigned long now = millis();
    int fpId = getFingerprintID();
    
    // בדיקה 1: האם הונחה טביעת אצבע תקינה?
    if (fpId > 0) {
        Serial.printf("טביעת אצבע אומתה — מזהה משתמש %d — שחרור תא\n", fpId);
        enterState(STATE_DISPENSING);
        return;
    }

    // --- התוספת ההנדסית שלנו: אתחול זמן דינאמי ---
    // כל עוד הקשיש זז מול הקופסה והחיישן מזהה אותו (HIGH),
    // אנחנו מקדמים את חותמת הזמן (stateEnteredAt) לזמן הנוכחי.
    // כך חלון ה-15 שניות לעולם לא ייסגר כל עוד יש נוכחות מול הקופסה.
    if (digitalRead(PIR_PIN) == HIGH) {
        stateEnteredAt = now; 
    }
    // ---------------------------------------------

    // בדיקה 2: פסק זמן - האם עברו 15 שניות מאז התנועה *האחרונה*?
    if (now - stateEnteredAt >= VERIFY_TIMEOUT_MS) {
        Serial.println("פסק זמן טביעת אצבע — עובר ל-snooze");
        enterState(STATE_SNOOZED);
    }

    // חלון האימות פג — מעבר ל-snooze
    if (now - stateEnteredAt >= VERIFY_TIMEOUT_MS) {
        Serial.println("פסק זמן טביעת אצבע — עובר ל-snooze");
        enterState(STATE_SNOOZED);
    }
}

// ---------------------------------------------------------------------------
// handleSnoozed() — ממתין SNOOZE_INTERVAL_MS ואז מחזיר אזעקה
// ---------------------------------------------------------------------------
static void handleSnoozed() {
    if (millis() - stateEnteredAt >= SNOOZE_INTERVAL_MS) {
        Serial.println("ה-snooze הסתיים — מחדש אזעקה");
        enterState(STATE_ALARM_RINGING);
    }
}


// ===========================================================================
//  עזרי שמע — פרטיים לקובץ זה
// ===========================================================================

static void playAlarmMelody() {
    if (!mp3Ready) {
        Serial.println("(שמע לא זמין) — מנגינת אזעקה הייתה מתנגנת כאן");
        return;
    }
    // קובץ 0001.mp3 חייב להיות על כרטיס ה-SD (שיטת שמות DFPlayer)
    dfPlayer.play(1);
    Serial.println("מנגינת אזעקה מתנגנת (רצועה 1)");
}

static void stopAudio() {
    if (mp3Ready) {
        dfPlayer.stop();
    }
}


// ===========================================================================
//  עזרי תצוגה — פרטיים לקובץ זה
//  עוטפים קריאות TFT כדי לשמור על ריכוז הלוגיקה
// ===========================================================================

static void drawAlarmScreen() {
    tft.fillScreen(BLACK);

    tft.setCursor(10, 60);
    tft.setTextColor(YELLOW);
    tft.setTextSize(2);
    tft.println("  הגיע זמן התרופה!");

    tft.setCursor(10, 140);
    tft.setTextColor(WHITE);
    tft.setTextSize(1);
    tft.println("  אנא התקרב לקופסה");
}

static void drawFingerprintPromptScreen() {
    tft.fillScreen(BLACK);

    tft.setCursor(10, 70);
    tft.setTextColor(GREEN);
    tft.setTextSize(2);
    tft.println("  הנח אצבע");
    tft.setCursor(10, 100);
    tft.println("  על החיישן");

    tft.setCursor(10, 150);
    tft.setTextColor(WHITE);
    tft.setTextSize(1);
    tft.println("  החזק יציב לאימות");
}
