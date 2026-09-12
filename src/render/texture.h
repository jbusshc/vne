#pragma once
#include "core/handle.h"
#include "core/types.h"

// Carga de texturas horneadas en formato QOI (ADR-0008). No sabe nada de sokol_gfx: eso
// vive en texture_internal.h, de uso exclusivo de src/render/*.cpp.

enum class TextureLoadResult : u8 { Ok, NotFound, BadFormat, OutOfSlots };

// logical_name se resuelve contra el backend de assets activo (directorio suelto o .pak,
// ver vfs/pak.h -- p.ej. "atlas_00.qoi"), no una ruta de archivo literal (M11).
//
// Un fallo nunca es fatal: *out siempre queda con un handle valido, apuntando al
// placeholder magenta si la carga fallo, y el juego continua (SPEC.md #4).
TextureLoadResult texture_load(const char* logical_name, TextureHandle* out);

// --- Carga en dos fases (M11, assets/assets.cpp): el hilo de IO no puede llamar a
// sg_make_image (sokol_gfx no es thread-safe), asi que la carga real de un asset_texture
// se parte en reservar-ahora + terminar-despues sobre el MISMO handle.

// Reserva un slot que empieza apuntando al placeholder magenta (misma imagen/sampler que
// usa un texture_load fallido). Instantaneo: no decodifica ni toca la GPU mas alla de
// reutilizar los objetos del placeholder ya creados. Este es el handle que
// assets_texture() devuelve de inmediato.
TextureHandle texture_reserve_placeholder();

// Decodifica QOI ya en memoria (bytes que el hilo de IO leyo del disco o del .pak) y sube
// la imagen real al slot que ya devolvio texture_reserve_placeholder(), en el sitio: el
// handle no cambia, solo lo que apunta. Debe llamarse desde el hilo principal (misma
// razon que arriba). Si el decode falla, el slot se queda apuntando al placeholder y
// devuelve false (nunca fatal, SPEC.md #4).
bool texture_finish_load_from_memory(TextureHandle handle, const u8* qoi_bytes,
                                      usize qoi_size);

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
