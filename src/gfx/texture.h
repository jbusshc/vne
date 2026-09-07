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

// Textura RGBA8 mutable de tamano fijo, para contenido generado en runtime (el atlas de
// glifos de text/glyph_cache.cpp; SPEC.md #7.2 pide paginas de 1024x1024 formato R8, pero
// se sube como RGBA8 con el byte de cobertura replicado en los 4 canales para poder
// dibujar glifos con el mismo pipeline de sprites de M1 sin un shader aparte — ver
// docs/DECISIONS.md, hito M2). No pensada para uso general: solo texture_update_dynamic
// puede modificarla despues de creada.
TextureHandle texture_create_dynamic(i32 width, i32 height);

// pixels_rgba debe tener width*height*4 bytes, el tamano con el que se creo la textura.
void texture_update_dynamic(TextureHandle h, const u8* pixels_rgba);
