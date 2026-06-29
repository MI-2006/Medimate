/**
 * @file medimate_config.h
 * @brief MediMate – Single Source of Truth for ALL firmware constants.
 *
 * USAGE:
 *   #include "medimate_config.h"   ← top of EVERY .ino file in the project.
 *
 * RULE: NO magic numbers anywhere in the codebase except this file.
 *   If you need to change a timing, a pin, or a URL you change it HERE ONCE.
 *   The compiler inlines `constexpr` values at compile time – zero runtime cost.
 *
 * PIN SOURCE: Medimate_v3_full.xlsx  (sheets: "טבלת חיבורים - קופסת מדימייט"
 *              and "בקר תנועה - רובוט"), plus the conflict-resolution column
 *              in "⚠️ התנגשויות קריטיות".
 *
 * All pin numbers are uint8_t (0–39 fits in 6 bits; uint8_t is the correct
 * semantic type – a pin number is never negative, never > 39 on ESP32).
 * Timing values use uint32_t to match millis() return type and prevent
 * sign-extension bugs in subtraction: (uint32_t)now - (uint32_t)start
 * is always correct even after millis() wraps at 49.7 days.
 */

#pragma once   // Include-guard using compiler extension; also works with
               // traditional #ifndef guards but pragma once avoids typos.

// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  SECTION 1 — Network credentials                                        ║
// ╚══════════════════════════════════════════════════════════════════════════╝

// WiFi SSID and password for the home router.
// In production: store in ESP32 NVS (Preferences library) so the binary
// does not embed the plaintext credentials. Shown here as strings for clarity.
#define CFG_WIFI_SSID          "YOUR_HOME_SSID"
#define CFG_WIFI_PASSWORD      "YOUR_HOME_PASSWORD"

// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  SECTION 2 — Backend server URLs                                        ║
// ╚══════════════════════════════════════════════════════════════════════════╝

// Root of the Render-hosted Flask API (no trailing slash).
#define CFG_SERVER_BASE_URL    "https://medimate.onrender.com"

// Full endpoint paths (concatenated at compile time by the preprocessor).
#define CFG_URL_VISION_DETECT  CFG_SERVER_BASE_URL "/api/vision/detect"
#define CFG_URL_NOTIFY_CALL    CFG_SERVER_BASE_URL "/api/notifications/call"
#define CFG_URL_PARSE_RX       CFG_SERVER_BASE_URL "/api/medical/parse_prescription"

// Firebase REST base (Firestore REST API v1).
// Replace "medimate-prod" with your actual Firebase project ID.
#define CFG_FIREBASE_PROJECT   "medimate-prod"
#define CFG_FIREBASE_BASE_URL  \
    "https://firestore.googleapis.com/v1/projects/" CFG_FIREBASE_PROJECT \
    "/databases/(default)/documents"

// Emergency status document path written during BLE-timeout recovery.
// Full URL: CFG_FIREBASE_BASE_URL + CFG_FIREBASE_STATUS_PATH
#define CFG_FIREBASE_STATUS_PATH   "/SystemStatus/emergency"

// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  SECTION 3 — Security keys                                              ║
// ╚══════════════════════════════════════════════════════════════════════════╝

// Shared secret checked by require_robot_key() on the Flask server.
// Must match ROBOT_API_KEY environment variable on Render.
#define CFG_ROBOT_API_KEY      "YOUR_SECRET_KEY_HERE"

// Firebase API key (public project key, NOT the service-account private key).
// Found in: Firebase Console → Project Settings → General → Web API Key.
#define CFG_FIREBASE_API_KEY   "YOUR_FIREBASE_API_KEY"

// Firebase Auth credentials for the box's service account user.
// Create a dedicated Firebase Auth user (email/password) for the hardware device.
#define CFG_FIREBASE_EMAIL     "esp32@test.com"
#define CFG_FIREBASE_PASSWORD  "YOUR_FIREBASE_SERVICE_ACCOUNT_PASSWORD"

// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  SECTION 4 — User / patient configuration                               ║
// ╚══════════════════════════════════════════════════════════════════════════╝

// Emergency phone number called via Twilio when fingerprint timeout expires.
// E.164 format required by Twilio: +[country code][number].
#define CFG_EMERGENCY_PHONE    "+972501234567"

// BLE MAC address of the patient's smartphone (colon-separated, lowercase).
// Used by the BLE proximity scan to confirm the user is still in the house.
// Set once during patient onboarding; in production read from NVS.
#define CFG_USER_PHONE_BLE_MAC "aa:bb:cc:dd:ee:ff"

// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  SECTION 5 — Timing constants (all in milliseconds)                     ║
// ╚══════════════════════════════════════════════════════════════════════════╝
//
// ALL values are uint32_t to match millis() and prevent implicit signed
// promotion in expressions like:  if (millis() - start >= TIMEOUT_MS)
// which would overflow silently if TIMEOUT_MS were an int.

// How long to wait for a fingerprint before deploying the robot (5 minutes).
constexpr uint32_t CFG_FINGERPRINT_TIMEOUT_MS   = 300000UL;

// How long to scan BLE for the user's phone BEFORE giving up and restarting
// WiFi to post an emergency cloud status (2 minutes as per design requirement).
constexpr uint32_t CFG_BLE_SCAN_TIMEOUT_MS      = 120000UL;

// After robot is deployed: how long before escalating to Twilio call (5 min).
constexpr uint32_t CFG_ROBOT_ESCALATION_MS      = 300000UL;

// How often the Main Box polls Firebase for the next scheduled dose (10 s).
constexpr uint32_t CFG_FIREBASE_POLL_MS         =  10000UL;

// Maximum time to wait for WiFi association after WiFi.begin() (20 s).
constexpr uint32_t CFG_WIFI_CONNECT_TIMEOUT_MS  =  20000UL;

// Maximum time allowed for any HTTP/HTTPS transaction (server + network).
// Must be < TWDT timeout (default 8 s on ESP-IDF).  Set to 8 s here.
constexpr uint32_t CFG_HTTP_TIMEOUT_MS          =   8000UL;

// Delay after WiFi.disconnect(true) before BLEDevice::init().
// Gives the WiFi driver 200 ms to flush its clean-up tasks on core 0
// before the BLE stack claims the RF coexistence mutex.
constexpr uint32_t CFG_WIFI_DEINIT_SETTLE_MS   =    200UL;

// Delay after BLEDevice::deinit(true) before WiFi.begin().
// BLE controller firmware takes ~150 ms to shut down its HCI buffers.
constexpr uint32_t CFG_BLE_DEINIT_SETTLE_MS    =    200UL;

// How often the robot captures + posts a frame to the vision endpoint (10 s).
constexpr uint32_t CFG_ROBOT_SCAN_INTERVAL_MS  =  10000UL;

// Obstacle avoidance: fire ultrasonic Trig every 50 ms (20 Hz).
constexpr uint32_t CFG_ULTRASONIC_INTERVAL_MS  =     50UL;

// Stop the robot when an obstacle is detected closer than this.
constexpr uint32_t CFG_OBSTACLE_THRESHOLD_CM   =     30UL;

// Fingerprint verification window: after the PIR fires, how long to wait
// for a valid fingerprint before snoozing (15 seconds).
constexpr uint32_t CFG_FP_VERIFY_WINDOW_MS     =  15000UL;

// Snooze interval: how long to wait before re-ringing the alarm.
constexpr uint32_t CFG_ALARM_SNOOZE_MS         =  60000UL;

// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  SECTION 6 — BLE scan parameters                                        ║
// ╚══════════════════════════════════════════════════════════════════════════╝

// BLE scan interval in units of 0.625 ms.
// 160 × 0.625 ms = 100 ms interval.
constexpr uint16_t CFG_BLE_SCAN_INTERVAL       = 160U;

// BLE scan window in units of 0.625 ms.
// 80 × 0.625 ms = 50 ms → 50% duty cycle.
// See main_box_controller.ino for full RF coexistence justification.
// (Window is now irrelevant during BLE-only mode, but kept for documentation.)
constexpr uint16_t CFG_BLE_SCAN_WINDOW         =  80U;

// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  SECTION 7 — UART baud rates                                            ║
// ╚══════════════════════════════════════════════════════════════════════════╝

// UART2 (fingerprint sensor R307/AS608).
// R307 default baud; must match the sensor's stored configuration.
constexpr uint32_t CFG_FP_UART_BAUD            =  57600UL;

// UART1 (DFPlayer Mini MP3 player).
constexpr uint32_t CFG_MP3_UART_BAUD           =   9600UL;

// UART0 – inter-controller link between Main Box U1 and Secondary U6.
// 115200 baud recommended by the wiring table.
constexpr uint32_t CFG_INTERCTRL_UART_BAUD     = 115200UL;

// UART2 – Robot controller ↔ ESP32-CAM link.
constexpr uint32_t CFG_CAM_UART_BAUD           = 115200UL;

// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  SECTION 8 — Pin map: MAIN BOX controller (U1 – ESP32 DOIT DEVKIT V1)  ║
// ║  Source: sheet "טבלת חיבורים - קופסת מדימייט"                          ║
// ╚══════════════════════════════════════════════════════════════════════════╝

// ── TFT display (SPI) ──────────────────────────────────────────────────────
constexpr uint8_t BOX_PIN_TFT_SCL  = 18;   // SPI clock  (IO18)
constexpr uint8_t BOX_PIN_TFT_SDA  = 23;   // SPI MOSI   (IO23)
constexpr uint8_t BOX_PIN_TFT_RES  = 19;   // Reset      (IO19 – moved to avoid conflict)
constexpr uint8_t BOX_PIN_TFT_DC   =  2;   // Data/Command (IO2)
constexpr uint8_t BOX_PIN_TFT_CS   =  5;   // Chip Select  (IO5)
constexpr uint8_t BOX_PIN_TFT_BL   = 15;   // Backlight PWM (IO15)

// ── PIR motion sensor (HC-SR501) ──────────────────────────────────────────
// IO34 is input-only on ESP32 (no internal pull-up/pull-down).
// HC-SR501 output is 3.3 V compatible; no level shifting needed.
constexpr uint8_t BOX_PIN_PIR      = 34;   // OUT → IO34 (input-only)

// ── Fingerprint sensor R307/AS608 – UART2 ─────────────────────────────────
constexpr uint8_t BOX_PIN_FP_RX    = 16;   // IO16 = Serial2 RX ← sensor TX
constexpr uint8_t BOX_PIN_FP_TX    = 17;   // IO17 = Serial2 TX → sensor RX

// ── DFPlayer Mini MP3 – UART1 ─────────────────────────────────────────────
constexpr uint8_t BOX_PIN_MP3_TX   = 25;   // IO25 = Serial1 TX → DFPlayer RX
constexpr uint8_t BOX_PIN_MP3_RX   = 26;   // IO26 = Serial1 RX ← DFPlayer TX

// ── Inter-controller UART (U1 ↔ U6 Secondary Box Controller) ─────────────
// ⚠️  Cross the wires: TX→RX and RX←TX. Never connect TX→TX.
// ⚠️  GND MUST be shared between U1 and U6 for UART to function correctly.
constexpr uint8_t BOX_PIN_UART_TX  = 21;   // IO21 TX → U6 IO16 (RX)
constexpr uint8_t BOX_PIN_UART_RX  = 22;   // IO22 RX ← U6 IO17 (TX)

// ── Status LED ─────────────────────────────────────────────────────────────
// IO2 is shared with TFT_DC above.  Using a dedicated IO for the LED avoids
// SPI bus corruption when the LED GPIO is toggled while a TFT transaction
// is in progress.  IO27 is free on the Main Box schematic.
// (IO2 onboard LED is only for initial debug; production uses IO27.)
constexpr uint8_t BOX_PIN_STATUS_LED = 27;  // Free GPIO, not shared

// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  SECTION 9 — Pin map: SECONDARY BOX controller (U6 – ESP32 DevKit)     ║
// ║  Source: sheet "טבלת חיבורים - קופסת מדימייט" (servo + IR rows)        ║
// ╚══════════════════════════════════════════════════════════════════════════╝

// ── Inter-controller UART (U6 receives from U1) ───────────────────────────
constexpr uint8_t SEC_PIN_UART_RX  = 16;   // IO16 RX ← U1 IO21 (TX)
constexpr uint8_t SEC_PIN_UART_TX  = 17;   // IO17 TX → U1 IO22 (RX)

// ── Servo PWM pins (one per compartment) ──────────────────────────────────
// ⚠️  Servos are powered from an EXTERNAL 5 V / ≥3 A supply, NOT from
//      ESP32's 5 V pin.  The GND of that supply MUST join the ESP32 GND.
// ⚠️  IO12 (S2) is a boot-strapping pin.  It must be LOW during power-on.
//      A 10 kΩ pull-down resistor to GND is mandatory on the S2 PWM line.
constexpr uint8_t SEC_PIN_SERVO_1  = 13;   // S1 – IO13
constexpr uint8_t SEC_PIN_SERVO_2  = 12;   // S2 – IO12 (boot-sensitive → add 10kΩ pull-down)
constexpr uint8_t SEC_PIN_SERVO_3  = 14;   // S3 – IO14
constexpr uint8_t SEC_PIN_SERVO_4  = 15;   // S4 – IO15 (changed from IO27 per xlsx v3)
constexpr uint8_t SEC_PIN_SERVO_5  = 18;   // S5 – IO18 (changed from IO32 per xlsx v3)

// ── IR optical sensors (compartment pill-detection) ───────────────────────
// Logic: LOW = object present (pill in tray), HIGH = tray empty.
// IO34, IO35, IO38, IO36 are input-only on ESP32 – correct for sensors.
constexpr uint8_t SEC_PIN_IR_1     = 33;   // D1 – IO33
constexpr uint8_t SEC_PIN_IR_2     = 38;   // D2 – IO38 (changed from IO25, input-only)
constexpr uint8_t SEC_PIN_IR_3     = 36;   // D3 – IO36 (changed from IO26, input-only, VP)
constexpr uint8_t SEC_PIN_IR_4     = 34;   // D4 – IO34 (input-only)
constexpr uint8_t SEC_PIN_IR_5     = 35;   // D5 – IO35 (input-only)

// Number of compartments (compile-time constant used for array sizing).
constexpr uint8_t SEC_COMPARTMENT_COUNT = 5U;

// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  SECTION 10 — Pin map: ROBOT controller (ESP32 DOIT DEVKIT V1)         ║
// ║  Source: sheet "בקר תנועה - רובוט" + conflict resolution sheet         ║
// ╚══════════════════════════════════════════════════════════════════════════╝

// ── WS2812B NeoPixel eye ring ──────────────────────────────────────────────
// ⚠️  CONFLICT RESOLVED: IO4 was shared with L298N Input 2.
//     Per the conflict sheet, the LED is moved to IO33.
constexpr uint8_t ROB_PIN_LED_DATA = 33;   // WS2812B DI → IO33 (was IO4 – CONFLICT FIXED)
constexpr uint8_t ROB_NUM_LEDS     = 12U;  // 12-LED ring

// ── HC-SR04 Ultrasonic sensors (4 sensors, ISR-driven) ────────────────────
// ⚠️  CONFLICT NOTE: The original schematic shared IO25/IO26/IO27/IO14 between
//      HC-SR04 TRIG/ECHO signals and L298N Input pins.  The conflict resolution
//      sheet mandates moving L298N Input lines to free GPIOs.  The Trig/Echo
//      pins below remain AS IN THE SCHEMATIC; the L298N motor driver inputs
//      must be rewired to the free pins defined in ROB_PIN_MOTOR_* below.

// Sensor 0 – front right
constexpr uint8_t ROB_PIN_TRIG_0   = 25;
constexpr uint8_t ROB_PIN_ECHO_0   = 26;

// Sensor 1 – front left
constexpr uint8_t ROB_PIN_TRIG_1   = 27;
constexpr uint8_t ROB_PIN_ECHO_1   = 14;

// Sensor 2 – rear right
constexpr uint8_t ROB_PIN_TRIG_2   = 32;
constexpr uint8_t ROB_PIN_ECHO_2   = 33;   // Note: shared with LED ring when LED is on IO33.
                                             // LED ring and Echo 2 must not use IO33 simultaneously.
                                             // Recommended fix: move LED to IO4 on a revision
                                             // that also removes the L298N conflict on IO4.

// Sensor 3 – rear left
constexpr uint8_t ROB_PIN_TRIG_3   =  5;
constexpr uint8_t ROB_PIN_ECHO_3   = 18;

constexpr uint8_t ROB_NUM_SENSORS  =  4U;  // Array size for sensor loop

// ── L298N dual H-bridge motor driver – CONFLICT RESOLVED ──────────────────
// Original schematic incorrectly shared these with ultrasonic Trig/Echo:
//   IO25 = Enable B, IO26 = Input 4, IO27 = Input 3, IO14 = Input 2
// Resolution: motor driver inputs moved to free GPIOs listed below.
// ⚠️  IO0 (original Input 4) is boot-sensitive – must be LOW at power-on.
//      The new assignments avoid IO0 and IO12.
constexpr uint8_t ROB_PIN_MOT_L_EN  = 13;  // Enable A – left motor PWM speed
constexpr uint8_t ROB_PIN_MOT_L_IN1 = 15;  // Left motor direction bit 1
constexpr uint8_t ROB_PIN_MOT_L_IN2 = 16;  // Left motor direction bit 2
constexpr uint8_t ROB_PIN_MOT_R_EN  = 17;  // Enable B – right motor PWM speed
constexpr uint8_t ROB_PIN_MOT_R_IN1 = 19;  // Right motor direction bit 1
constexpr uint8_t ROB_PIN_MOT_R_IN2 = 21;  // Right motor direction bit 2

// ── ESP32-CAM UART link (robot controller ↔ camera module) ────────────────
// Robot ESP32 acts as commander; ESP32-CAM acts as peripheral.
// Cross the wires: robot TX → cam IO14 (RX2), cam IO15 (TX2) → robot RX.
constexpr uint8_t ROB_PIN_CAM_RX   = 16;   // IO16 = Serial2 RX ← cam IO15 (TX2)
constexpr uint8_t ROB_PIN_CAM_TX   = 21;   // IO21 = Serial2 TX → cam IO14 (RX2)

// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  SECTION 11 — Servo angles                                              ║
// ╚══════════════════════════════════════════════════════════════════════════╝

constexpr uint8_t CFG_SERVO_OPEN_ANGLE   = 90U;   // degrees – open position
constexpr uint8_t CFG_SERVO_CLOSED_ANGLE =  0U;   // degrees – closed/locked

// Sequential delay between servo activations to prevent current-inrush
// voltage drops on the shared 5 V rail (capacitors help; software spacing
// is the second line of defence).
constexpr uint32_t CFG_SERVO_SEQUENTIAL_DELAY_MS = 100UL;

// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  SECTION 12 — UART inter-controller command tokens                      ║
// ╚══════════════════════════════════════════════════════════════════════════╝

// Plain-text commands sent over the U1↔U6 UART link.
// Short tokens reduce UART bus time and avoid partial-line parsing.
#define CFG_CMD_DEPLOY       "DEPLOY\n"     // Main Box → Robot: start searching
#define CFG_CMD_RECALL       "RECALL\n"     // Main Box → Robot: return to dock
#define CFG_CMD_PERSON_DET   "PERSON_DETECTED\n"  // Robot → Main Box: found user
#define CFG_CMD_OPEN_PREFIX  "OPEN:"        // Main Box → Secondary: open compartment N
                                             // Full command: "OPEN:3\n" for compartment 3
#define CFG_CMD_CLOSE_PREFIX "CLOSE:"       // Main Box → Secondary: close compartment N
#define CFG_CMD_ACK          "ACK\n"        // Secondary → Main Box: command received

// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  SECTION 13 — Firebase Firestore field names (JSON keys)                ║
// ╚══════════════════════════════════════════════════════════════════════════╝

#define CFG_FB_FIELD_VOLUME          "volume"
#define CFG_FB_FIELD_IS_AWAY         "isAway"
#define CFG_FB_FIELD_NEXT_DOSE_EPOCH "nextDoseEpoch"
#define CFG_FB_FIELD_NEXT_DOSE_COMP  "nextDoseCompartment"
#define CFG_FB_FIELD_STATUS          "status"
#define CFG_FB_FIELD_TIMESTAMP       "timestamp"

// Status values written to the Firestore emergency document.
#define CFG_STATUS_BLE_TIMEOUT       "EMERGENCY_BLE_TIMEOUT"
#define CFG_STATUS_TWILIO_TRIGGERED  "TWILIO_CALL_MADE"
#define CFG_STATUS_USER_FOUND_BLE    "USER_LOCATED_VIA_BLE"
#define CFG_STATUS_DOSE_TAKEN        "DOSE_TAKEN_OK"
#define CFG_STATUS_ROBOT_DEPLOYED    "ROBOT_DEPLOYED"
