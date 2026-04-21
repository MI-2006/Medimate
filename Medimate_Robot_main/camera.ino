// GND בבקר הראשי -> GND במצלמה 
// TX2 בבקר הראשי -> IO14 במצלמה 
// RX2 בבקר הראשי -> IO15 במצלמה 
// פונקציה שמקבלת את התשובה מהמצלמה/ענן ומחליטה מה לעשות
void processDecision(String response) {
    response.trim(); // מנקה רווחים או ירידות שורה מיותרות מההודעה
    
    if (response == "PERSON_DETECTED") {
        Serial.println(">>> Action: Found someone! Moving toward target...");
        // בהמשך נוסיף כאן פקודות למנועים
        
    } else if (response == "NO_PERSON") {
        Serial.println(">>> Action: Area clear. Scanning next direction...");
        // בהמשך נוסיף כאן פקודות למנועים
    } else {
        Serial.print(">>> Camera says: ");
        Serial.println(response);
    }
}