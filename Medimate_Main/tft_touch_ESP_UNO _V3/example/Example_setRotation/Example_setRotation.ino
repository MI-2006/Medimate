
//=================== Example3_5 ===================
/*************** Avi Hayun 9/07/24 ***************/
#include "TFT9341Touch.h"

//tft9341touch lcd(10, 9, 7, 2); //cs, dc ,tcs, tirq  for Arduino uno 
tft9341touch lcd(5, 4, 15, 35);  //cs, dc ,tcs, tirq for ESP32

void setup() {
  lcd.begin();
}

void loop() {
   for(byte i=0; i<4;i++){
     lcd.fillScreen(BLACK);
     lcd.setRotation(i);
     lcd.print(10, 10, "TFT9341", 3,YELLOW,BLACK);
     delay(500);
     }
}
//==================================================