
//=================== Example3_4 ===================
/*************** Avi Hayun 9/07/24 ***************/
#include "TFT9341Touch.h"

//tft9341touch lcd(10, 9, 7, 2); //cs, dc ,tcs, tirq for Arduino uno
tft9341touch lcd(5, 4, 15, 35);  //cs, dc ,tcs, tirq for ESP32

void setup() {
  lcd.begin();

  lcd.print(80, 50, "Cuounter", 3, YELLOW, BLACK);
  lcd.setTextColor(RED,BLACK);
  lcd.setTextSize(5);
}

void loop() {

  for (byte i = 0; i <= 255; i++) {
    lcd.setCursor(130, 100);
    lcd.print(String(i) + "  ");

    delay(500);
  }
}
//==================================================