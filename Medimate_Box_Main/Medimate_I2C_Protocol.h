#ifndef MEDIMATE_I2C_PROTOCOL_H
#define MEDIMATE_I2C_PROTOCOL_H

#include <Arduino.h>

// ===========================================================================
//  Medimate_I2C_Protocol.h  — v2
//  פרויקט מדימייט — מיכל יזרעאלי
//
//  מקור-אמת יחיד (Single Source of Truth) לפרוטוקול ה-I2C בין בקר הקופסה
//  הראשי (Master) לבין בקר התאים (Slave).
//
//  *** קובץ זה חייב להיות מועתק זהה לשתי תיקיות הסקיצה: ***
//      Medimate_Box_Main/Medimate_I2C_Protocol.h           (Master)
//      Medimate_medicineCells_main/Medimate_I2C_Protocol.h (Slave)
//
//  שינויים ב-v2 לעומת v1:
//    • תגובת ה-Slave הורחבה מ-1 בית ל-2 בתים בכל Wire.requestFrom():
//        בית 0: CellsStatus  — מצב מכונת המצבים של מנוע הסרוו
//        בית 1: InventoryBitmask — ביט N (0-מבוסס) = 1 ↔ תא N+1 נמוך במלאי
//    • נוסף קוד סטטוס: CELLS_STATUS_PILL_STUCK — הפסקת-זמן של חיישן המשפך
//    • נוסף CELLS_RESPONSE_BYTES = 2 — כל קריאת requestFrom() חייבת לבקש
//      בדיוק מספר זה. חריגה בכיוון כלשהו תשאיר בתים תקועים בFIFO ה-Slave
//      ותשבש את הקריאה הבאה.
// ===========================================================================

// ---------------------------------------------------------------------------
// כתובת ה-Slave ופרמטרי האפיק
// ---------------------------------------------------------------------------
static const uint8_t  CELLS_I2C_ADDRESS   = 0x08;      // טווח חוקי: 0x08–0x77
static const int      CELLS_I2C_SDA_PIN   = 21;        // ברירת המחדל של ESP32
static const int      CELLS_I2C_SCL_PIN   = 22;        // ברירת המחדל של ESP32
static const uint32_t CELLS_I2C_CLOCK_HZ  = 100000UL;  // Standard-Mode — ראו הרצאה v1
static const uint8_t  CELLS_RESPONSE_BYTES = 2;        // חובה לבקש בדיוק מספר זה

// ---------------------------------------------------------------------------
// Masks לפירוק תגובת ה-Slave (2 בתים)
// ---------------------------------------------------------------------------
// Wire.requestFrom(CELLS_I2C_ADDRESS, CELLS_RESPONSE_BYTES)
// byte 0 = (CellsStatus)Wire.read()
// byte 1 = (uint8_t)Wire.read()  ← InventoryBitmask
static const uint8_t CELLS_RESP_IDX_STATUS    = 0;
static const uint8_t CELLS_RESP_IDX_INVENTORY = 1;

// InventoryBitmask: bit N (0-מבוסס) = 1 ↔ תא N+1 נמוך במלאי
// דוגמה: 0b00000101 = תאים 1 ו-3 נמוכים במלאי
static const uint8_t INVENTORY_BIT_COMP(uint8_t compId) { return (1u << (compId - 1u)); }
// הערה: פונקציה inline ולא מאקרו, כדי לאפשר בדיקת טיפוס ע"י המהדר

// ---------------------------------------------------------------------------
// פקודות: Master -> Slave  (תמיד 3 בתים: cmd | compartment | checksum)
// ---------------------------------------------------------------------------
enum CellsCommand : uint8_t {
    CELLS_CMD_OPEN_COMPARTMENT = 0x01,
    CELLS_CMD_PING             = 0x02   // בדיקת חיות בלבד
};

// ---------------------------------------------------------------------------
// רגיסטר סטטוס: Slave -> Master (תמיד בית 0 מתוך 2 בכל requestFrom)
// ---------------------------------------------------------------------------
enum CellsStatus : uint8_t {
    CELLS_STATUS_IDLE                = 0x00,
    CELLS_STATUS_OPENING             = 0x01,
    CELLS_STATUS_HOLDING             = 0x02,  // ממתין לחיישן המשפך
    CELLS_STATUS_CLOSING             = 0x03,
    CELLS_STATUS_DONE                = 0x04,  // כדור נאסף ואומת ע"י חיישן המשפך
    CELLS_STATUS_PILL_STUCK          = 0x05,  // NEW: פסקת-זמן — הכדור לא נפל בזמן
    CELLS_STATUS_ERR_BAD_COMPARTMENT = 0xFE,
    CELLS_STATUS_ERR_BAD_CHECKSUM    = 0xFD,
    CELLS_STATUS_ERR_BUSY            = 0xFC
};

// ---------------------------------------------------------------------------
// Checksum מינימלי — XOR פקודה ↔ מזהה תא (ראו הנמקה ב-v1)
// ---------------------------------------------------------------------------
inline uint8_t cellsChecksum(uint8_t cmd, uint8_t compartment) {
    return cmd ^ compartment;
}

#endif // MEDIMATE_I2C_PROTOCOL_H
