// ==================================================
//          מנוע התקשורת (Firebase Engine)
// ==================================================

//  הפונקציה 1: אימות משתמש מול Firebase 
void authenticateUser() {
  if (WiFi.status() != WL_CONNECTED) return;
  
  HTTPClient http;
  String authUrl = "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=" + apiKey;
  http.begin(authUrl);
  http.addHeader("Content-Type", "application/json");

  // שימוש ב-ArduinoJson כדי לבנות את בקשת ההתחברות בצורה בטוחה
  JsonDocument requestDoc;
  requestDoc["email"] = USER_EMAIL;
  requestDoc["password"] = USER_PASSWORD;
  requestDoc["returnSecureToken"] = true;
  
  String requestJson;
  serializeJson(requestDoc, requestJson);

  int httpCode = http.POST(requestJson);

  if (httpCode == 200) {
    String payload = http.getString();
    // פירוק התשובה כדי לשלוף את הטוקן
    JsonDocument responseDoc;
    deserializeJson(responseDoc, payload);
    idToken = responseDoc["idToken"].as<String>();
    Serial.println("Authentication Successful! Token received.");
  } else {
    Serial.println("Auth Failed! HTTP Code: " + String(httpCode));
    Serial.println(http.getString());
  }
  http.end();
}
// פונקציה 2: עדכון סטטוס קופסה (PATCH)
void updateBoxStatus(String id, int currentVolume, bool isUserAway) {
  if (WiFi.status() != WL_CONNECTED || idToken == "") { Serial.println("No WiFi or Not Authenticated!"); return; }
  HTTPClient http;
  
  String url = "https://firestore.googleapis.com/v1/projects/" + projectId + 
               "/databases/(default)/documents/Boxes/" + id + 
               "?updateMask.fieldPaths=volume&updateMask.fieldPaths=isAway&key=" + apiKey;

  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  // הוספת הטוקן כדי לפתוח את הנעילה של מסד הנתונים!
  http.addHeader("Authorization", "Bearer " + idToken);

  // בניית ה-JSON באמצעות הספריה (מונע פרגמנטציה!)
  JsonDocument doc;
  doc["fields"]["volume"]["integerValue"] = currentVolume;
  doc["fields"]["isAway"]["booleanValue"] = isUserAway;
  
  String json;
  serializeJson(doc, json);

  //פונקקציה שמטפלת בבקשה של הפאטצ' כיוון שהוא לא ממש בא בטבעי
  int httpCode = http.sendRequest("PATCH", json);
    
    if (httpCode > 0) {
      Serial.println("Update Success! HTTP Code: " + String(httpCode)); 
    } else {
      Serial.println("Error on update: " + http.errorToString(httpCode));
    }
    http.end();
  }
// פונקציה 3: קבלת קופסה משויכת למשתמש (GET)
String getUserLinkedBox(String uId) {
  if (WiFi.status() != WL_CONNECTED || idToken == "") return "";
  
  HTTPClient http;
  String url = "https://firestore.googleapis.com/v1/projects/" + projectId + 
               "/databases/(default)/documents/Users/" + uId + 
               "?key=" + apiKey;
               
  http.begin(url);
  http.addHeader("Authorization", "Bearer " + idToken);
  int httpCode = http.GET();
  String linkedBox = "";
  
  if (httpCode == 200) {
    String payload = http.getString();
    
    // שימוש ב-ArduinoJson לחילוץ נתונים (במקום IndexOf)
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      String fullPath = doc["fields"]["linkedBoxId"]["stringValue"].as<String>();
      // ניקוי הנתיב (אם גוגל מחזיר /Boxes/...)
      int lastSlash = fullPath.lastIndexOf("/");
      if (lastSlash != -1) {
        linkedBox = fullPath.substring(lastSlash + 1);
      } else {
        linkedBox = fullPath;
      }
    }
  } else {
    Serial.println("Error getting user: " + http.errorToString(httpCode));
  }
  
  http.end();
  return linkedBox;
}
// פונקציה 4: קריאת נתוני הקופסה והדפסתם לסריאל (GET)
void getAndPrintBoxData(String id) {
  if (WiFi.status() != WL_CONNECTED || idToken == "") { Serial.println("No WiFi or Not Authenticated!"); return; }
  
  HTTPClient http;
  String url = "https://firestore.googleapis.com/v1/projects/" + projectId + 
               "/databases/(default)/documents/Boxes/" + id + 
               "?key=" + apiKey;

  http.begin(url);
  http.addHeader("Authorization", "Bearer " + idToken);
  int httpCode = http.GET(); 
  
  if (httpCode == 200) {
    String payload = http.getString(); 
    lastSuccessfulSync = millis(); // שומרים את הזמן הנוכחי
    // חילוץ חכם עם ArduinoJson
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    
   if(!error) {
    JsonObject fields;
    Serial.println("Parsing JSON...");
    if (doc.containsKey("documents")) {
        fields = doc["documents"][0]["fields"].as<JsonObject>();
      } else {
        fields = doc["fields"].as<JsonObject>();
      }
       if (!fields.isNull()) {
        int volumeVal = fields["volume"]["integerValue"].as<int>();
        bool isAway = fields["isAway"]["booleanValue"].as<bool>();

        Serial.printf("--> EXTRACTED VOLUME: %d, IS AWAY: %s\n", volumeVal, isAway ? "YES" : "NO");
        Serial.println("JSON Parsed successfully.");
        // קריאה אחת בלבד לשכבת התצוגה
        updateDisplay(volumeVal, isAway);
      }
    }
  }
  http.end();
}
            //  Serial.print("--> EXTRACTED VOLUME: ");
            //  Serial.println(volumeVal);
            //  Serial.print("--> IS AWAY: ");
            //  Serial.println(isAway ? "YES" : "NO");

             // בתוך ה-if(!error) אחרי חילוץ הנתונים:
//             Serial.print("--> EXTRACTED VOLUME: ");
//             Serial.println(volumeVal);

// .end();
// }