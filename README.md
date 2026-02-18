# HUB12_1DATA (v1.0.0 PRO)

Librería para **paneles P10 HUB12 monocromáticos (32x16) con 1 DATA (R1)** usando **ESP32 / ESP32-S3**.

## Características
- Framebuffer 1bpp
- Render por **scan 1/4** (HUB12)
- `update()` (reconstruye scan) + `refresh()` / `startAutoRefresh()`
- **DirtyRect** para actualizar solo lo que cambió
- **Doble buffer** opcional (framebuffer)
- Primitivas: pixel, líneas, rectángulos, círculos, triángulos (fill y outline)
- Texto con fuentes tipo **DMD2** (`SystemFont5x7`, `Arial_Black_16`, etc.)
- Texto escalado, clipping, marquesinas
- API estilo Arduino: `setCursor()`, `print()`, `printf()`

## Instalación
1. Arduino IDE → **Sketch → Include Library → Add .ZIP Library…**
2. Selecciona el ZIP de esta librería.

## Pines (ejemplo)
```cpp
HUB12_1DATA::Pins pins = {36,1,2,41,40,39}; // OE,A,B,CLK,LAT,DATA
HUB12_1DATA d(pins, 1, 1, false);
```

## Uso básico
- Dibuja en framebuffer con `drawPixel/drawText/...`
- Llama `update()` para pasar al scan buffer
- Mantén el refresco:
  - con `startAutoRefresh(periodUs)` **(recomendado)**
  - o llamando `refresh()` muy seguido en `loop()`

### Nota de estabilidad
Si usas `setOnTimeUs(600)`, usa `startAutoRefresh(5000)` o mayor para evitar saturación.

## Ejemplos
- **print_cursor_counter**: `setCursor()` + `printf()` con contador.
- **scroll_two_windows**: 2 marquesinas con clipping (0..15) y (16..31).

## Licencia
MIT (puedes cambiarla si deseas).
