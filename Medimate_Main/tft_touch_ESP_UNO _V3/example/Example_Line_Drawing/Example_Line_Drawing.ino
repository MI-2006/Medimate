
//=================== Example3_6 ===================
/*************** Avi Hayun 9/07/24 ***************/
#include "TFT9341Touch.h"

//tft9341touch lcd(10, 9, 7, 2); //cs, dc ,tcs, tirq  for Arduino uno
tft9341touch lcd(5, 4, 15, 35);  //cs, dc ,tcs, tirq for ESP32

void setup() {
  lcd.begin();
}

void loop() {
  lcd.fillScreen(WHITE);
  delay(500);
  lcd.drawHLine(0, 120, 319, BLACK);
  delay(500);
  lcd.drawVLine(160, 0, 239, RED);
  delay(500);
  lcd.drawLine(0, 0, 319, 239, BLUE);
  delay(500);
  lcd.drawLine(0, 239, 319, 0, GREEN);
  delay(500);
}
//==================================================