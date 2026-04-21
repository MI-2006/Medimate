
//=================== Example3_1 ===================
/*************** Avi Hayun 9/07/24 ***************/
#include "TFT9341Touch.h"

//tft9341touch lcd(10, 9, 7, 2); //cs, dc ,tcs, tirq  for Arduino uno
tft9341touch lcd(5, 4, 15, 35);  //cs, dc ,tcs, tirq for ESP32

void setup() {
  lcd.begin();
  lcd.setRotation(2);
  lcd.setCursor(100, 40);
  lcd.print("Hello World");

  lcd.print(60, 80, "Hello World", 3,  GREEN, BLACK);
}

void loop() {
}
//==================================================

