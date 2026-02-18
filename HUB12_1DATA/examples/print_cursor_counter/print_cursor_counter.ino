#include <Arduino.h>
#include <HUB12_1DATA.h>
#include <fonts/SystemFont5x7.h>

HUB12_1DATA::Pins pins = {36,1,2,41,40,39};
HUB12_1DATA d(pins, 1, 1, false);

int n=0;
unsigned long t0=0;

void setup(){
  d.begin();
  d.setOnTimeUs(600);
  d.setFont(SystemFont5x7);

  d.startAutoRefresh(5000);

  d.clear();
  d.drawRect(0,0,d.width(),d.height(),true);
  d.update();
}

void loop(){
  if(millis()-t0>200){
    t0=millis();
    d.fillRect(2,6,28,8,false);
    d.setCursor(2,6);
    d.printf("CNT=%02d", n++);
    d.update();
  }
  delay(1);
}
