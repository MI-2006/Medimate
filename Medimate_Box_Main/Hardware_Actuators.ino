// ===========================================================================
//  Hardware_Actuators.ino
//  פרויקט מדימייט — מיכל יזרעאלי
//
//  קובץ זה אחראי על:
//    • מכונת מצבים מערכתית (זרימת האזעקה, PIR, שמירת טביעת אצבע, שחרור תא)
//    • חיישן תנועה HC-SR501 — לא חוסם, מבוסס millis()
//    • שחרור תא תרופה — מתבצע ע"י בקר תאים נפרד (Slave), על גבי אפיק I2C.
//      ראו openCompartment() / pollCompartmentDispense() ואת
//      Medimate_medicineCells_main.ino בצד ה-Slave.
//    • נגן MP3 מסוג DFPlayer Mini — ניגון מנגינה וטיפול ב-snooze
//
//  UART1 (פינים 25/26) שמור לנגן ה-MP3.
//  Serial2 (פינים 16/17) שמור אך ורק לחיישן טביעת האצבע.
//  GPIO 34 (פין קלט בלבד) משמש לחיישן ה-PIR.
//  GPIO 21/22 (I2C ברירת מחדל) שמורים לקישור עם בקר התאים.
//
//  *** קובץ Medimate_I2C_Protocol.h חייב להיות נוכח באותה תיקיית סקיצה. ***
// ===========================================================================

#include <Wire.h>
#include "Medimate_I2C_Protocol.h"

// ---------------------------------------------------------------------------
// הגדרות פינים
// ---------------------------------------------------------------------------
#define PIR_PIN         34    // פלט דיגיטלי של HC-SR501 (פין קלט בלבד)

// הערה הנדסית: ההגדרות SERVO_PIN_1..5 שהיו כאן בעבר (מוק מקומי) הוסרו
// בכוונה. מנועי הסרוו אינם מחוברים פיזית לבקר הזה יותר — הם עברו לבקר
// התאים (Slave) הנפרד, ומתוקשרים עם בקר זה דרך I2C ולא דרך GPIO ישיר.
// השארת ה-#define-ים האלו הייתה משאירה קוד מת שמתעד פינים שגויים.

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
// תת-מכונת מצבים לשחרור תא (DISPENSING) — שכבת ה-I2C מול בקר התאים.
// מופרדת בכוונה ממכונת המצבים המערכתית הראשית: STATE_DISPENSING רק קורא
// ל-handleDispensing() בכל סיבוב לולאה, וזו זו שמחזיקה את הפרטים.
// ---------------------------------------------------------------------------
enum DispenseResult : uint8_t {
    DISPENSE_PENDING,        // openCompartment() עוד לא נקרא במחזור הנוכחי
    DISPENSE_SENDING,        // מנסה לשדר/לשדר מחדש את פקודת ה-I2C
    DISPENSE_AWAITING_SLAVE, // הפקודה אושרה (ACK) — ממתין לסטטוס DONE מה-Slave
    DISPENSE_SUCCESS,        // ה-Slave דיווח CELLS_STATUS_DONE
    DISPENSE_FAILED          // נכשל אחרי מקסימום ניסיונות / שגיאת Slave / פסק זמן כולל
};

static const unsigned long DISPENSE_POLL_INTERVAL_MS  = 250UL;  // קצב בדיקת סטטוס
static const unsigned long DISPENSE_RETRY_BACKOFF_MS  = 200UL;  // המתנה בין ניסיונות שידור
static const uint8_t       DISPENSE_MAX_SEND_RETRIES  = 3;      // מקסימום ניסיונות שידור
static const unsigned long DISPENSE_OVERALL_TIMEOUT_MS = 5000UL; // תקציב כולל: ~250+1500+250+1000=3000 + מרווח

static DispenseResult dispenseState         = DISPENSE_PENDING;
static uint8_t        dispenseCompartmentId = 0;
static uint8_t        dispenseSendAttempts  = 0;
static unsigned long  dispenseCommandSentAt = 0;
static unsigned long  dispenseLastPollAt    = 0;



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
static void handleDispensing();
static bool sendOpenCompartmentCommand(uint8_t compId);
static DispenseResult pollCompartmentDispense();
static void drawAlarmScreen();
static void drawFingerprintPromptScreen();
// הצהרה מוקדמת על פונקציות ניהול זמן ושרת
bool setupTime();
void setupServer();
// הצהרות מוקדמות על פונקציות ה-HAL מ-music_with_MP3.ino
void setupMP3();
void playAlarmMelody();
void stopAudio();

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

    // אתחול אפיק ה-I2C במצב Master, ביעד בקר התאים.
    // קצב 100kHz (Standard Mode) נבחר בכוונה — לא בגלל מגבלת רוחב-פס
    // (התעבורה כאן זעירה), אלא כי תקציב זמן העלייה (Rise-Time) הגדול יותר
    // ב-100kHz מאפשר נגדי Pull-up גדולים יותר, שעמידים יותר בפני ההפרעות
    // האלקטרומגנטיות שכבר תיעדנו (נפילות מתח בעת הפעלת הסרוואים, סעיף 14).
    bool wireOk = Wire.begin(CELLS_I2C_SDA_PIN, CELLS_I2C_SCL_PIN, CELLS_I2C_CLOCK_HZ);
    if (!wireOk) {
        Serial.println("שגיאה קריטית: אתחול אפיק ה-I2C (Master) נכשל.");
    }

    // בדיקת נוכחות בקר התאים על האפיק מיד באתחול — אבחון מוקדם ולא חוסם.
    // נכשל בעדינות (לא תוקע את כל הקופסה עם while(1)) — בדיוק כמו
    // setupFingerprint() ב-Fingerprint_Function.ino. אם בקר התאים מנותק,
    // המערכת תמשיך לפעול ותדווח שגיאה בכל ניסיון openCompartment() עתידי.
    Wire.beginTransmission(CELLS_I2C_ADDRESS);
    uint8_t cellsBusError = Wire.endTransmission();
    if (cellsBusError == 0) {
        Serial.println("בקר התאים אותר בהצלחה על אפיק ה-I2C.");
    } else {
        Serial.printf("אזהרה: בקר התאים לא הגיב על אפיק ה-I2C (קוד שגיאה %d). "
                      "בדוק חיווט SDA/SCL ונגדי Pull-up.\n", cellsBusError);
    }

    // אתחול נגן ה-MP3 — כל הלוגיקה מוגדרת ב-music_with_MP3.ino
    setupMP3();

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
        // לא עוד "ירה ושכח": openCompartment() נקרא פעם אחת בכניסה למצב
        // (ראו enterState() — STATE_DISPENSING). כאן, בכל סיבוב לולאה,
        // רק בודקים האם בקר התאים כבר אישר סיום פיזי — תואם לדרישת
        // "לולאת הבקרה הסגורה" המתועדת בסעיף 11 בספר הפרויקט.
        handleDispensing();

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
// openCompartment() — שולח את פקודת "פתח תא" לבקר התאים דרך I2C.
// מספר התא: 1 עד 5
//
// קריטי: פונקציה זו אינה מבצעת בעצמה את שידור הבתים על האפיק! היא רק
// *מאתחלת* את תת-מכונת המצבים (DISPENSE_SENDING), שהשידור בפועל (כולל
// ניסיונות חוזרים) מתבצע בתוך pollCompartmentDispense(), הנקראת מ-loop()
// בכל סיבוב. כך הפונקציה הזו עצמה היא O(1), לא חוסמת, ובטוחה לקריאה
// מתוך enterState() — בדיוק כמו כל שאר פעולות-הכניסה-למצב באותה פונקציה.
// ---------------------------------------------------------------------------
void openCompartment(uint8_t compId) {
    if (compId < 1 || compId > 5) {
        Serial.printf("openCompartment(): מזהה תא לא חוקי — %d\n", compId);
        dispenseState = DISPENSE_FAILED;
        return;
    }

    dispenseCompartmentId = compId;
    dispenseSendAttempts  = 0;
    dispenseState         = DISPENSE_SENDING;
    dispenseLastPollAt    = 0;          // מאלץ ניסיון שידור מיידי בסיבוב הבא
    dispenseCommandSentAt = millis();   // עוגן לפסק הזמן הכולל
}

// ---------------------------------------------------------------------------
// sendOpenCompartmentCommand() — שידור בפועל של 3 בתי הפקודה על האפיק.
// מחזיר true אם הבתים אושרו (ACK) ע"י בקר התאים, false בכל מקרה אחר.
// "אושר" כאן פירושו רק שהבקר *קיבל* את הפקודה — לא שהתנועה הסתיימה.
// ---------------------------------------------------------------------------
static bool sendOpenCompartmentCommand(uint8_t compId) {
    uint8_t cmd = CELLS_CMD_OPEN_COMPARTMENT;
    uint8_t chk = cellsChecksum(cmd, compId);

    Wire.beginTransmission(CELLS_I2C_ADDRESS);
    Wire.write(cmd);
    Wire.write(compId);
    Wire.write(chk);
    uint8_t i2cResult = Wire.endTransmission();

    switch (i2cResult) {
        case 0:
            Serial.printf("I2C: פקודת פתיחה לתא %d נשלחה ואושרה.\n", compId);
            return true;
        case 1:
            Serial.println("I2C שגיאה: המטען חורג מגודל מאגר השידור הפנימי.");
            return false;
        case 2:
            Serial.println("I2C שגיאה: אין ACK על כתובת ה-Slave — בקר התאים מנותק/לא מגיב.");
            return false;
        case 3:
            Serial.println("I2C שגיאה: אין ACK על בית הנתונים.");
            return false;
        case 5:
            Serial.println("I2C שגיאה: Timeout באפיק — בדוק נגדי Pull-up וחיווט SDA/SCL.");
            return false;
        default:
            Serial.printf("I2C שגיאה לא מזוהה, קוד %d\n", i2cResult);
            return false;
    }
}

// ---------------------------------------------------------------------------
// pollCompartmentDispense() — "מנוע" תת-מכונת המצבים של השחרור. נקרא בכל
// סיבוב loop() (דרך handleDispensing()). אחראי על:
//   1. שידור/שידור-חוזר עם Backoff עד DISPENSE_MAX_SEND_RETRIES ניסיונות.
//   2. לאחר ACK — Polling מוגבל-קצב (לא בכל סיבוב!) של רגיסטר הסטטוס.
//   3. אכיפת פסק זמן כולל שמכסה גם שידור וגם המתנה לתנועה הפיזית.
// לעולם לא חוסם — כל ענף בודק חותמת זמן ומחזיר שליטה מיד אם עדיין מוקדם.
// ---------------------------------------------------------------------------
static DispenseResult pollCompartmentDispense() {
    if (dispenseState == DISPENSE_SUCCESS ||
        dispenseState == DISPENSE_FAILED  ||
        dispenseState == DISPENSE_PENDING) {
        return dispenseState;
    }

    unsigned long now = millis();

    // שעון עצר כולל: שידור + המתנה לתנועה הפיזית + חלון החסד של ה-Slave
    // חייבים להסתיים בתוך התקציב הזה (ראו החישוב בהגדרת הקבוע למעלה).
    if (now - dispenseCommandSentAt >= DISPENSE_OVERALL_TIMEOUT_MS) {
        Serial.println("Dispense: פסק זמן כולל — אין אישור DONE מבקר התאים.");
        dispenseState = DISPENSE_FAILED;
        return dispenseState;
    }

    if (dispenseState == DISPENSE_SENDING) {
        bool backoffElapsed = (now - dispenseLastPollAt >= DISPENSE_RETRY_BACKOFF_MS);
        if (dispenseSendAttempts > 0 && !backoffElapsed) {
            return dispenseState; // עדיין בתוך חלון ה-Backoff בין ניסיונות
        }

        dispenseLastPollAt = now;
        dispenseSendAttempts++;

        bool ok = sendOpenCompartmentCommand(dispenseCompartmentId);
        if (ok) {
            dispenseState = DISPENSE_AWAITING_SLAVE;
        } else if (dispenseSendAttempts >= DISPENSE_MAX_SEND_RETRIES) {
            Serial.printf("I2C: כשל אחרי %d ניסיונות שידור — בקר התאים אינו זמין.\n",
                          DISPENSE_MAX_SEND_RETRIES);
            dispenseState = DISPENSE_FAILED;
        }
        // אחרת: נשארים ב-DISPENSE_SENDING, ננסה שוב אחרי ה-Backoff הבא
        return dispenseState;
    }

    // dispenseState == DISPENSE_AWAITING_SLAVE
    if (now - dispenseLastPollAt < DISPENSE_POLL_INTERVAL_MS) {
        return dispenseState; // מגביל קצב Polling — לא בודקים בכל סיבוב לולאה
    }
    dispenseLastPollAt = now;

    uint8_t bytesReceived = Wire.requestFrom((int)CELLS_I2C_ADDRESS, 1);
    if (bytesReceived != 1 || !Wire.available()) {
        Serial.println("I2C: אין תשובת סטטוס מבקר התאים בסבב הזה — ינסה שוב.");
        return dispenseState;
    }

    uint8_t status = Wire.read();
    if (status == CELLS_STATUS_DONE) {
        Serial.println("בקר התאים דיווח: שחרור הושלם בהצלחה (אומת פיזית).");
        dispenseState = DISPENSE_SUCCESS;
    } else if (status == CELLS_STATUS_ERR_BAD_COMPARTMENT ||
               status == CELLS_STATUS_ERR_BAD_CHECKSUM    ||
               status == CELLS_STATUS_ERR_BUSY) {
        Serial.printf("בקר התאים דיווח שגיאה, קוד 0x%02X\n", status);
        dispenseState = DISPENSE_FAILED;
    }
    // אחרת: OPENING / HOLDING / CLOSING / IDLE — עדיין באמצע תנועה לגיטימית,
    // נמשיך ל-Polling בסבב הבא.

    return dispenseState;
}

// ---------------------------------------------------------------------------
// handleDispensing() — הגשר בין תת-מכונת המצבים הזו למכונת המצבים
// המערכתית הראשית. נקרא מתוך runStateMachine() כל עוד currentState ==
// STATE_DISPENSING, וקובע מתי (ורק מתי) מותר לחזור ל-STATE_IDLE.
// ---------------------------------------------------------------------------
static void handleDispensing() {
    DispenseResult result = pollCompartmentDispense();

    if (result == DISPENSE_SUCCESS) {
        Serial.println("שחרור התרופה אומת פיזית — חוזר למצב IDLE.");
        enterState(STATE_IDLE);
    } else if (result == DISPENSE_FAILED) {
        // TODO הנדסי הבא (לא בסקופ המשימה הנוכחית): לדווח כשל זה ל-Firebase
        // ולמשפחה, לא רק ל-Serial — כשל שחרור תרופה הוא אירוע קריטי שדורש
        // נראות לבני אדם, לא רק שורת לוג.
        Serial.println("שחרור התרופה נכשל — חוזר ל-IDLE.");
        enterState(STATE_IDLE);
    }
    // DISPENSE_SENDING / DISPENSE_AWAITING_SLAVE: נשארים ב-STATE_DISPENSING,
    // ה-Polling/השידור-החוזר ימשיכו בסיבוב הבא של loop().
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
        openCompartment(pendingCompartmentId); // שולח את פקודת ה-I2C פעם אחת בלבד

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
