#include <Arduino.h>
#include <HUB12_1DATA.h>
#include <fonts/SystemFont5x7.h>

HUB12_1DATA::Pins pins = {36,1,2,41,40,39};
HUB12_1DATA d(pins, 1, 1, false);

const char* msg1 = " 1111 ";
const char* msg2 = " 2222 ";

int x1, x2;
int w1, w2;

unsigned long lastStep = 0;
const uint16_t stepMs = 60;

void setup() {
  d.begin();
  d.setOnTimeUs(600);
  d.setFont(SystemFont5x7);

  d.startAutoRefresh(5000);

  w1 = d.textWidth(msg1, 1);
  w2 = d.textWidth(msg2, 1);

  x1 = 15;
  x2 = 31;
}

void loop() {
  if (millis() - lastStep >= stepMs) {
    lastStep = millis();

    d.clear();

    // Ventana izquierda (0..15)
    d.setClipRect(0, 0, 16, 16);
    int y = (16 - d.fontHeight()) / 2;
    d.drawText(x1, y, msg1, true, 1);
    d.clearClipRect();

    // Ventana derecha (16..31)
    d.setClipRect(16, 0, 16, 16);
    d.drawText(x2, y, msg2, true, 1);
    d.clearClipRect();

    d.update();

    x1--; x2--;
    if (x1 < -w1) x1 = 15;
    if (x2 < (16 - w2)) x2 = 31;
  }
  delay(1);
}
