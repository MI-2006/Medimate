// ===========================================================================
//  Hardware_Actuators.ino  — v2
//  פרויקט מדימייט — מיכל יזרעאלי
//
//  שינויים עיקריים ב-v2:
//    • pollCompartmentDispense() מבקש CELLS_RESPONSE_BYTES (2) בתים בכל פעם.
//      לא 1 בית! בקשת 1 בית כשה-Slave שולח 2 תשאיר בית תקוע ב-FIFO ותשבש
//      את הקריאה הבאה — באג שקט קלאסי שהמהדר לא יתפוס.
//    • DISPENSE_PILL_STUCK נוסף ל-DispenseResult — מאפשר למכונת המצבים
//      המערכתית להגיב שונה לכשל פיזי (כדור תקוע) לעומת כשל תקשורת (I2C).
//    • pollInventoryBitmask() — ממשק ציבורי חדש, נקרא מ-loop() בנפרד.
//      אסור לקרוא לו בזמן DISPENSING כי שני Thread-ים יחסמו את I2C bus.
//    • handleDispensing() מבדיל: SUCCESS → IDLE, STUCK → ALARM_RINGING
//      (המשתמש עדיין צריך לקחת תרופה), FAILED → IDLE עם לוג שגיאה.
//    • תוקן באג v1: handleActiveUI() הכיל בדיקת פסקת-זמן כפולה עם אותו
//      גוף — הבדיקה השנייה הייתה קוד מת שלעולם לא הגיב לאחרי הראשונה.
//
//  UART1 (25/26)  ← נגן MP3
//  Serial2 (16/17) ← חיישן טביעת אצבע
//  GPIO 34         ← PIR
//  GPIO 21/22 (I2C Master) ← בקר התאים
// ===========================================================================

#include <Wire.h>
#include "Medimate_I2C_Protocol.h"

// ---------------------------------------------------------------------------
// פינים
// ---------------------------------------------------------------------------
#define PIR_PIN 34

// ---------------------------------------------------------------------------
// קבועי זמן
// ---------------------------------------------------------------------------
static const unsigned long SNOOZE_INTERVAL_MS           = 120000UL;
static const unsigned long PIR_WAIT_TIMEOUT_MS          =  30000UL;
static const unsigned long VERIFY_TIMEOUT_MS            =  15000UL;
static const unsigned long PIR_DEBOUNCE_MS              =   2000UL;
static const unsigned long INVENTORY_POLL_INTERVAL_MS   =  30000UL; // בדיקת מלאי כל 30 שניות

// ---------------------------------------------------------------------------
// מכונת מצבים מערכתית
// ---------------------------------------------------------------------------
enum SystemState : uint8_t {
    SYS_STATE_IDLE          = 0,
    SYS_STATE_ALARM_RINGING = 1,
    SYS_STATE_ACTIVE_UI     = 2,
    SYS_STATE_DISPENSING    = 3,
    SYS_STATE_SNOOZED       = 4
};
static SystemState currentState = SYS_STATE_IDLE;
static uint8_t pendingCompartmentId = 0;
static unsigned long stateEnteredAt       = 0;
static unsigned long lastPIREventAt       = 0;

// ---------------------------------------------------------------------------
// תת-מכונת מצבים: DISPENSING
//
// DispenseResult DISPENSE_PILL_STUCK — חדש ב-v2.
// מבחין בין שני סוגי כישלון:
//   DISPENSE_FAILED     — כשל תקשורת (I2C לא ענה, checksum שגוי וכו')
//   DISPENSE_PILL_STUCK — הסרוו פעל תקין, אך הכדור לא עבר את המשפך.
//                         המשמעות: המשתמש עדיין לא נטל תרופה ← חזרה לאזעקה.
// ---------------------------------------------------------------------------


static const unsigned long DISPENSE_POLL_INTERVAL_MS   = 250UL;
static const unsigned long DISPENSE_RETRY_BACKOFF_MS   = 200UL;
static const uint8_t       DISPENSE_MAX_SEND_RETRIES   = 3;
// עדכון מ-v1: הפסקת-זמן הכוללת גדלה ל-6000ms כי ה-Slave מחכה עד 3000ms
// לאירוע ISR לפני שמדווח PILL_STUCK. 3000 (Slave) + 250*N (polling RTT) + מרווח.
static const unsigned long DISPENSE_OVERALL_TIMEOUT_MS = 6000UL;

static DispenseResult dispenseState         = DISPENSE_PENDING;
static uint8_t        dispenseCompartmentId = 0;
static uint8_t        dispenseSendAttempts  = 0;
static unsigned long  dispenseCommandSentAt = 0;
static unsigned long  dispenseLastPollAt    = 0;

// ---------------------------------------------------------------------------
// ניטור מלאי — Master צד
// ---------------------------------------------------------------------------
static unsigned long lastInventoryPollAt = 0;
static uint8_t       lastKnownInventory  = 0xFF; // 0xFF = לא ידוע (יאלץ הדפסה בפעם הראשונה)

// ---------------------------------------------------------------------------
// Externs
// ---------------------------------------------------------------------------
extern int  getFingerprintID();
extern bool isFingerprintActive;
extern tft9341touch tft;

// ---------------------------------------------------------------------------
// הצהרות מוקדמות
// ---------------------------------------------------------------------------
static void enterState(SystemState newState);
static void handleAlarmRinging();
static void handleActiveUI();
static void handleSnoozed();
static void handleDispensing();
static bool sendOpenCompartmentCommand(uint8_t compId);
DispenseResult pollCompartmentDispense();
static void drawAlarmScreen();
static void drawFingerprintPromptScreen();
bool setupTime();
void setupServer();
void setupMP3();
void playAlarmMelody();
void stopAudio();

// ===========================================================================
//  ממשק ציבורי
// ===========================================================================

void setupHardwareActuators() {
    pinMode(PIR_PIN, INPUT);
    Serial.println("PIR הוגדר.");

    bool wireOk = Wire.begin(CELLS_I2C_SDA_PIN, CELLS_I2C_SCL_PIN, CELLS_I2C_CLOCK_HZ);
    if (!wireOk) {
        Serial.println("קריטי: I2C Master init נכשל.");
    }

    Wire.beginTransmission(CELLS_I2C_ADDRESS);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
        Serial.println("בקר תאים אותר על ה-I2C.");
    } else {
        Serial.printf("אזהרה: בקר תאים לא ענה (err=%d). בדוק SDA/SCL ונגדי Pull-up.\n", err);
    }

    setupMP3();
    Serial.println("אתחול אקטואטורים הושלם.");
}

void runStateMachine() {
    switch (currentState) {
        case STATE_IDLE:
            break;
        case STATE_ALARM_RINGING:
            handleAlarmRinging();
            break;
        case SYS_STATE_ACTIVE_UI:
            handleActiveUI();
            break;
        case STATE_DISPENSING:
            handleDispensing();
            break;
        case SYS_STATE_SNOOZED:
            handleSnoozed();
            break;
    }
}

void triggerAlarm(uint8_t compartmentId) {
    if (currentState != STATE_IDLE) {
        Serial.println("triggerAlarm(): מתעלם — אזעקה כבר פעילה.");
        return;
    }
    if (compartmentId < 1 || compartmentId > 5) {
        Serial.printf("triggerAlarm(): תא לא חוקי %d.\n", compartmentId);
        return;
    }
    pendingCompartmentId = compartmentId;
    Serial.printf("אזעקה: תא %d.\n", compartmentId);
    enterState(STATE_ALARM_RINGING);
}

// ---------------------------------------------------------------------------
// openCompartment() — מאתחל את תת-מכונת ה-DISPENSING; לא שולח I2C בעצמה.
// ---------------------------------------------------------------------------
void openCompartment(uint8_t compId) {
    if (compId < 1 || compId > 5) {
        Serial.printf("openCompartment(): תא לא חוקי %d.\n", compId);
        dispenseState = DISPENSE_FAILED;
        return;
    }
    dispenseCompartmentId = compId;
    dispenseSendAttempts  = 0;
    dispenseState         = DISPENSE_SENDING;
    dispenseLastPollAt    = 0;
    dispenseCommandSentAt = millis();
}

// ---------------------------------------------------------------------------
// pollInventoryBitmask() — ממשק ציבורי, נקרא מ-loop() ב-Medimate_Box_Main.ino
//
// כלל ברזל: אסור לקרוא לפונקציה זו כאשר currentState == STATE_DISPENSING.
// הסיבה: בזמן שחרור תא, pollCompartmentDispense() מבצע Wire.requestFrom()
// כל 250ms. קריאה מקבילה ל-Wire.requestFrom() ממקור נוסף (לולאה חיצונית)
// היא reentrancy על מנהל ה-I2C — אינה בטוחה ב-single-threaded loop().
// הגנה על-ידי בדיקת מצב, לא על-ידי mutex — כי כל שתי הקריאות רצות
// מאותו thread (loop()) ולכן לא יכולות להתרחש בו-זמנית ממש.
// המשמעות: בזמן dispensing המלאי לא יתעדכן ל-30 השניות הבאות — זה מקובל.
// ---------------------------------------------------------------------------
void pollInventoryBitmask() {
    if (currentState == STATE_DISPENSING) {
        return; // I2C עסוק — דחה לסיבוב הבא
    }

    unsigned long now = millis();
    if (now - lastInventoryPollAt < INVENTORY_POLL_INTERVAL_MS) {
        return;
    }
    lastInventoryPollAt = now;

    uint8_t bytesReceived = Wire.requestFrom((int)CELLS_I2C_ADDRESS,
                                              (int)CELLS_RESPONSE_BYTES);
    if (bytesReceived != CELLS_RESPONSE_BYTES) {
        Serial.println("Inventory poll: בקר תאים לא ענה.");
        return;
    }

    /* byte 0 = CellsStatus */ Wire.read(); // לא רלוונטי בהקשר זה — חובה לקרוא
    uint8_t inventory = Wire.read();        // byte 1 = InventoryBitmask

    // לא מדפיסים אם אין שינוי — למניעת הצפת Serial
    if (inventory == lastKnownInventory) {
        return;
    }

    // מצא שינוי: הדפס רק ביטים שעברו מ-0 ל-1 (תאים שהתרוקנו)
    uint8_t newlyEmpty = (uint8_t)(inventory & ~lastKnownInventory);
    for (uint8_t i = 0; i < 5; i++) {
        if (newlyEmpty & (1u << i)) {
            Serial.printf(
                "⚠️  מלאי נמוך: תא %d ריק — יש לחדש מיד!\n"
                "    TODO: שלח עדכון ל-Firebase ולאפליקציה המשפחתית.\n",
                i + 1
            );
        }
    }

    // תאים שחזרו למלאי (אם המשתמש מילא מחדש)
    uint8_t newlyRestocked = (uint8_t)(~inventory & lastKnownInventory);
    for (uint8_t i = 0; i < 5; i++) {
        if (newlyRestocked & (1u << i)) {
            Serial.printf("✅  מלאי שוחזר: תא %d.\n", i + 1);
        }
    }

    lastKnownInventory = inventory;
}

// ===========================================================================
//  תת-מכונת ה-DISPENSING
// ===========================================================================

static bool sendOpenCompartmentCommand(uint8_t compId) {
    uint8_t cmd = CELLS_CMD_OPEN_COMPARTMENT;
    uint8_t chk = cellsChecksum(cmd, compId);

    Wire.beginTransmission(CELLS_I2C_ADDRESS);
    Wire.write(cmd);
    Wire.write(compId);
    Wire.write(chk);
    uint8_t result = Wire.endTransmission();

    switch (result) {
        case 0:
            Serial.printf("I2C: פקודה לתא %d אושרה (ACK).\n", compId);
            return true;
        case 2:
            Serial.println("I2C: אין ACK על כתובת Slave — מנותק?");
            return false;
        case 3:
            Serial.println("I2C: אין ACK על נתונים.");
            return false;
        case 5:
            Serial.println("I2C: Timeout — בדוק Pull-up.");
            return false;
        default:
            Serial.printf("I2C: שגיאה %d.\n", result);
            return false;
    }
}

// ---------------------------------------------------------------------------
// pollCompartmentDispense() — v2
//
// שינוי קריטי: Wire.requestFrom() מבקש CELLS_RESPONSE_BYTES (2) בתים.
// קריאת 2 בתים גם מתאימה לשינוי ה-onI2CRequest() ב-Slave.
// CellsStatus נמצא ב-byte 0; inventory ב-byte 1 (נקרא ונזרק — לא
// רלוונטי בהקשר זה, אבל *חייב* להיקרא כדי לרוקן את ה-Wire buffer).
//
// CELLS_STATUS_PILL_STUCK → DISPENSE_PILL_STUCK (לא DISPENSE_FAILED):
// חשוב מאוד! PILL_STUCK = הסרוו פעל, הכדור לא ירד. זהו כשל מכני,
// לא כשל תקשורת. handleDispensing() יגיב לזה אחרת (חזרה לאזעקה,
// לא פשוט IDLE) — כי המשתמש עדיין לא נטל תרופה.
// ---------------------------------------------------------------------------
DispenseResult pollCompartmentDispense() {
    if (dispenseState == DISPENSE_SUCCESS ||
        dispenseState == DISPENSE_FAILED  ||
        dispenseState == DISPENSE_PILL_STUCK ||
        dispenseState == DISPENSE_PENDING) {
        return dispenseState;
    }

    unsigned long now = millis();

    if (now - dispenseCommandSentAt >= DISPENSE_OVERALL_TIMEOUT_MS) {
        Serial.println("Dispense: פסקת-זמן כוללת — אין תגובה מבקר התאים.");
        dispenseState = DISPENSE_FAILED;
        return dispenseState;
    }

    if (dispenseState == DISPENSE_SENDING) {
        bool backoffElapsed = (now - dispenseLastPollAt >= DISPENSE_RETRY_BACKOFF_MS);
        if (dispenseSendAttempts > 0 && !backoffElapsed) {
            return dispenseState;
        }

        dispenseLastPollAt = now;
        dispenseSendAttempts++;

        if (sendOpenCompartmentCommand(dispenseCompartmentId)) {
            dispenseState = DISPENSE_AWAITING_SLAVE;
        } else if (dispenseSendAttempts >= DISPENSE_MAX_SEND_RETRIES) {
            Serial.printf("I2C: כשל אחרי %d ניסיונות.\n", DISPENSE_MAX_SEND_RETRIES);
            dispenseState = DISPENSE_FAILED;
        }
        return dispenseState;
    }

    // DISPENSE_AWAITING_SLAVE — polling מוגבל-קצב
    if (now - dispenseLastPollAt < DISPENSE_POLL_INTERVAL_MS) {
        return dispenseState;
    }
    dispenseLastPollAt = now;

    uint8_t received = Wire.requestFrom((int)CELLS_I2C_ADDRESS,
                                         (int)CELLS_RESPONSE_BYTES);
    if (received != CELLS_RESPONSE_BYTES || !Wire.available()) {
        Serial.println("Dispense poll: לא ענה — ינסה בסיבוב הבא.");
        return dispenseState;
    }

    uint8_t status    = Wire.read();   // byte 0 = CellsStatus
    /* inventory */    Wire.read();   // byte 1 = InventoryBitmask (נזרק כאן)

    switch (status) {
        case CELLS_STATUS_DONE:
            Serial.println("Dispense: כדור אושר במשפך ← SUCCESS.");
            dispenseState = DISPENSE_SUCCESS;
            break;
        case CELLS_STATUS_PILL_STUCK:
            Serial.println("Dispense: כדור תקוע ← PILL_STUCK (חוזר לאזעקה).");
            dispenseState = DISPENSE_PILL_STUCK;
            break;
        case CELLS_STATUS_ERR_BAD_COMPARTMENT:
        case CELLS_STATUS_ERR_BAD_CHECKSUM:
        case CELLS_STATUS_ERR_BUSY:
            Serial.printf("Dispense: שגיאת Slave 0x%02X ← FAILED.\n", status);
            dispenseState = DISPENSE_FAILED;
            break;
        default:
            // OPENING / HOLDING / CLOSING — תנועה לגיטימית, ממשיכים לחכות
            break;
    }

    return dispenseState;
}

// ---------------------------------------------------------------------------
// handleDispensing() — מבדיל בין שלושה מסלולי יציאה מ-STATE_DISPENSING:
//
//   SUCCESS     → STATE_IDLE         (תרופה נלקחה, הכל תקין)
//   PILL_STUCK  → STATE_ALARM_RINGING (תרופה לא נלקחה! המשתמש עדיין צריך)
//   FAILED      → STATE_IDLE         (כשל תקשורת — לא ניתן לדעת מה קרה)
//                 + TODO: דיווח Firebase
// ---------------------------------------------------------------------------
static void handleDispensing() {
    DispenseResult result = pollCompartmentDispense();

    if (result == DISPENSE_SUCCESS) {
        Serial.println("שחרור הושלם ואומת — חוזר ל-IDLE.");
        enterState(STATE_IDLE);

    } else if (result == DISPENSE_PILL_STUCK) {
        Serial.println("כדור תקוע! המשתמש טרם נטל תרופה — מחדש אזעקה.");
        // TODO: דיווח אירוע PILL_STUCK ל-Firebase — זהו מצב חריג שמשפחת
        // המטופל צריכה לדעת עליו. יש להוסיף לאחר חיבור שכבת ה-Firebase.
        enterState(STATE_ALARM_RINGING);

    } else if (result == DISPENSE_FAILED) {
        Serial.println("כשל תקשורת בשחרור תא — חוזר ל-IDLE.");
        // TODO: דיווח כשל ל-Firebase
        enterState(STATE_IDLE);
    }
    // SENDING / AWAITING: נשאר ב-STATE_DISPENSING, יסקר בסיבוב הבא
}

// ===========================================================================
//  מטפלי מצבים
// ===========================================================================

static void enterState(SystemState newState) {
    currentState   = newState;
    stateEnteredAt = millis();

    if (newState == STATE_ALARM_RINGING) {
        playAlarmMelody();
        drawAlarmScreen();
        Serial.println("→ STATE_ALARM_RINGING");

    } else if (newState == SYS_STATE_ACTIVE_UI) {
        drawFingerprintPromptScreen();
        Serial.println("→ SYS_STATE_ACTIVE_UI");

    } else if (newState == STATE_DISPENSING) {
        stopAudio();
        Serial.println("→ STATE_DISPENSING");
        openCompartment(pendingCompartmentId);

    } else if (newState == SYS_STATE_SNOOZED) {
        stopAudio();
        Serial.printf("→ SYS_STATE_SNOOZED (יחזור בעוד %lus)\n",
                      SNOOZE_INTERVAL_MS / 1000UL);

    } else if (newState == STATE_IDLE) {
        stopAudio();
        pendingCompartmentId = 0;
        dispenseState        = DISPENSE_PENDING;
        extern int lastVolume;
        extern int lastAway;
        lastVolume = -1;
        lastAway   = -1;
        Serial.println("→ STATE_IDLE");
    }
}

static void handleAlarmRinging() {
    unsigned long now = millis();
    bool pirHigh   = (digitalRead(PIR_PIN) == HIGH);
    bool debounced = (now - lastPIREventAt > PIR_DEBOUNCE_MS);

    if (pirHigh && debounced) {
        lastPIREventAt = now;
        Serial.println("PIR: תנועה — פותח ממשק");
        enterState(SYS_STATE_ACTIVE_UI);
        return;
    }
    if (now - stateEnteredAt >= PIR_WAIT_TIMEOUT_MS) {
        Serial.println("PIR: פסקת-זמן → snooze");
        enterState(SYS_STATE_SNOOZED);
    }
}

// תיקון באג v1: הבדיקה הכפולה הוסרה — הבדיקה השנייה הייתה קוד מת לחלוטין.
// לאחר enterState() הראשון, currentState כבר אינו SYS_STATE_ACTIVE_UI, כך
// שהענף השני לעולם לא הורץ. הותרת קוד מת אמנם לא שוברת — אבל היא
// מעידה על חוסר תשומת-לב שמוביל לבאגים חמורים יותר בהמשך.
static void handleActiveUI() {
    unsigned long now  = millis();
    int           fpId = getFingerprintID();

    if (fpId > 0) {
        Serial.printf("טביעת אצבע #%d — שחרור תא\n", fpId);
        enterState(STATE_DISPENSING);
        return;
    }

    if (digitalRead(PIR_PIN) == HIGH) {
        stateEnteredAt = now; // אתחול דינאמי — ראו v1
    }

    if (now - stateEnteredAt >= VERIFY_TIMEOUT_MS) {
        Serial.println("פסקת-זמן טביעת אצבע → snooze");
        enterState(SYS_STATE_SNOOZED);
    }
}

static void handleSnoozed() {
    if (millis() - stateEnteredAt >= SNOOZE_INTERVAL_MS) {
        Serial.println("Snooze: מחדש אזעקה");
        enterState(STATE_ALARM_RINGING);
    }
}

// ===========================================================================
//  עזרי תצוגה
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
