#pragma once
#include "base/handle.h"
#include "base/types.h"

// Carga de fuentes con FreeType (SPEC.md #7.2). No sabe nada de FreeType/HarfBuzz: eso
// vive en font_internal.h, de uso exclusivo de src/text/*.cpp.

// logical_name se resuelve contra el backend de assets activo (directorio suelto o .pak,
// ver assets/pak.h -- p.ej. "ttf/NotoSans.ttf"), no una ruta de archivo literal (M11).
// Sincrono: parsear metricas de un TTF es barato, no pasa por el hilo de IO (ver el
// comentario de text_load_font en font.cpp para el porque).
//
// Devuelve un handle invalido (h.valid() == false) si la carga falla; el error concreto
// queda en el log. No hay placeholder de fuente: sin fuente no hay texto que dibujar.
FontHandle text_load_font(const char* logical_name, u32 px_size);
