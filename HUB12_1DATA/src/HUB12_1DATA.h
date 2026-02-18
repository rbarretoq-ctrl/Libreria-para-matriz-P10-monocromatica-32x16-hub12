#pragma once
#include <Arduino.h>
#include <Print.h>
#if defined(ESP32)
  #include "esp_timer.h"
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
#endif

// Estructura para GPIO rápido (se usa como miembro de clase)
struct HUB12FastPin {
  uint32_t mask = 0;
  bool hiBank = false; // false: GPIO <32, true: GPIO >=32
};

class HUB12_1DATA : public Print {
public:
struct Marquee {
  HUB12_1DATA* d = nullptr;

  int16_t x0=0, y0=0, w=0, h=0;     // ventana
  int16_t x=0;                      // posición actual del texto
  int16_t textW=0;                  // ancho del texto en px
  uint8_t spacing=1;
  uint16_t stepMs=40;
  unsigned long last=0;
  const char* text=nullptr;

  void attach(HUB12_1DATA& дисп) { d = &дисп; }

  void start(int X, int Y, int W, int H, const char* t, uint8_t sp=1, uint16_t ms=40) {
    if (!d || !t) return;
    text = t;
    spacing = sp;
    stepMs = ms;

    // recorte a pantalla
    if (W <= 0 || H <= 0) return;
    if (X < 0) { W += X; X = 0; }
    if (Y < 0) { H += Y; Y = 0; }
    if (X + W > (int)d->width())  W = d->width()  - X;
    if (Y + H > (int)d->height()) H = d->height() - Y;
    if (W <= 0 || H <= 0) return;

    x0=X; y0=Y; w=W; h=H;

    textW = d->textWidth(text, spacing);
    x = x0 + w;         // entra desde la derecha
    last = millis();
  }

  void tick(bool on=true) {
    if (!d || !text) return;
    unsigned long now = millis();
    if ((uint16_t)(now - last) < stepMs) return;
    last = now;

    // limpia SOLO la ventana
    d->fillRect(x0, y0, w, h, false);

    // clip
    d->setClipRect(x0, y0, w, h);

    // centra vertical
    int yText = y0 + (h - (int)d->fontHeight()) / 2;
    d->drawText(x, yText, text, on, spacing);

    d->clearClipRect();
    d->update();

    // mueve
    x--;
    if (x < (x0 - textW)) x = x0 + w;
  }
};

// Marquesina (scroll horizontal dentro de un rectángulo)
void marqueeStart(int x, int y, int w, int h, const char* text, uint8_t spacing = 1, uint16_t stepMs = 40);
void marqueeTick(bool on = true);
void marqueeStop();

uint8_t fontHeight() const;
uint8_t fontWidth() const;

void setClipRect(int x, int y, int w, int h); // w,h en pixeles
void clearClipRect();

int textWidthScaled(const char* s, uint8_t scale = 2, uint8_t spacing = 1);
void drawTextScaledCentered(const char* s, uint8_t scale = 2, bool on = true, uint8_t spacing = 1);

int textWidth(const char* s, uint8_t spacing = 1);
void drawCharScaled(int x, int y, char c, uint8_t scale = 2, bool on = true);
void drawTextScaled(int x, int y, const char* s, uint8_t scale = 2, bool on = true, uint8_t spacing = 1);

void setFont(const uint8_t* font);                 // fuente estilo DMD2
void drawChar(int x, int y, char c, bool on = true);
void drawText(int x, int y, const char* s, bool on = true, uint8_t spacing = 1);

// --- Print/cursor estilo Arduino ---
void setCursor(int16_t x, int16_t y);
int16_t getCursorX() const { return _cx; }
int16_t getCursorY() const { return _cy; }

void setTextWrap(bool enabled) { _wrap = enabled; }
bool getTextWrap() const { return _wrap; }

void setTextSize(uint8_t size);              // 1..N (usa drawTextScaled)
uint8_t getTextSize() const { return _tsize; }

void setTextSpacing(uint8_t spacing) { _tspacing = spacing; }
uint8_t getTextSpacing() const { return _tspacing; }

void setTextColor(bool on) { _tcolor = on; } // monocromo
bool getTextColor() const { return _tcolor; }

uint8_t charWidth(char c) const;

// Print API
size_t write(uint8_t c) override;
using Print::write;

// printf cómodo
int printf(const char* fmt, ...);


void drawCircle(int x0, int y0, int r, bool on = true);
void fillCircle(int x0, int y0, int r, bool on = true);

void drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, bool on = true);
void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, bool on = true);

  struct Pins {
    int oe;
    int a;
    int b;
    int clk;
    int lat;
    int data;
  };

  // serpentine=true: recomendado para arreglos 3x2 típicos (fila 2 al revés)
  HUB12_1DATA(const Pins& pins, uint8_t panelsX, uint8_t panelsY, bool serpentine = true);
  ~HUB12_1DATA();

  bool begin();
  void end();

  uint16_t width()  const { return _w; }
  uint16_t height() const { return _h; }

  // Brillo: tiempo ON por fila (microsegundos). Más = más brillo (y más consumo).
  void setOnTimeUs(uint16_t us) { _onTimeUs = us; }

  // Framebuffer
  void clear();
  void drawPixel(int x, int y, bool on = true);
  bool getPixel(int x, int y) const;

  // Primitivas básicas
  void drawFastHLine(int x, int y, int w, bool on = true);
  void drawFastVLine(int x, int y, int h, bool on = true);
  void drawRect(int x, int y, int w, int h, bool on = true);
  void fillRect(int x, int y, int w, int h, bool on = true);
  void drawLine(int x0, int y0, int x1, int y1, bool on = true);

  // Render
  void update();   // reconstruye scan buffers si hubo cambios
  void refresh();  // llamar MUY seguido (loop)
  // Doble buffer de dibujo (front/back framebuffer)
  void setDoubleBuffer(bool enable = true);
  bool isDoubleBuffer() const;
  void swapBuffers(bool copyFrontToBack = false);

bool startAutoRefresh(uint32_t periodUs = 500); // 500us = buen punto de partida
void stopAutoRefresh();
bool isAutoRefresh() const;

private:

#if defined(ESP32)
  esp_timer_handle_t _arTimer = nullptr;
  bool _arEnabled = false;
  uint32_t _arPeriodUs = 500;
  static void _arTimerCb(void* arg);
  TaskHandle_t _arTask = nullptr;
  static void _arTaskFn(void* arg);
#endif

#if defined(ESP32)
  portMUX_TYPE _scanMux = portMUX_INITIALIZER_UNLOCKED;
#endif

// Estado marquesina
bool _mqEnabled = false;
int16_t _mqX0=0, _mqY0=0, _mqW=0, _mqH=0;
int16_t _mqX=0;
int16_t _mqTextW=0;
uint8_t _mqSpacing=1;
uint16_t _mqStepMs=40;
unsigned long _mqLast=0;
const char* _mqText=nullptr;

bool _clipEnabled = false;
int16_t _clipX0 = 0, _clipY0 = 0, _clipX1 = 0, _clipY1 = 0; // [x0,y0]..[x1,y1] inclusive

const uint8_t* _font = nullptr;

// Estado Print/cursor
int16_t _cx = 0;
int16_t _cy = 0;
bool    _wrap = true;
uint8_t _tsize = 1;
uint8_t _tspacing = 1;
bool    _tcolor = true;

  Pins _p;
  uint8_t _panelsX, _panelsY;
  bool _serp;

  uint16_t _w = 0, _h = 0;

  // Framebuffer (1bpp). En doble buffer: _fb = back/draw, _fbFront = front/show.
  uint8_t* _fb = nullptr;          // buffer de dibujo (back)
  uint8_t* _fbFront = nullptr;     // buffer mostrado (front)
  uint8_t* _fb2 = nullptr;         // segundo buffer (solo si doble buffer)
  uint32_t _fbBytes = 0;
  bool _doubleBuffer = false;


    uint8_t* _scanA = nullptr;
  uint8_t* _scanB = nullptr;
  volatile uint8_t* _scanActive = nullptr;  // doble buffer scan
  uint32_t _bytesPerR = 0;       // 16 * panelsTotal
  uint32_t _scanBytes = 0;

  volatile bool _dirty = true;
  uint16_t _onTimeUs = 800;

  // Fast GPIO pins
  HUB12FastPin _fOE, _fA, _fB, _fCLK, _fLAT, _fDATA;

  inline bool fbGet(int x, int y) const;
  inline void fbSet(int x, int y, bool on);

  // Mapeo interno del panel 32x16 1-DATA (calibrado)
  static int xyToBitIndexLocal(int x, int y);

  // Mapeo global -> panelIdx + coords locales (serpentina opcional)
  void mapGlobalToChain(int gx, int gy, uint8_t& panelIdx, int& lx, int& ly) const;

  void buildScan();

  // Low-level (fast)
  inline void pulseCLK();
  inline void pulseLAT();
  inline void setRow(uint8_t r);
  inline void writeData(bool on);
};
