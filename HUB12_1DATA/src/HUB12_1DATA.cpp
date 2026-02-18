#include "HUB12_1DATA.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(ESP32)
  #include "esp_timer.h"
#endif

// Acceso directo a registros GPIO (ESP32 / ESP32-S3)
#include "soc/gpio_struct.h"
#include "soc/gpio_reg.h"

// ---------- Fast GPIO helpers ----------
static inline HUB12FastPin makeFastPin(int pin) {
  HUB12FastPin fp;
  if (pin < 32) { fp.hiBank = false; fp.mask = (1u << pin); }
  else          { fp.hiBank = true;  fp.mask = (1u << (pin - 32)); }
  return fp;
}

static inline void fastHigh(const HUB12FastPin& p) {
  if (!p.hiBank) GPIO.out_w1ts = p.mask;
  else           GPIO.out1_w1ts.val = p.mask;
}

static inline void fastLow(const HUB12FastPin& p) {
  if (!p.hiBank) GPIO.out_w1tc = p.mask;
  else           GPIO.out1_w1tc.val = p.mask;
}

// ---------- Ctor/Dtor ----------
HUB12_1DATA::HUB12_1DATA(const Pins& pins, uint8_t panelsX, uint8_t panelsY, bool serpentine)
: _p(pins), _panelsX(panelsX), _panelsY(panelsY), _serp(serpentine) {
  _w = 32 * _panelsX;
  _h = 16 * _panelsY;

  _fbBytes = (_w * _h + 7) / 8;

  uint16_t panelsTotal = _panelsX * _panelsY;
  _bytesPerR = 16 * panelsTotal;     // 128 bits = 16 bytes por panel, por r
  _scanBytes = 4 * _bytesPerR;       // r=0..3
}

HUB12_1DATA::~HUB12_1DATA() {
  end();
}

bool HUB12_1DATA::begin() {
  if (_fb || _scanA || _scanB) end();

  _fb = (uint8_t*)calloc(_fbBytes, 1);
  if (_doubleBuffer) _fb2 = (uint8_t*)calloc(_fbBytes, 1);
  _scanA = (uint8_t*)calloc(_scanBytes, 1);
  _scanB = (uint8_t*)calloc(_scanBytes, 1);
  _scanActive = _scanA;
  if (!_fb || (_doubleBuffer && !_fb2) || !_scanA || !_scanB) {
    end();
    return false;
  }

    // Framebuffer pointers
  if (_doubleBuffer) {
    _fbFront = _fb;   // front muestra lo actual
    _fb = _fb2;       // back para dibujar
  } else {
    _fbFront = _fb;
  }

pinMode(_p.oe, OUTPUT);
  pinMode(_p.a, OUTPUT);
  pinMode(_p.b, OUTPUT);
  pinMode(_p.clk, OUTPUT);
  pinMode(_p.lat, OUTPUT);
  pinMode(_p.data, OUTPUT);

  // Fast pin masks
  _fOE   = makeFastPin(_p.oe);
  _fA    = makeFastPin(_p.a);
  _fB    = makeFastPin(_p.b);
  _fCLK  = makeFastPin(_p.clk);
  _fLAT  = makeFastPin(_p.lat);
  _fDATA = makeFastPin(_p.data);

  fastLow(_fCLK);
  fastLow(_fLAT);

  // OE activo HIGH en tu panel: apagado al inicio
  fastLow(_fOE);

  clear();
  buildScan();
  return true;
}

void HUB12_1DATA::end() {
  // liberar framebuffers sin doble free
  uint8_t* p1 = _fb;
  uint8_t* p2 = _fb2;
  uint8_t* p3 = _fbFront;

  if (p2 == p1) p2 = nullptr;
  if (p3 == p1 || p3 == p2) p3 = nullptr;

  if (p1) free(p1);
  if (p2) free(p2);
  if (p3) free(p3);

  _fb = nullptr;
  _fb2 = nullptr;
  _fbFront = nullptr;

  if (_scanA){ free(_scanA); _scanA = nullptr; }
  if (_scanB){ free(_scanB); _scanB = nullptr; }
  _scanActive = nullptr;
}


void HUB12_1DATA::setDoubleBuffer(bool enable) {
  _doubleBuffer = enable;

  // Si aún no se ha llamado begin(), solo guardamos el flag.
  if (!_fb && !_fb2) return;

  if (_doubleBuffer) {
    if (!_fb2) {
      _fb2 = (uint8_t*)calloc(_fbBytes, 1);
      if (!_fb2) { _doubleBuffer = false; _fbFront = _fb; return; }
    }
    // Mantener lo visible en front, y usar el otro como back.
    if (!_fbFront) _fbFront = _fb;
    if (_fb == _fbFront) {
      _fb = _fb2; // back para dibujar
    }
  } else {
    // Pasar a single buffer: copiar lo visible al buffer único
    if (_fbFront && _fbFront != _fb) {
      memcpy(_fb, _fbFront, _fbBytes);
    }
    if (_fb2) { free(_fb2); _fb2 = nullptr; }
    _fbFront = _fb;
  }

  _dirty = true;
}

bool HUB12_1DATA::isDoubleBuffer() const {
  return _doubleBuffer;
}

void HUB12_1DATA::swapBuffers(bool copyFrontToBack) {
  if (!_doubleBuffer) { _fbFront = _fb; return; }
  uint8_t* tmp = _fbFront;
  _fbFront = _fb;
  _fb = tmp;
  if (copyFrontToBack && _fb && _fbFront) {
    memcpy(_fb, _fbFront, _fbBytes);
  }
}

// ---------- Fast low-level ----------
inline void HUB12_1DATA::pulseCLK() { fastHigh(_fCLK); fastLow(_fCLK); }
inline void HUB12_1DATA::pulseLAT() { fastHigh(_fLAT); fastLow(_fLAT); }

inline void HUB12_1DATA::setRow(uint8_t r) {
  if (r & 1) fastHigh(_fA); else fastLow(_fA);
  if (r & 2) fastHigh(_fB); else fastLow(_fB);
}

// DATA activo LOW: LOW=ON, HIGH=OFF
inline void HUB12_1DATA::writeData(bool on) {
  if (on) fastLow(_fDATA);
  else    fastHigh(_fDATA);
}

// ---------- Framebuffer ----------
inline bool HUB12_1DATA::fbGet(int x, int y) const {
  uint32_t idx = (uint32_t)y * _w + (uint32_t)x;
  uint8_t* src = _fbFront ? _fbFront : _fb;
  return (src[idx >> 3] >> (idx & 7)) & 1;
}

inline void HUB12_1DATA::fbSet(int x, int y, bool on) {
  uint32_t idx = (uint32_t)y * _w + (uint32_t)x;
  uint32_t b = idx >> 3;
  uint8_t bit = idx & 7;
  if (on) _fb[b] |=  (1 << bit);
  else    _fb[b] &= ~(1 << bit);
}

void HUB12_1DATA::clear() {
  memset(_fb, 0, _fbBytes);
  _dirty = true;
}

void HUB12_1DATA::drawPixel(int x, int y, bool on) {

  if (x < 0 || y < 0 || x >= (int)_w || y >= (int)_h) return;
// clipping
if (_clipEnabled) {
  if (x < _clipX0 || x > _clipX1 || y < _clipY0 || y > _clipY1) return;
}

  fbSet(x, y, on);
  _dirty = true;
}

bool HUB12_1DATA::getPixel(int x, int y) const {
  if (x < 0 || y < 0 || x >= (int)_w || y >= (int)_h) return false;
  return fbGet(x, y);
}

// ---------- Primitivas ----------
void HUB12_1DATA::drawFastHLine(int x, int y, int w, bool on) {
  if (y < 0 || y >= (int)_h) return;
  if (w < 0) { x += w; w = -w; }
  int x2 = x + w - 1;
  if (x2 < 0 || x >= (int)_w) return;
  if (x < 0) x = 0;
  if (x2 >= (int)_w) x2 = _w - 1;
  for (int i = x; i <= x2; i++) fbSet(i, y, on);
  _dirty = true;
}

void HUB12_1DATA::drawFastVLine(int x, int y, int h, bool on) {
  if (x < 0 || x >= (int)_w) return;
  if (h < 0) { y += h; h = -h; }
  int y2 = y + h - 1;
  if (y2 < 0 || y >= (int)_h) return;
  if (y < 0) y = 0;
  if (y2 >= (int)_h) y2 = _h - 1;
  for (int i = y; i <= y2; i++) fbSet(x, i, on);
  _dirty = true;
}

void HUB12_1DATA::drawRect(int x, int y, int w, int h, bool on) {
  drawFastHLine(x, y, w, on);
  drawFastHLine(x, y + h - 1, w, on);
  drawFastVLine(x, y, h, on);
  drawFastVLine(x + w - 1, y, h, on);
  _dirty = true;
}

void HUB12_1DATA::fillRect(int x, int y, int w, int h, bool on) {
  for (int yy = 0; yy < h; yy++) drawFastHLine(x, y + yy, w, on);
  _dirty = true;
}

// Bresenham
void HUB12_1DATA::drawLine(int x0, int y0, int x1, int y1, bool on) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  while (true) {
    fbSet(x0, y0, on);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
  _dirty = true;
}

// ---------- Mapeo local (32x16) confirmado ----------
int HUB12_1DATA::xyToBitIndexLocal(int x, int y) {
  // patrón confirmado por el barrido de bits:
  // band 12 -> sub 0, band 8 -> sub 1, band 4 -> sub 2, band 0 -> sub 3
  int pos8 = (x >> 3) & 3;   // 0..3
  int x8   = x & 7;          // 0..7
  int band = y & 12;         // 0,4,8,12

  int sub;
  if (band == 12) sub = 0;
  else if (band == 8) sub = 1;
  else if (band == 4) sub = 2;
  else sub = 3;

  int bitIn32 = sub * 8 + x8;    // 0..31
  return pos8 * 32 + bitIn32;    // 0..127
}

// global -> panelIdx + local coords
void HUB12_1DATA::mapGlobalToChain(int gx, int gy, uint8_t& panelIdx, int& lx, int& ly) const {
  uint8_t px = gx / 32;
  uint8_t py = gy / 16;
  lx = gx % 32;
  ly = gy % 16;

  if (!_serp) {
    panelIdx = py * _panelsX + px;
    return;
  }

  // Serpentina: filas alternadas
  if ((py & 1) == 0) {
    panelIdx = py * _panelsX + px;
  } else {
    panelIdx = py * _panelsX + (_panelsX - 1 - px);
    lx = 31 - lx; // invierte X local para mantener coordenadas globales rectas
  }
}

// ---------- Scan build (rápido) ----------
void HUB12_1DATA::buildScan() {
  // Construye en el buffer "back" y luego hace swap atomico.
  if (!_scanActive || !_scanA || !_scanB) return;

  uint8_t* out = (uint8_t*)((_scanActive == _scanA) ? _scanB : _scanA);
  memset(out, 0, _scanBytes);

  for (int gy = 0; gy < (int)_h; gy++) {
    for (int gx = 0; gx < (int)_w; gx++) {
      if (!fbGet(gx, gy)) continue;

      uint8_t panelIdx;
      int lx, ly;
      mapGlobalToChain(gx, gy, panelIdx, lx, ly);

      uint8_t r = (uint8_t)(ly & 3);

      int bit = xyToBitIndexLocal(lx, ly);       // 0..127
      uint32_t byteInPanel = (uint32_t)bit >> 3; // 0..15
      uint8_t bitInByte = 7 - (bit & 7);         // MSB-first

      uint32_t base = (uint32_t)r * _bytesPerR + (uint32_t)panelIdx * 16;
      out[base + byteInPanel] |= (1 << bitInByte);
    }
  }

#if defined(ESP32)
  portENTER_CRITICAL(&_scanMux);
  _scanActive = out;
  portEXIT_CRITICAL(&_scanMux);
#else
  _scanActive = out;
#endif

  _dirty = false;
}

void HUB12_1DATA::update() {
  if (_doubleBuffer) {
    // Lo dibujado está en _fb (back). Lo hacemos visible intercambiando buffers.
    swapBuffers(false);
    _dirty = true;
    buildScan();
  } else {
    if (_dirty) buildScan();
  }
}

// ---------- Refresh (muy rápido) ----------
void HUB12_1DATA::refresh() {
  if (_dirty) buildScan();

  const uint8_t* scan;
#if defined(ESP32)
  portENTER_CRITICAL(&_scanMux);
  scan = (const uint8_t*)_scanActive;
  portEXIT_CRITICAL(&_scanMux);
#else
  scan = (const uint8_t*)_scanActive;
#endif
  if (!scan) return;

  for (uint8_t r = 0; r < 4; r++) {
    fastLow(_fOE);     // apagar mientras carga
    setRow(r);

    const uint8_t* rowStream = scan + (uint32_t)r * _bytesPerR;

    for (uint32_t i = 0; i < _bytesPerR; i++) {
      uint8_t v = rowStream[i];
      for (int b = 7; b >= 0; b--) {
        writeData((v >> b) & 1);
        pulseCLK();
      }
    }

    pulseLAT();
    fastHigh(_fOE);    // mostrar
    delayMicroseconds(_onTimeUs);
  }
}

void HUB12_1DATA::drawCircle(int x0, int y0, int r, bool on) {
  int x = -r;
  int y = 0;
  int err = 2 - 2 * r;

  do {
    drawPixel(x0 - x, y0 + y, on);
    drawPixel(x0 - y, y0 - x, on);
    drawPixel(x0 + x, y0 - y, on);
    drawPixel(x0 + y, y0 + x, on);

    int e2 = err;
    if (e2 <= y) {
      y++;
      err += y * 2 + 1;
      if (-x == y && e2 <= x) e2 = 0;
    }
    if (e2 > x) {
      x++;
      err += x * 2 + 1;
    }
  } while (x <= 0);

  _dirty = true;
}

// ===================== CIRCULO RELLENO =====================
void HUB12_1DATA::fillCircle(int x0, int y0, int r, bool on) {
  // Algoritmo: Midpoint + líneas verticales (rápido)
  int x = 0;
  int y = r;
  int d = 1 - r;

  // línea central
  drawFastVLine(x0, y0 - r, 2 * r + 1, on);

  while (y >= x) {
    // cuatro "columnas" simétricas
    drawFastVLine(x0 + x, y0 - y, 2 * y + 1, on);
    drawFastVLine(x0 - x, y0 - y, 2 * y + 1, on);
    drawFastVLine(x0 + y, y0 - x, 2 * x + 1, on);
    drawFastVLine(x0 - y, y0 - x, 2 * x + 1, on);

    x++;
    if (d < 0) {
      d += 2 * x + 1;
    } else {
      y--;
      d += 2 * (x - y) + 1;
    }
  }

  _dirty = true;
}

// ===================== TRIANGULO (BORDES) =====================
void HUB12_1DATA::drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, bool on) {
  drawLine(x0, y0, x1, y1, on);
  drawLine(x1, y1, x2, y2, on);
  drawLine(x2, y2, x0, y0, on);
  _dirty = true;
}

// ===================== TRIANGULO RELLENO =====================
static inline void _swapInt(int &a, int &b) { int t = a; a = b; b = t; }

void HUB12_1DATA::fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, bool on) {

  // Ordenar por y: y0 <= y1 <= y2
  if (y0 > y1) { _swapInt(y0, y1); _swapInt(x0, x1); }
  if (y1 > y2) { _swapInt(y1, y2); _swapInt(x1, x2); }
  if (y0 > y1) { _swapInt(y0, y1); _swapInt(x0, x1); }

  // Triángulo degenerado (línea)
  if (y0 == y2) {
    int a = x0, b = x0;
    if (x1 < a) a = x1; if (x1 > b) b = x1;
    if (x2 < a) a = x2; if (x2 > b) b = x2;
    drawFastHLine(a, y0, b - a + 1, on);
    _dirty = true;
    return;
  }

  // Usamos “scanline fill”
  long dx01 = x1 - x0, dy01 = y1 - y0;
  long dx02 = x2 - x0, dy02 = y2 - y0;
  long dx12 = x2 - x1, dy12 = y2 - y1;

  long sa = 0, sb = 0;

  int y, last;

  // Parte superior (y0->y1)
  if (y1 == y2) last = y1;
  else last = y1 - 1;

  for (y = y0; y <= last; y++) {
    int a = x0 + (dy01 ? sa / dy01 : 0);
    int b = x0 + (dy02 ? sb / dy02 : 0);
    sa += dx01;
    sb += dx02;
    if (a > b) _swapInt(a, b);
    drawFastHLine(a, y, b - a + 1, on);
  }

  // Parte inferior (y1->y2)
  sa = dx12 * (y - y1);
  sb = dx02 * (y - y0);

  for (; y <= y2; y++) {
    int a = x1 + (dy12 ? sa / dy12 : 0);
    int b = x0 + (dy02 ? sb / dy02 : 0);
    sa += dx12;
    sb += dx02;
    if (a > b) _swapInt(a, b);
    drawFastHLine(a, y, b - a + 1, on);
  }

  _dirty = true;
}

void HUB12_1DATA::setFont(const uint8_t* font) {
  _font = font;
}

#include <pgmspace.h>  // asegúrate que esté arriba del archivo

void HUB12_1DATA::drawChar(int x, int y, char c, bool on) {
  if (!_font) return;

  uint16_t size = (uint16_t)pgm_read_byte(_font + 0) |
                  ((uint16_t)pgm_read_byte(_font + 1) << 8);

  uint8_t fontW   = pgm_read_byte(_font + 2);
  uint8_t fontH   = pgm_read_byte(_font + 3);
  uint8_t first   = pgm_read_byte(_font + 4);
  uint8_t count   = pgm_read_byte(_font + 5);

  uint8_t uc = (uint8_t)c;
  if (uc < first || uc >= (uint8_t)(first + count)) return;

  uint8_t idx = uc - first;

  uint8_t bytesPerCol = (fontH + 7) >> 3;

  const uint8_t* widths = _font + 6;
  const uint8_t* data;

  uint8_t charW = fontW;
  uint32_t offset = 0;

  if (size == 0) {
    // ANCHO FIJO (System5x7): data empieza en +6
    data = _font + 6;
    charW = fontW;
    offset = (uint32_t)idx * (uint32_t)charW * bytesPerCol;
  } else {
    // ANCHO VARIABLE (Arial_Black_16): widths[0..count-1], data empieza después
    data = widths + count;
    charW = pgm_read_byte(widths + idx);

    for (uint8_t i = 0; i < idx; i++) {
      offset += (uint32_t)pgm_read_byte(widths + i) * bytesPerCol;
    }
  }

  // Dibujo: en FontCreator los bytes no vienen intercalados por columna cuando fontH>8:
  // primero todas las columnas LOW, luego todas las columnas HIGH (por bloques de charW).
  for (uint8_t col = 0; col < charW; col++) {
    for (uint8_t row = 0; row < fontH; row++) {

      uint8_t byteIndex = row >> 3;   // 0..bytesPerCol-1
      uint8_t bitIndex  = row & 7;

      uint32_t colOffset;
      if (bytesPerCol == 1) {
        colOffset = offset + col;
      } else {
        colOffset = offset + col + (uint32_t)byteIndex * (uint32_t)charW;
      }

      uint8_t b = pgm_read_byte(data + colOffset);

      if (b & (1 << bitIndex)) {
        drawPixel(x + col, y + row, on);
      }
    }
  }

  _dirty = true;
}


void HUB12_1DATA::drawText(int x, int y, const char* s, bool on, uint8_t spacing) {
  if (!_font) return;

  uint16_t size = (uint16_t)pgm_read_byte(_font + 0) |
                  ((uint16_t)pgm_read_byte(_font + 1) << 8);

  uint8_t fontW = pgm_read_byte(_font + 2);
  uint8_t first = pgm_read_byte(_font + 4);
  uint8_t count = pgm_read_byte(_font + 5);

  const uint8_t* widths = _font + 6;

  int cx = x;
  while (*s) {
    char c = *s++;
    uint8_t uc = (uint8_t)c;

    uint8_t adv = fontW; // default

    if (size != 0) {
      if (uc >= first && uc < (uint8_t)(first + count)) {
        adv = pgm_read_byte(widths + (uc - first));
      }
    }

    drawChar(cx, y, c, on);
    cx += (int)adv + (int)spacing;
  }

  _dirty = true;
}
int HUB12_1DATA::textWidth(const char* s, uint8_t spacing) {
  if (!_font || !s) return 0;

  uint16_t size = (uint16_t)pgm_read_byte(_font + 0) |
                  ((uint16_t)pgm_read_byte(_font + 1) << 8);

  uint8_t fontW = pgm_read_byte(_font + 2);
  uint8_t first = pgm_read_byte(_font + 4);
  uint8_t count = pgm_read_byte(_font + 5);

  const uint8_t* widths = _font + 6;

  int w = 0;
  while (*s) {
    uint8_t uc = (uint8_t)(*s++);
    uint8_t adv = fontW; // fijo por defecto

    if (size != 0) { // fuente variable
      if (uc >= first && uc < (uint8_t)(first + count)) {
        adv = pgm_read_byte(widths + (uc - first));
      }
    }

    w += adv;
    if (*s) w += spacing; // no agregar espacio extra al final
  }
  return w;
}
void HUB12_1DATA::drawCharScaled(int x, int y, char c, uint8_t scale, bool on) {
  if (!_font) return;
  if (scale < 1) scale = 1;

  uint16_t size = (uint16_t)pgm_read_byte(_font + 0) |
                  ((uint16_t)pgm_read_byte(_font + 1) << 8);

  uint8_t fontW   = pgm_read_byte(_font + 2);
  uint8_t fontH   = pgm_read_byte(_font + 3);
  uint8_t first   = pgm_read_byte(_font + 4);
  uint8_t count   = pgm_read_byte(_font + 5);

  uint8_t uc = (uint8_t)c;
  if (uc < first || uc >= (uint8_t)(first + count)) return;

  uint8_t idx = uc - first;
  uint8_t bytesPerCol = (fontH + 7) >> 3;

  const uint8_t* widths = _font + 6;
  const uint8_t* data;

  uint8_t charW = fontW;
  uint32_t offset = 0;

  if (size == 0) {
    // ancho fijo (System5x7)
    data = _font + 6;
    charW = fontW;
    offset = (uint32_t)idx * (uint32_t)charW * bytesPerCol;
  } else {
    // ancho variable (Arial_Black_16)
    data = widths + count;
    charW = pgm_read_byte(widths + idx);

    for (uint8_t i = 0; i < idx; i++) {
      offset += (uint32_t)pgm_read_byte(widths + i) * bytesPerCol;
    }
  }

  // Dibujo escalado (respeta el “layout” FontCreator para H>8)
  for (uint8_t col = 0; col < charW; col++) {
    for (uint8_t row = 0; row < fontH; row++) {

      uint8_t byteIndex = row >> 3;
      uint8_t bitIndex  = row & 7;

      uint32_t colOffset;
      if (bytesPerCol == 1) {
        colOffset = offset + col;
      } else {
        colOffset = offset + col + (uint32_t)byteIndex * (uint32_t)charW;
      }

      uint8_t b = pgm_read_byte(data + colOffset);

      if (b & (1 << bitIndex)) {
        // pixel ON -> bloque scale x scale
        fillRect(x + (int)col * scale, y + (int)row * scale, scale, scale, on);
      }
    }
  }

  _dirty = true;
}

void HUB12_1DATA::drawTextScaled(int x, int y, const char* s, uint8_t scale, bool on, uint8_t spacing) {
  if (!_font || !s) return;
  if (scale < 1) scale = 1;

  uint16_t size = (uint16_t)pgm_read_byte(_font + 0) |
                  ((uint16_t)pgm_read_byte(_font + 1) << 8);

  uint8_t fontW = pgm_read_byte(_font + 2);
  uint8_t first = pgm_read_byte(_font + 4);
  uint8_t count = pgm_read_byte(_font + 5);
  const uint8_t* widths = _font + 6;

  int cx = x;
  while (*s) {
    char c = *s++;
    uint8_t uc = (uint8_t)c;

    uint8_t adv = fontW; // avance base
    if (size != 0) {     // variable
      if (uc >= first && uc < (uint8_t)(first + count)) {
        adv = pgm_read_byte(widths + (uc - first));
      }
    }

    drawCharScaled(cx, y, c, scale, on);
    cx += (int)(adv + spacing) * (int)scale;
  }

  _dirty = true;
}
int HUB12_1DATA::textWidthScaled(const char* s, uint8_t scale, uint8_t spacing) {
  if (scale < 1) scale = 1;
  // Reusa tu textWidth normal y solo escala
  return textWidth(s, spacing) * (int)scale;
}

void HUB12_1DATA::drawTextScaledCentered(const char* s, uint8_t scale, bool on, uint8_t spacing) {
  if (!s) return;
  if (scale < 1) scale = 1;
  if (!_font) return;

  uint8_t fontH = pgm_read_byte(_font + 3);

  int tw = textWidthScaled(s, scale, spacing);
  int th = (int)fontH * (int)scale;

  int x = (width()  - tw) / 2;
  int y = (height() - th) / 2;

  drawTextScaled(x, y, s, scale, on, spacing);
  _dirty = true;
}
void HUB12_1DATA::setClipRect(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) { _clipEnabled = false; return; }

  int x0 = x;
  int y0 = y;
  int x1 = x + w - 1;
  int y1 = y + h - 1;

  // recorta al tamaño del display
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 >= (int)width())  x1 = width() - 1;
  if (y1 >= (int)height()) y1 = height() - 1;

  if (x0 > x1 || y0 > y1) { _clipEnabled = false; return; }

  _clipEnabled = true;
  _clipX0 = x0; _clipY0 = y0;
  _clipX1 = x1; _clipY1 = y1;
}

void HUB12_1DATA::clearClipRect() {
  _clipEnabled = false;
}
uint8_t HUB12_1DATA::fontHeight() const {
  if (!_font) return 0;
  return pgm_read_byte(_font + 3);
}

uint8_t HUB12_1DATA::fontWidth() const {
  if (!_font) return 0;
  return pgm_read_byte(_font + 2);
}
void HUB12_1DATA::marqueeStart(int x, int y, int w, int h, const char* text, uint8_t spacing, uint16_t stepMs) {
  _mqEnabled = false;
  _mqText = text;
  if (!_mqText || !_font) return;
  if (w <= 0 || h <= 0) return;

  // recorta a pantalla
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > (int)width())  w = width()  - x;
  if (y + h > (int)height()) h = height() - y;
  if (w <= 0 || h <= 0) return;

  _mqX0 = x; _mqY0 = y; _mqW = w; _mqH = h;
  _mqSpacing = spacing;
  _mqStepMs = stepMs;

  _mqTextW = textWidth(_mqText, _mqSpacing);
  _mqX = _mqX0 + _mqW;   // entra desde la derecha

  _mqLast = millis();
  _mqEnabled = true;
}

void HUB12_1DATA::marqueeTick(bool on) {
  if (!_mqEnabled || !_mqText || !_font) return;

  unsigned long now = millis();
  if ((uint16_t)(now - _mqLast) < _mqStepMs) return;
  _mqLast = now;

  // limpiar SOLO la ventana
  fillRect(_mqX0, _mqY0, _mqW, _mqH, false);

  // clip a la ventana
  setClipRect(_mqX0, _mqY0, _mqW, _mqH);

  // centra verticalmente según fuente actual
  int yText = _mqY0 + (_mqH - (int)fontHeight()) / 2;

  drawText(_mqX, yText, _mqText, on, _mqSpacing);

  clearClipRect();

  update();

  // mover
  _mqX--;
  if (_mqX < (_mqX0 - _mqTextW)) _mqX = _mqX0 + _mqW;
}

void HUB12_1DATA::marqueeStop() {
  _mqEnabled = false;
  _mqText = nullptr;
}
#if defined(ESP32)

void HUB12_1DATA::_arTimerCb(void* arg) {
  HUB12_1DATA* self = (HUB12_1DATA*)arg;
  if (self->_arTask) {
    // Despierta el task (binario). Si el refresh va atrasado, se salta ticks.
    xTaskNotifyGive(self->_arTask);
  }
}

void HUB12_1DATA::_arTaskFn(void* arg) {
  HUB12_1DATA* self = (HUB12_1DATA*)arg;
  for (;;) {
    // Espera "tick" del timer. Si llegaron varios, ulTaskNotifyTake devuelve >1.
    // Nosotros hacemos UN refresh por despertar para evitar backlog infinito.
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!self->_arEnabled) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    self->refresh();
  }
}

bool HUB12_1DATA::startAutoRefresh(uint32_t periodUs) {
  if (_arEnabled) return true;

  // refresh() tarda aprox: 4*_onTimeUs + overhead (shift/latch).
  // Si el periodo es menor, el timer se satura y el ESP32 termina colgándose.
  uint32_t minUs = (uint32_t)_onTimeUs * 4u + 300u; // margen
  if (periodUs < minUs) periodUs = minUs;
  _arPeriodUs = periodUs;

  // Task dedicado
  if (!_arTask) {
    xTaskCreate(_arTaskFn, "hub12_ar_task", 4096, this, 1, &_arTask);
    if (!_arTask) return false;
  }

  esp_timer_create_args_t args = {};
  args.callback = &HUB12_1DATA::_arTimerCb;
  args.arg = this;
  args.dispatch_method = ESP_TIMER_TASK;   // importante: no ISR
  args.name = "hub12_ar";

  if (esp_timer_create(&args, &_arTimer) != ESP_OK) {
    _arTimer = nullptr;
    return false;
  }
  if (esp_timer_start_periodic(_arTimer, _arPeriodUs) != ESP_OK) {
    esp_timer_delete(_arTimer);
    _arTimer = nullptr;
    return false;
  }

  _arEnabled = true;
  return true;
}

void HUB12_1DATA::stopAutoRefresh() {
  if (!_arEnabled) return;
  if (_arTimer) {
    esp_timer_stop(_arTimer);
    esp_timer_delete(_arTimer);
    _arTimer = nullptr;
  }
  _arEnabled = false;

  // No destruimos el task para evitar fragmentación; queda dormido.
}

bool HUB12_1DATA::isAutoRefresh() const {
  return _arEnabled;
}

#endif




// ===================== Print / cursor =====================

void HUB12_1DATA::setCursor(int16_t x, int16_t y) {
  _cx = x;
  _cy = y;
}

void HUB12_1DATA::setTextSize(uint8_t size) {
  if (size < 1) size = 1;
  _tsize = size;
}

uint8_t HUB12_1DATA::charWidth(char c) const {
  if (!_font) return 0;

  const uint8_t fontW = pgm_read_byte(_font + 2);
  const uint8_t first = pgm_read_byte(_font + 4);
  const uint8_t count = pgm_read_byte(_font + 5);

  // size==0 => fuente fija (System5x7, etc)
  const uint16_t sizeBytes = (uint16_t)pgm_read_byte(_font) | ((uint16_t)pgm_read_byte(_font + 1) << 8);
  if (sizeBytes == 0) return fontW;

  int idx = (int)c - (int)first;
  if (idx < 0 || idx >= (int)count) return fontW;

  const uint8_t* widths = _font + 6;
  return pgm_read_byte(widths + idx);
}

size_t HUB12_1DATA::write(uint8_t c) {
  if (c == '\r') return 1;

  // newline
  if (c == '\n') {
    _cx = 0;
    _cy += (int)fontHeight() * (int)_tsize + 1;
    return 1;
  }

  // si no hay fuente, no dibujamos
  if (!_font) return 1;

  if (_tsize == 1) {
    drawChar(_cx, _cy, (char)c, _tcolor);
    _cx += (int)charWidth((char)c) + (int)_tspacing;
  } else {
    char s[2] = {(char)c, 0};
    drawTextScaled(_cx, _cy, s, _tsize, _tcolor, _tspacing);
    _cx += ((int)charWidth((char)c) * (int)_tsize) + (int)_tspacing;
  }

  // wrap
  if (_wrap && _cx >= (int)width()) {
    _cx = 0;
    _cy += (int)fontHeight() * (int)_tsize + 1;
  }

  return 1;
}

int HUB12_1DATA::printf(const char* fmt, ...) {
  char buf[128];
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (n < 0) return n;
  print(buf);
  return n;
}
