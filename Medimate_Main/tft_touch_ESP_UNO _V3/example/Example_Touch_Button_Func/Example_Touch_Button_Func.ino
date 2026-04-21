//=================== Example3_10 ===================
/*************** Avi Hayun 9/07/24 ***************/
#include "TFT9341Touch.h"
#define ledBlue 2
//tft9341touch lcd(10, 9, 7, 2); //cs, dc ,tcs, tirq  for Arduino uno
tft9341touch lcd(5, 4, 15, 35);  //cs, dc ,tcs, tirq for ESP32
byte flag = 0;

void setup() {
  lcd.begin();
  lcd.setTouch(3780, 372, 489, 3811);  // Calibration
  lcd.print(60, 20, "Test SWITCH", 3, YELLOW, BLACK);
  lcd.drawCircle(150, 120, 40, WHITE);
  lcd.drawButton(1, 110, 180, 80, 50, 5, CYAN, BLACK, "SW", 3);

  pinMode(ledBlue, OUTPUT);
}

void loop() {
  if (lcd.touched()) {
    delay(30);
    lcd.readTouch();
    int8_t buttonNum = lcd.ButtonTouch(lcd.xTouch, lcd.yTouch);

    if (buttonNum == 1)
      flag = ~flag;

    if (flag) {
      lcd.fillCircle(150, 120, 39, BLUE);
      digitalWrite(ledBlue, HIGH);
    } else {
      lcd.fillCircle(150, 120, 39, BLACK);
      digitalWrite(ledBlue, LOW);
    }
    while (lcd.touched())
      ;
    delay(30);
  }
}
//================================================