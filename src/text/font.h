#pragma once
#include "base/handle.h"
#include "base/types.h"

// Carga de fuentes con FreeType (SPEC.md #7.2). No sabe nada de FreeType/HarfBuzz: eso
// vive en font_internal.h, de uso exclusivo de src/text/*.cpp.

// Devuelve un handle invalido (h.valid() == false) si la carga falla; el error concreto
// queda en el log. No hay placeholder de fuente: sin fuente no hay texto que dibujar.
FontHandle text_load_font(const char* path, u32 px_size);
