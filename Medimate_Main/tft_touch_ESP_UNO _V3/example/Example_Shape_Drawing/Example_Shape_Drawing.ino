
//=================== Example3_7 ===================
/*************** Avi Hayun 9/07/24 ***************/
#include "TFT9341Touch.h"
//tft9341touch lcd(10, 9, 7, 2); //cs, dc ,tcs, tirq  for Arduino uno
tft9341touch lcd(5, 4, 15, 35);  //cs, dc ,tcs, tirq for ESP32

void setup() {
  lcd.begin();
}

void loop() {
  lcd.fillScreen(WHITE);
  lcd.fillTriangle(100, 80, 160, 80, 120, 10, YELLOW);
  lcd.fillRect(200, 90, 80, 50, MAGENTA);
  lcd.fillCircle(80, 135, 50, RED);
  lcd.drawRoundRect(10, 190, 278, 40, 10, BLUE);
  delay(2000);
  lcd.drawTriangle(100, 80, 160, 80, 120, 10, RED);
  lcd.drawRect(230, 90, 80, 50, BLUE);
  lcd.drawCircle(110, 135, 50, BLACK);
  for (int i = 0; i <= 258; i++)
    lcd.fillRoundRect(10, 190, 20 + i, 40, 10, BLUE);
     delay(2000);
}
//==================================================