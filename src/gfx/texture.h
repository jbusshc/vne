#pragma once
#include "base/handle.h"
#include "base/types.h"

// Carga de texturas horneadas en formato QOI (ADR-0008). No sabe nada de sokol_gfx: eso
// vive en texture_internal.h, de uso exclusivo de src/gfx/*.cpp.

enum class TextureLoadResult : u8 { Ok, NotFound, BadFormat, OutOfSlots };

// Un fallo nunca es fatal: *out siempre queda con un handle valido, apuntando al
// placeholder magenta si la carga fallo, y el juego continua (SPEC.md #4).
TextureLoadResult texture_load(const char* path, TextureHandle* out);

// Ancho/alto en pixeles. Escribe 0,0 si el handle no resuelve a nada.
void texture_size(TextureHandle h, i32* out_w, i32* out_h);
