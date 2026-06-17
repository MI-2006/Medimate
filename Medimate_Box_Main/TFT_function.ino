#include "TFT9341Touch.h"
#include <SPI.h>
// 1. הגדרת משתני זיכרון גלובליים - נשמרים בין קריאה לקריאה
// נתחיל בערכים לא הגיוניים (כמו -1) כדי להבטיח עדכון ראשון מיד באתחול
int lastVolume = -1; 
int lastAway = -1; // משתמשים ב-int במקום bool כדי לאפשר ערך "לא ידוע" התחלתי
// יצירת מופע של המסך
tft9341touch tft = tft9341touch(5, 4);//אלמנט שייצור דופק של חיבור אינטרנטי
void drawHeartbeat(bool success) {
    uint16_t color = success ? GREEN : RED;
    tft.fillCircle(300, 20, 5, color); // עיגול קטן בפינה הימנית העליונה
}
// ==========================================
//                 פונקציות מסך   
// ==========================================
//אתחול המסך
void setupDisplay() {
  tft.begin(); // אתחול הדרייבר
  tft.setRotation(1); // לרוחב
  tft.fillScreen(BLACK);
  
  tft.setCursor(10, 20);
  tft.setTextColor(WHITE);
  tft.setTextSize(2);
  tft.println("Medimate System");
  tft.println("Initializing...");
}

//עדכון המסך
void updateDisplay(int volume, bool isAway) {
  // בדיקה אם חל שינוי בנתונים
  if (volume != lastVolume || (int)isAway != lastAway) {
    
    tft.fillScreen(BLACK); 
    tft.setCursor(20, 20);
    tft.setTextColor(CYAN);
    tft.setTextSize(2);
    tft.println("MediMATE Status");

    tft.setCursor(20, 60);
    if (isAway) {
      tft.setTextColor(RED);
      tft.print("Mode: AWAY");
    } else {
      tft.setTextColor(GREEN);
      tft.print("Mode: ACTIVE");
    }

    tft.setCursor(20, 100);
    tft.setTextColor(WHITE);
    tft.setTextSize(3);
    tft.print("Vol: ");
    
    // התיקון לעמימות: Casting מפורש
    tft.print((int32_t)volume); 
    tft.print("%");

    int barWidth = map(volume, 0, 100, 0, 280);
    tft.drawRect(20, 150, 280, 30, WHITE);
    tft.fillRect(20, 150, barWidth, 30, BLUE);
    

    // עדכון הזיכרון
    lastVolume = volume;
    lastAway = (int)isAway;
  }

  // בדיקה: האם עברו יותר מ-30 שניות מאז הסנכרון האחרון?
  bool isDataFresh = (millis() - lastSuccessfulSync < 30000);
    
  drawHeartbeat(isDataFresh);

  if (!isDataFresh) {
      tft.setTextColor(RED);
      tft.setTextSize(1);
      tft.setCursor(220, 30);
      tft.print("OFFLINE");
  }
}