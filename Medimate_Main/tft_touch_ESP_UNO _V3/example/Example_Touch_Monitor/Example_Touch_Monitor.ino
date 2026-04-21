
//=================== Example3_7 ===================
/*************** Avi Hayun 9/07/24 ***************/
#include "TFT9341Touch.h"

//tft9341touch lcd(10, 9, 7, 2); //cs, dc ,tcs, tirq  for Arduino uno
tft9341touch lcd(5, 4, 15, 35);  //cs, dc ,tcs, tirq for ESP32
int x, y;
String str;
void setup() {
  lcd.begin();
  lcd.setTouch(3780, 372, 489, 3811);  //Calibration
  lcd.print(80, 20, "Test Touch", 3, WHITE, BLACK);
}

void loop() {

  if (lcd.touched()) {
    lcd.readTouch();
    x = lcd.xTouch;
    y = lcd.yTouch;
    if (x != -1 && y != -1) {
      str = "x=" + String(x) + "  y=" + String(y) + "  ";
      lcd.print(50, 100, str, 3, YELLOW, BLACK);
    }
  }
}
//==================================================