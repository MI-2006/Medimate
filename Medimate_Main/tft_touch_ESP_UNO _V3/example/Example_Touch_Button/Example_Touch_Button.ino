//=================== Example3_7 ===================
/*************** Avi Hayun 9/07/24 ***************/
#include "TFT9341Touch.h"
#define ledBlue 2
//tft9341touch LcdTouch(10, 9, 7, 2); //cs, dc ,tcs, tirq  uno
tft9341touch lcd(5, 4, 15, 35);  //cs, dc ,tcs, tirq esp32
byte flag = 0;
int x, y;

void setup() {
  lcd.begin();
  lcd.setTouch(3780, 372, 489, 3811);  // Calibration
  lcd.print(60, 20, "Test SWITCH", 3, YELLOW, BLACK);
  lcd.drawCircle(150, 120, 40, WHITE);
  lcd.fillRoundRect(110, 180, 80, 50, 5, CYAN);
  lcd.print(130, 190, "SW", 3, BLACK, CYAN);
  pinMode(ledBlue, OUTPUT);
}

void loop() {
  if (lcd.touched()) {
    delay(30);

    lcd.readTouch();
    x = lcd.xTouch;
    y = lcd.yTouch;
   
    if ((x > 110) && (x < (110 + 80)) && (y > 180) && (y < (180 + 50))) {
      flag = ~flag;  // לשמירת מצב  flag
    }

    if (flag) {
      lcd.fillCircle(150, 120, 39, BLUE);
      digitalWrite(ledBlue, HIGH);
    } else {
      lcd.fillCircle(150, 120, 39, BLACK);
      digitalWrite(ledBlue, LOW);
    }

     while (lcd.touched())
      ;  // המתנה לשחרור לחיצה
    delay(10);
  }
}
