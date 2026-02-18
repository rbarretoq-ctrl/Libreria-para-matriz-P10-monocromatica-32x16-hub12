#include <Arduino.h>
#include <HUB12_1DATA.h>
#include <fonts/SystemFont5x7.h>
HUB12_1DATA::Pins pins = {36,1,2,41,40,39};
HUB12_1DATA d(pins, 1, 1, false);

void setup() {
  d.begin();
  d.setFont(SystemFont5x7);
  d.setOnTimeUs(600);  
  d.startAutoRefresh(5000);
  d.clear();
  d.drawChar(2,1,'S',true);
  d.drawRect(8, 8, 6, 6,true);
  d.drawCircle(24, 8, 6,true);
  d.update();
}

void loop() {
  
}
