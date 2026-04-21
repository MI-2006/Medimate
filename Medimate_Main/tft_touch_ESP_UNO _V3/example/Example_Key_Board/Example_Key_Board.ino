//=================== Example3_10 ===================
/*************** Avi Hayun 9/07/24 ***************/
#include "TFT9341Touch.h"

//tft9341touch lcd(10, 9, 7, 2); //cs, dc ,tcs, tirq  for Arduino uno
tft9341touch lcd(5, 4, 15, 35);  //cs, dc ,tcs, tirq for ESP32

void setup() {
  lcd.begin();
  lcd.setTouch(3780, 372, 489, 3811);  // Calibration
  lcd.print(60, 20, "Key Board", 3, YELLOW, BLACK);

  lcd.drawButton(1, 5, 60, 70, 50, 5, GREEN, BLACK, "1", 3);
  lcd.drawButton(2, 90, 60, 70, 50, 5, CYAN, BLACK, "2", 3);
  lcd.drawButton(3, 170, 60, 70, 50, 5, RED, BLACK, "3", 3);
  lcd.drawButton(4, 250, 60, 70, 50, 5, BLUE, BLACK, "4", 3);

  lcd.print(10, 150, "text box:", 3, WHITE, BLACK);
}

void loop() {
  if (lcd.touched()) {
    delay(10);
    lcd.readTouch();
    int8_t buttonNum = lcd.ButtonTouch(lcd.xTouch, lcd.yTouch);

    delay(10);
    if (buttonNum != -1) {
      lcd.setCursor(10, 180);
      lcd.print(buttonNum);
      while (lcd.touched())
        ;
    }
  }
}
//================================================