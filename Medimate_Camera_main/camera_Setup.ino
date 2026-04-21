// הגדרת מיפוי הפינים עבור מודול ESP32-CAM הסטנדרטי (דגם AI-Thinker)
#define PWDN_GPIO_NUM     32 // פין לכיבוי חומרה (Power Down)
#define RESET_GPIO_NUM    -1 // פין איפוס (לא בשימוש מוגדר, לכן -1)
#define XCLK_GPIO_NUM      0 // פין שעון חיצוני לעדשה
#define SIOD_GPIO_NUM     26 // פין נתונים I2C
#define SIOC_GPIO_NUM     27 // פין שעון I2C
#define Y9_GPIO_NUM       35 // פיני נתוני התמונה (8 ביט נתונים מהעדשה למעבד)
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25 // פין סנכרון אנכי של התמונה
#define HREF_GPIO_NUM     23 // פין סנכרון אופקי של התמונה
#define PCLK_GPIO_NUM     22 // פין שעון הפיקסלים

void initCamera() {
  // יצירת מבנה נתונים (struct) שיכיל את כל הגדרות המצלמה
  camera_config_t config;
  
  // שיוך הפינים שהגדרנו למעלה אל תוך מבנה הנתונים
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;       //(20MHz) תדר העבודה של שעון המצלמה 
  config.pixel_format = PIXFORMAT_JPEG; //JPEG פורמט התמונה שיצא מהעדשה - דחיסת 
  
  // הגדרת איכות ורזולוציה
  config.frame_size = FRAMESIZE_QVGA;   // רזולוציה נמוכה (320*240) לשליחה מהירה מאוד לענן
  config.jpeg_quality = 12;             // איכות התמונה (מספר נמוך = איכות גבוהה יותר, 12 זה סביר)
  config.fb_count = 1;                  // שימוש בחוצץ  אחד בזיכרון לשמירת תמונה

  // הפעלת האתחול הפיזי של המצלמה ושמירת קוד השגיאה (אם יש)
  esp_err_t err = esp_camera_init(&config);
  
  // בדיקה האם האתחול נכשל
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err); // הדפסת השגיאה למחשב
    return; // יציאה מהפונקציה במקרה של תקלה
  }
  Serial.println("Camera Hardware Initialized"); // הדפסת הצלחה
}