// ===========================================================================
//  Medimate_medicineCells_main.ino  — v2
//  פרויקט מדימייט — מיכל יזרעאלי
//
//  בקר התאים (Slave) — ESP32 נפרד. קבצי הסקיצה:
//    • קובץ זה          — I2C Slave handler + מכונת מצבים של 5 סרוואים
//    • Medimate_IR_Sensors.ino — חיישני מלאי (polling) + חיישן משפך (ISR)
//    • Medimate_I2C_Protocol.h — פרוטוקול משותף עם ה-Master
//
//  שינויים עיקריים ב-v2:
//    • ST_HOLDING מתנהג כעת כמחכה לאירוע, לא כטיימר עיוור.
//      הוא ממתין לאחד מהשניים: (א) g_funnelDropDetected=true מה-ISR,
//      (ב) פסקת-זמן PILL_DROP_TIMEOUT_MS — כדור תקוע.
//    • הסטטוס הנשלח ב-onI2CRequest() הורחב ל-2 בתים:
//        byte 0 = CellsStatus, byte 1 = g_inventoryBitmask
//      ה-Master חייב לבקש CELLS_RESPONSE_BYTES (2) בכל requestFrom().
//    • pillDropSuccess מבחין בין CELLS_STATUS_DONE לבין CELLS_STATUS_PILL_STUCK
//      בשלב ה-ST_CLOSING.
// ===========================================================================

#include <Arduino.h>
#include <Wire.h>
#include <ESP32Servo.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "Medimate_I2C_Protocol.h"

// Medimate_IR_Sensors.ino הצהרות מוקדמות על סמלים מוגדרים ב
extern volatile bool g_funnelDropDetected;
extern volatile uint8_t g_inventoryBitmask;
void setupIRSensors();
void pollInventoryIR();
void clearFunnelFlag();

// פיני הסרוו
static const uint8_t SERVO_PIN[5] = {13, 12, 14, 15, 18};

// קבועי תנועה מכנית
static const int SERVO_MIN_PULSE_US  = 500;
static const int SERVO_MAX_PULSE_US  = 2400;
static const int SERVO_CLOSED_ANGLE  = 0;
static const int SERVO_OPEN_ANGLE    = 90;

static const unsigned long OPEN_TRAVEL_MS      = 250UL;
static const unsigned long CLOSE_TRAVEL_MS     = 250UL;
static const unsigned long DONE_GRACE_MS       = 1000UL;

// PILL_DROP_TIMEOUT_MS — שינוי שמות ומשמעות מ-v1:
// לא זמן פתיחה עיוור —  אלא גבול עליון להמתנה לאירוע ISR.
// ניתן לכדור 3 שניות לעבור את המשפך. אם לא עשה זאת —
// כנראה כדור תקוע מכנית ויש לסגור ולדווח PILL_STUCK.
static const unsigned long PILL_DROP_TIMEOUT_MS = 3000UL;

// מכונת המצבים המקומית
enum CellState : uint8_t {
    ST_IDLE,
    ST_OPENING,
    ST_HOLDING,   // חדש: מחכה לאירוע ISR או פסקת-זמן
    ST_CLOSING,
    ST_DONE
};

static CellState cellState = ST_IDLE;
static uint8_t       activeCompartment = 0;
static unsigned long stateEnteredAt    = 0;
static bool          pillDropSuccess   = false;  // מבחין SUCCESS מ-STUCK ב-ST_CLOSING

// volatile: onI2CRequest() רץ בהקשר I2C-driver task — שונה מ-loop().
// כתיבה/קריאה לבית בודד אטומית ב-ESP32, אך volatile מונע caching ב-register.
static volatile CellsStatus lastStatus = CELLS_STATUS_IDLE;

static Servo cellServo[5];

// תור פקודות I2C
struct CellsCommandMsg {
    uint8_t cmd;
    uint8_t compartment;
    uint8_t checksum;
};
static QueueHandle_t cellsCmdQueue;

// הצהרות מוקדמות של פונקציות פרטיות
static void handleIncomingCommand(const CellsCommandMsg &msg);
static void startOpeningCompartment(uint8_t compId);
static void runCellsStateMachine();
void onI2CReceive(int numBytes);
void onI2CRequest();

//  Callbacks I2C
void onI2CReceive(int numBytes) {
    if (numBytes != 3) {
        while (Wire.available()) { Wire.read(); }
        return;
    }

    CellsCommandMsg msg;
    msg.cmd         = Wire.read();
    msg.compartment = Wire.read();
    msg.checksum    = Wire.read();

    BaseType_t woken = pdFALSE;
    xQueueSend(cellsCmdQueue, &msg, 0);
    // התור מלא  הפקודה נדחית. ה-Master מצויד ב-retry.
}

// ---------------------------------------------------------------------------
// onI2CRequest() — v2: שולח תמיד CELLS_RESPONSE_BYTES (2) בתים.
//
// בית 0: CellsStatus     — מצב מכונת המצבים
// בית 1: InventoryBitmask — ביטמאסק מלאי מ-Medimate_IR_Sensors.ino
//
// חשוב: Wire.write(ptr, len) שולח len בתים מ-FIFO אחד אטומי.
// הפרדה לשתי קריאות Wire.write() גם עובדת, אך גרסת מערך בטוחה יותר
// מפני interleaving אם הדרייבר משתנה בגרסאות עתידיות של arduino-esp32.
// ---------------------------------------------------------------------------
void onI2CRequest() {
    uint8_t response[CELLS_RESPONSE_BYTES];
    response[CELLS_RESP_IDX_STATUS]    = (uint8_t)lastStatus;
    response[CELLS_RESP_IDX_INVENTORY] = g_inventoryBitmask;
    Wire.write(response, CELLS_RESPONSE_BYTES);
}
BATK33365
// ===========================================================================
//  עיבוד פקודות — loop() בלבד
// ===========================================================================
static void handleIncomingCommand(const CellsCommandMsg &msg) {
    if (msg.checksum != cellsChecksum(msg.cmd, msg.compartment)) {
        lastStatus = CELLS_STATUS_ERR_BAD_CHECKSUM;
        Serial.println("I2C: checksum לא תואם — פקודה נדחתה.");
        return;
    }

    if (msg.cmd == CELLS_CMD_PING) {
        return;
    }

    if (msg.cmd != CELLS_CMD_OPEN_COMPARTMENT) {
        Serial.printf("I2C: פקודה לא מוכרת 0x%02X.\n", msg.cmd);
        return;
    }

    if (cellState != ST_IDLE) {
        lastStatus = CELLS_STATUS_ERR_BUSY;
        Serial.println("I2C: עסוק — פקודה נדחתה.");
        return;
    }

    if (msg.compartment < 1 || msg.compartment > 5) {
        lastStatus = CELLS_STATUS_ERR_BAD_COMPARTMENT;
        Serial.printf("I2C: תא לא חוקי %u.\n", msg.compartment);
        return;
    }

    startOpeningCompartment(msg.compartment);
}

// ---------------------------------------------------------------------------
// startOpeningCompartment() — מאפס את הדגל לפני פתיחה (חשוב!)
//
// clearFunnelFlag() חייבת להיקרא *לפני* attach()+write() ולא אחרי.
// אם קוראים אחרי, קיים חלון זמן בין הפתיחה לאיפוס שבו כדור יכול
// לעבור ולהצית ISR — ואיפוס הדגל אחר-כך יגרום לאיבוד האירוע.
// ---------------------------------------------------------------------------
static void startOpeningCompartment(uint8_t compId) {
    clearFunnelFlag();         // חובה לפני הפעלת הסרוו

    activeCompartment = compId;
    pillDropSuccess   = false; // מאפס שאריות ממחזור קודם

    Servo &s = cellServo[compId - 1];
    s.setPeriodHertz(50);
    s.attach(SERVO_PIN[compId - 1], SERVO_MIN_PULSE_US, SERVO_MAX_PULSE_US);
    s.write(SERVO_OPEN_ANGLE);

    cellState      = ST_OPENING;
    stateEnteredAt = millis();
    lastStatus     = CELLS_STATUS_OPENING;

    Serial.printf("תא %u: פתיחה ל-%d°.\n", compId, SERVO_OPEN_ANGLE);
}

// ---------------------------------------------------------------------------
// runCellsStateMachine() — מכונת מצבים לא-חוסמת
//
// ST_HOLDING הוא הלב של השיפור: ממתין לאחד מהשניים:
//   (א) g_funnelDropDetected == true  → כדור עבר ← SUCCESS
//   (ב) מעבר PILL_DROP_TIMEOUT_MS    → כדור תקוע ← STUCK
//
// סדר הבדיקות: קודם הצלחה, אחר-כך פסקת-זמן.
// אם ה-ISR הציב את הדגל ממש בקצה הtimeout — הצלחה תנצח, כראוי.
// ---------------------------------------------------------------------------
static void runCellsStateMachine() {
    unsigned long now = millis();

    switch (cellState) {

        case ST_OPENING:
            if (now - stateEnteredAt >= OPEN_TRAVEL_MS) {
                cellState      = ST_HOLDING;
                stateEnteredAt = now;
                lastStatus     = CELLS_STATUS_HOLDING;
                Serial.printf("תא %u: מחכה לכדור (ISR משפך, timeout=%lums).\n",
                              activeCompartment, PILL_DROP_TIMEOUT_MS);
            }
            break;

        case ST_HOLDING:
            // בדיקה ראשונה: אישור ISR מהמשפך
            if (g_funnelDropDetected) {
                clearFunnelFlag();
                pillDropSuccess = true;
                cellServo[activeCompartment - 1].write(SERVO_CLOSED_ANGLE);
                cellState      = ST_CLOSING;
                stateEnteredAt = now;
                lastStatus     = CELLS_STATUS_CLOSING;
                Serial.printf("תא %u: כדור אותר במשפך (ISR) — סוגר.\n",
                              activeCompartment);
                break;
            }
            // בדיקה שנייה: פסקת-זמן
            if (now - stateEnteredAt >= PILL_DROP_TIMEOUT_MS) {
                pillDropSuccess = false;
                cellServo[activeCompartment - 1].write(SERVO_CLOSED_ANGLE);
                cellState      = ST_CLOSING;
                stateEnteredAt = now;
                lastStatus     = CELLS_STATUS_PILL_STUCK;
                Serial.printf("תא %u: פסקת-זמן — כדור תקוע. סוגר וידווח PILL_STUCK.\n",
                              activeCompartment);
            }
            break;

        case ST_CLOSING:
            if (now - stateEnteredAt >= CLOSE_TRAVEL_MS) {
                cellServo[activeCompartment - 1].detach();  // מניעת jitter + חיסכון בזרם
                cellState      = ST_DONE;
                stateEnteredAt = now;
                // lastStatus כבר הוגדר ב-ST_HOLDING (CLOSING → DONE / PILL_STUCK)
                // מגדירים כאן רק אם הצלחה, כי PILL_STUCK כבר הוגדר לעיל
                if (pillDropSuccess) {
                    lastStatus = CELLS_STATUS_DONE;
                }
                Serial.printf("תא %u: %s, detach() בוצע.\n",
                              activeCompartment,
                              pillDropSuccess ? "שחרור הושלם" : "PILL_STUCK");
            }
            break;

        case ST_DONE:
            if (now - stateEnteredAt >= DONE_GRACE_MS) {
                cellState         = ST_IDLE;
                lastStatus        = CELLS_STATUS_IDLE;
                activeCompartment = 0;
                pillDropSuccess   = false;
            }
            break;

        case ST_IDLE:
        default:
            break;
    }
}

// ===========================================================================
//  setup() / loop()
// ===========================================================================
void setup() {
    Serial.begin(115200);

    // אתחול פיני סרוו
    for (uint8_t i = 0; i < 5; i++) {
        pinMode(SERVO_PIN[i], OUTPUT);
        digitalWrite(SERVO_PIN[i], LOW);
    }

    // הקצאת טיימרי LEDC — ראו הסבר מפורט ב-v1
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);

    // אתחול חיישנים (מ-Medimate_IR_Sensors.ino)
    setupIRSensors();

    // אתחול I2C Slave
    cellsCmdQueue = xQueueCreate(4, sizeof(CellsCommandMsg));
    if (cellsCmdQueue == NULL) {
        Serial.println("קריטי: תור הפקודות נכשל ביצירה.");
    }

    bool wireOk = Wire.begin(CELLS_I2C_ADDRESS, CELLS_I2C_SDA_PIN,
                              CELLS_I2C_SCL_PIN, CELLS_I2C_CLOCK_HZ);
    if (!wireOk) {
        Serial.println("קריטי: I2C Slave init נכשל.");
    }
    Wire.onReceive(onI2CReceive);
    Wire.onRequest(onI2CRequest);

    Serial.printf("בקר תאים מוכן | I2C=0x%02X | SDA=%d SCL=%d\n",
                  CELLS_I2C_ADDRESS, CELLS_I2C_SDA_PIN, CELLS_I2C_SCL_PIN);
}

void loop() {
    // 1. סקירת חיישני מלאי (מגביל קצב פנימית ב-200ms)
    pollInventoryIR();

    // 2. עיבוד פקודות I2C שהגיעו מה-Master
    CellsCommandMsg msg;
    if (xQueueReceive(cellsCmdQueue, &msg, 0) == pdTRUE) {
        handleIncomingCommand(msg);
    }

    // 3. ריצת מכונת המצבים
    runCellsStateMachine();
}
