#include <Adafruit_Fingerprint.h>

//=========================================================
//לא לשכוח לעשות אפשרות להכניס טביעת אצבע גם מהאפליקציה
// =======================================================

//משתנה שיעזור לנו לדעת אם החיישן מחובר ותקין
// כדי שלא ננסה לקרוא ממנו סתם אם הוא מנותק
bool isFingerprintActive = false;

void setupFingerprint() {
  // קצב התקשורת הסטנדרטי של החיישן
  finger.begin(57600);
  
  // בדיקה אם החיישן מגיב עם הסיסמה המוגדרת מראש (ברירת מחדל של רוב החיישנים)
  if (finger.verifyPassword()) {
    Serial.println("Found fingerprint sensor!");
    isFingerprintActive = true; // נסמן שהחיישן מוכן לעבודה
  } else {
    Serial.println("Did not find fingerprint sensor :(");
    isFingerprintActive = false; // נסמן שיש בעיה
    // הערה חשובה: הורדתי את לולאת ה- while(1) שהייתה כאן! 
    // אם החיישן מתנתק, אנחנו לא רוצים שכל הקופסה תיתקע.
  }
}

// פונקציה שנקראת מתוך ה-loop ורצה ברקע
int getFingerprintID() {
  // אם החיישן לא הוגדר בהצלחה, אין טעם לנסות לקרוא ממנו
  if (!isFingerprintActive) return -1;

  // שלב 1: ניסיון לקחת תמונה של טביעת אצבע מהחיישן
  uint8_t p = finger.getImage();
  
  // אם אין אצבע על החיישן, נחזיר -1 ונצא מיד.
  // זה חשוב כדי לא לעכב את שאר הפעולות ב-loop (כמו עדכון המסך).
  if (p != FINGERPRINT_OK) return -1;

  // שלב 2: תמונה צולמה! עכשיו ממירים אותה לתבנית מתמטית שהחיישן מבין
  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return -1; // כשל בהמרה (אולי האצבע זזה מדי)

  // שלב 3: חיפוש מהיר במסד הנתונים הפנימי של החיישן כדי למצוא התאמה
  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK) {
    // מצאנו התאמה!
    Serial.print("Found ID #"); 
    Serial.println(finger.fingerID);
    return finger.fingerID; // נחזיר את מספר המזהה (ID) של המשתמש
  } else {
    // האצבע הונחה בצורה ברורה, אבל היא לא רשומה במערכת
    Serial.println("Fingerprint not recognized.");
    return -1;
  }
}

// פונקציה לרישום טביעת אצבע חדשה לפי מספר מזהה (ID)
bool enrollNewFingerprint(uint8_t id) {
  int p = -1;
  Serial.print("Waiting for valid finger to enroll as ID #"); 
  Serial.println(id);

  // שלב 1: ממתינים עד שהמשתמש יניח אצבע לסריקה ראשונה
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    if (p == FINGERPRINT_OK) {
      Serial.println("Image taken successfully");
    }
    delay(50); // מניעת עומס על הבקר בזמן ההמתנה
  }

  // ממירים את התמונה לתבנית ראשונה
  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) {
    Serial.println("Error converting image");
    return false;
  }

  Serial.println("Please remove finger...");
  delay(2000); // נותנים למשתמש זמן להרים את האצבע

  // מוודאים שהאצבע באמת הורמה מהחיישן
  p = 0;
  while (p != FINGERPRINT_NOFINGER) {
    p = finger.getImage();
  }

  Serial.println("Place the SAME finger again");
  
  // שלב 2: סריקה שנייה
  p = -1;
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    if (p == FINGERPRINT_OK) {
      Serial.println("Second image taken");
    }
    delay(50);
  }

  // ממירים את התמונה לתבנית שנייה
  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK) {
    Serial.println("Error converting second image");
    return false;
  }

  // שלב 3: מיזוג התבניות ויצירת מודל
  p = finger.createModel();
  if (p != FINGERPRINT_OK) {
    Serial.println("Prints did not match! Try again.");
    return false;
  }

  // שמירת המודל בזיכרון החיישן תחת ה-ID שקיבלנו
  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK) {
    Serial.println("Fingerprint stored successfully!");
    return true;
  } else {
    Serial.println("Error storing fingerprint.");
    return false;
  }
}