#ifndef MEDIMATE_CORE_H
#define MEDIMATE_CORE_H

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>



// חובה להשתמש ב-extern כדי למנוע כפילויות זיכרון כשהקובץ מוכלל מספר פעמים
extern const uint32_t THRESHOLD_WARNING_MINUTES;
extern const uint32_t THRESHOLD_SEVERE_MINUTES;

IntakeStatus calculate_Intake_Status(uint32_t original_taking, uint32_t actual_taking);
void setupTime();
void setupServer();

#endif