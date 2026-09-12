#pragma once
#include "core/handle.h"
#include "core/types.h"

// Carga de fuentes con FreeType (SPEC.md #7.2). No sabe nada de FreeType/HarfBuzz: eso
// vive en font_internal.h, de uso exclusivo de src/text/*.cpp.

// logical_name se resuelve contra el backend de assets activo (directorio suelto o .pak,
// ver vfs/pak.h -- p.ej. "ttf/NotoSans-subset.ttf"), no una ruta de archivo literal (M11).
// Sincrono: parsear metricas de un TTF es barato, no pasa por el hilo de IO (ver el
// comentario de text_load_font en font.cpp para el porque).
//
// bold = true carga la MISMA cara pero marcada para engordar el contorno al rasterizar
// (negrita sintetica, M12): no hay ningun TTF en negrita entre los assets, y {b} necesita
// un FontHandle propio para que el cache de glifos no mezcle los normales con los
// engordados. Por lo demas es una fuente como cualquier otra.
//
// Devuelve un handle invalido (h.valid() == false) si la carga falla; el error concreto
// queda en el log. No hay placeholder de fuente: sin fuente no hay texto que dibujar.
FontHandle text_load_font(const char* logical_name, u32 px_size, bool bold = false);

// Vuelve a cargar una fuente EN EL SITIO (M11, recarga en caliente): el handle no cambia,
// asi que todo el que ya lo tenga guardado (VnMode, MenuMode...) pasa a usar la version
// nueva sin enterarse. Devuelve false y deja la fuente anterior intacta si la recarga
// falla, para no dejar el juego sin fuente por un archivo a medio guardar.
//
// El llamante debe invalidar los glifos ya rasterizados de esta fuente
// (glyph_cache_invalidate_font), o se seguirian dibujando los viejos.
bool text_reload_font(FontHandle handle, const char* logical_name, u32 px_size);
