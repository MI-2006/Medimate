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

bool setupTime() {
  // אתחול מנגנון הזמן מול שרת ה-NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer); 
  
  struct tm timeinfo; 
  Serial.println("Waiting for NTP time sync...");

  const uint8_t maxAttempts = 15; // מקסימום 15 ניסיונות (7.5 שניות סך הכל)
  uint8_t currentAttempt = 0;

  // לולאה מוגבלת בזמן - מונעת תקיעה קריטית של מערכת זמן אמת
  while (!getLocalTime(&timeinfo) && currentAttempt < maxAttempts) {
    Serial.print(".");
    delay(500); // השהייה קצרה בין ניסיונות דגימה 
    currentAttempt++;
  }

  // בדיקת סטטוס היציאה מהלולאה
  if (currentAttempt >= maxAttempts) {
    Serial.println("\n[ERROR] NTP Synchronization Timeout! Running in offline mode.");
    return false; // סנכרון נכשל, אך המערכת תמשיך לעבוד
  }

  Serial.println("\nTime synchronized successfully!");
  return true; // סנכרון הצליח
}

void setupServer() {
  // יצירת אובייקט חכם לטיפול בבקשות POST המכילות JSON
  AsyncCallbackJsonWebHandler* handler = new AsyncCallbackJsonWebHandler("/api/settings", [](AsyncWebServerRequest *request, JsonVariant &json) {
    // המרת המידע הגולמי לאובייקט ג'יסון קריא
    JsonObject jsonObj = json.as<JsonObject>();
    // שליחת תשובה אסינכרונית ללקוח כדי שלא ייתקע
    request->send(200, "application/json", "{\"status\":\"success\", \"message\":\"Data received by MEDIMATE\"}");
  });
  
  server.addHandler(handler);
  server.begin();
}