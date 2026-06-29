
const char* WIFI_SSID =  "Hots";        // "Kita-5";
const char* WIFI_PASS =  "0548105650";  // "Xnhbrrfxho";

// כתובת שרת הפייתון העתידי שלך (נשנה לכתובת האמיתית של Render כשתבני אותו)
String SERVER_URL = String SERVER_URL = "https://medimate-backend-j00y.onrender.com/upload";

// הגדרת המפתח הסודי - חייב להיות זהה ב-100% למה שכתבת בפייתון!
// הסבר: זהו ה-Shared Secret. אם תו אחד יהיה שונה (אפילו רווח), השרת יחסום את הבקשה.
const char* ROBOT_API_KEY = "MediMate_Super_Secret_Key_2026";

void initWiFi() {
  // פקודה להתחברות לראוטר
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  
  // לולאה שממתינה עד שהבקר מקבל כתובת IP מהראוטר
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); // המתנה של חצי שנייה
    Serial.print("."); // הדפסת נקודה למחשב כדי לראות שמשהו קורה
  }
  
  Serial.println("\nWiFi Connected! IP Address:"); // ירידת שורה והדפסת הצלחה
  Serial.println(WiFi.localIP()); // הדפסת כתובת ה-IP שקיבלנו מהראוטר
}

void takeAndSendPicture() {
    // 1. לכידת תמונה מהעדשה ושמירתה ב-PSRAM
    // הסבר: הפונקציה מחזירה מצביע (Pointer) לחוצץ התמונה.
    camera_fb_t * fb = esp_camera_fb_get();
    
    // בדיקת תקינות: אם החיישן לא החזיר תמונה, נצא מיד כדי למנוע קריסה.
    if (!fb) {
        Serial.println("Camera capture failed");
        return;
    }

    // 2. אתחול אובייקט ה-HTTP
    HTTPClient http;
    
    // התחלת התקשורת מול ה-URL של Render (חובה HTTPS!)
    http.begin(SERVER_URL);
    
    // 3. הוספת כותרות (Headers) - כאן קורה הקסם
    // כותרת ראשונה: אומרת לשרת מה סוג המידע (תמונת JPEG)
    http.addHeader("Content-Type", "image/jpeg");
    
    // כותרת שנייה: המפתח הסודי שלנו לצורך אימות (Authentication)
    // הסבר: השרת מחפש ב-Headers את המפתח 'X-Robot-Key' ומשווה למה שמוגדר אצלו.
    http.addHeader("X-Robot-Key", ROBOT_API_KEY);

    // 4. הגדרת פסק זמן (Timeout)
    // הסבר: מונע מהרובוט "להיתקע" אם האינטרנט איטי או השרת עמוס. 
    // 5 שניות זה זמן סביר ל-Upload של תמונת QVGA.
    http.setTimeout(5000);

    // 5. שליחת התמונה בפועל בשיטת POST
    Serial.println("Sending encrypted request to Render...");
    int httpResponseCode = http.POST(fb->buf, fb->len);

    // 6. טיפול בתשובת השרת
    if (httpResponseCode > 0) {
        // קבלת הטקסט מהשרת (למשל: "PERSON_DETECTED")
        String cloudResponse = http.getString();
        Serial.print("Server Response: ");
        Serial.println(cloudResponse);
        
        // העברת התשובה לבקר הראשי של הרובוט דרך ה-UART
        MainController.println(cloudResponse);
    } else {
        // טיפול בשגיאות תקשורת (למשל Timeout או חוסר ב-WiFi)
        Serial.print("Error on sending POST: ");
        Serial.println(http.errorToString(httpResponseCode).c_str());
        
        // מודיעים לבקר הראשי שהייתה שגיאה כדי שלא יחכה לנצח
        MainController.println("SERVER_ERROR");
    }

    // 7. סגירת החיבור ושחרור משאבים
    http.end(); 

    // *** חובה: שחרור חוצץ התמונה בחזרה למערכת ***
    // הסבר: ללא שורה זו, הזיכרון יתמלא והבקר יקרוס אחרי פחות מדקה של עבודה.
    esp_camera_fb_return(fb);
}