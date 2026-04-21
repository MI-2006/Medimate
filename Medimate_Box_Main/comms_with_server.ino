void wakeUpServer() {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        // קריאת GET פשוטה ומהירה רק כדי להעיר את Render
        http.begin("https://your-service-name.onrender.com/ping");
        
        // הגדרת Timeout קצר - לא אכפת לנו מהתשובה, רק שהבקשה תגיע ליעד
        http.setTimeout(500); 
        int httpCode = http.GET();
        
        if (httpCode > 0) {
            Serial.println("Server wakeup signal sent.");
        }
        http.end();
    }
}