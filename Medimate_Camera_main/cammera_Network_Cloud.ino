// פרטי הרשת שלך (יש להחליף בנתונים האמיתיים שלך)
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASS";

// כתובת שרת הפייתון העתידי שלך (נשנה לכתובת האמיתית כשתבני אותו)
String SERVER_URL = "http://192.168.1.100:5000/upload";

void initWiFi() {
  // פקודה להתחברות לראוטר
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  
  //IP לולאה שממתינה עד שהבקר מקבל מהראוטר כתובת 
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); // המתנה של חצי שנייה
    Serial.print("."); // הדפסת נקודה למחשב כדי לראות שמשהו קורה
  }
  
  Serial.println("\nWiFi Connected! IP Address:"); // ירידת שורה והדפסת הצלחה
  Serial.println(WiFi.localIP()); // הדפסת כתובת ה-IP שקיבלנו מהראוטר
}

void takeAndSendPicture() {
  // לכידת פריים  מהעדשה ושמירתו בזיכרון (הפונקציה מחזירה מצביע) 
  camera_fb_t * fb = esp_camera_fb_get();
  
  // בדיקה אם הצילום נכשל (למשל אם יש בעיית חומרה פתאומית)
  if (!fb) {
    Serial.println("Camera capture failed");
    MainController.println("ERROR_CAMERA"); // שליחת הודעת שגיאה לבקר הראשי של הרובוט
    return; // יציאה מהפונקציה
  }
  // התחלת התקשורת מול השרת.
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "image/jpeg");
  // הגדרת פסק זמן ל-3000 מילישניות (3 שניות).
  // הסבר: אם שרת הענן לא עונה תוך 3 שניות, הבקר ינתק את הקשר ויחזיר שגיאה. זה מבטיח שהרובוט שלך לא "יקפא" לעולם בהמתנה אינסופית וימשיך להגיב לסביבה.
  http.setTimeout(3000);   
  // שליחת בקשת ה-POST.
  //(fb->buf),(fb->len) אנחנו שולחים את הזיכרון שבו שמורה התמונה ואת הגודל שלה 
  int httpResponseCode = http.POST(fb->buf, fb->len);

  // בדיקה האם השרת החזיר קוד תקין (גדול מ-0 מסמל שנוצר קשר)
  if (httpResponseCode > 0) {
    //"PERSON_DETECTED" משיכת גוף התשובה (הטקסט) שהשרת החזיר לנו למשל: 
    String cloudResponse = http.getString(); 
    
    Serial.print("Cloud Analysis Result: ");
    Serial.println(cloudResponse);
    
    // *** הנקודה הקריטית: העברת פסק הדין של הענן בחזרה אל הבקר הראשי של הרובוט ***
    MainController.println(cloudResponse); 
    
  } else {
    // אם הייתה בעיה ברשת או בשרת עצמו
    Serial.print("Error sending picture. HTTP code: ");
    Serial.println(httpResponseCode);
    MainController.println("ERROR_CLOUD"); // עדכון הבקר הראשי שהענן לא עונה
  }
  
  // סגירת חיבור האינטרנט ושחרור משאבי הרשת
  http.end(); 
  else {
    Serial.println("WiFi Disconnected!");
    MainController.println("ERROR_WIFI"); // עדכון הבקר הראשי שאין אינטרנט
  }

  // פעולת חובה: החזרת הזיכרון של התמונה למערכת אם נשכח את זה, המצלמה תקרוס בצילום הבא
  esp_camera_fb_return(fb); 
}