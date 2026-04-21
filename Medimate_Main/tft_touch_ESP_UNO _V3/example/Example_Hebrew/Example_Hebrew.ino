
//=================== Example3_3 ===================
/*************** Avi Hayun 9/07/24 ***************/
#include "TFT9341Touch.h"

//tft9341touch lcd(10, 9, 7, 2); //cs, dc ,tcs, tirq  for Arduino uno
tft9341touch lcd(5, 4, 15, 35);  //cs, dc ,tcs, tirq for ESP32
void setup() {
  lcd.begin();
  lcd.setTextSize(3);
  lcd.setCursor(30, 40);
  lcd.printHebrew("ברוכים הבאים");
  lcd.setCursor(60, 100);
  lcd.print("ESP32");
  lcd.printHebrew("למשפחת");
}

void loop() {
}
//==================================================