// משתנה לשמירת פיני התקשורת מול הבקר הראשי
const int RX_PIN = 14; // הפין דרכו המצלמה קולטת נתונים מהבקר
const int TX_PIN = 15; // הפין דרכו המצלמה משדרת נתונים לבקר

void initUART() {
  // פתיחת ערוץ התקשורת בפורמט 8N1 (8 ביט מידע, ללא פריטי, ביט סיום 1) מול הפינים שהגדרנו
  MainController.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN); 
  Serial.println("UART Interface with Main Controller Initialized");
}

void checkMainControllerCommand() {
  // בדיקה: האם הצטברו נתונים בערוץ הקליטה מהבקר הראשי?
  if (MainController.available() > 0) {
    
    // קריאת תו בודד (בית) אחד מהערוץ
    char command = MainController.read(); 
    
    //(Capture) C  האם התו שהתקבל הוא האות?
    if (command == 'C') {
      Serial.println("Received 'C' from Robot. Taking picture...");
      
      // קריאה לפונקציה (בלשונית הרשת) שלוכדת את התמונה ומעלה לענן
      takeAndSendPicture(); 
    }
  }
}