#pragma once
#include "core/handle.h"
#include "core/types.h"

// Recarga en caliente de assets (SPEC.md #7.4: "en Debug y Dev, un watcher comprueba
// mtimes cada 500 ms y recarga texturas, fuentes, shaders y scripts en caliente").
// Sustituye al watcher de M8, que vigilaba un unico archivo fijo (demo.vns).
//
// REPARTO DELIBERADO con el watcher de scripts del editor:
//   - Aqui: lo que se recarga entero dentro del sistema de assets (texturas y fuentes).
//     assets/ no sabe que es un GameState ni una VM, y no debe saberlo.
//   - editor/editor.cpp: los .vns, porque recargar un guion significa ademas tocar el
//     CompiledScript vivo y el pc de la VM (ADR-0042: sz_bake se invoca como subproceso
//     desde el editor, no desde el juego).
// Juntos cubren el criterio de M11: tocar un .png, un .ttf o un .vns recarga los tres.
//
// Los shaders que menciona SPEC.md #7.4 no entran: se escriben a mano en C++
// (src/render/shaders.h, ADR-0010), no hay archivo suelto que vigilar.
//
// Todo esto solo existe en Debug/Dev. En Ship el .pak es inmutable y no hay nada que
// vigilar: las funciones se compilan vacias.

void hot_reload_init();

// Registra una fuente ya cargada para vigilar su .ttf. El handle se recarga en el sitio
// (text_reload_font), asi que quien lo tenga guardado no se entera de nada.
void hot_reload_watch_font(FontHandle handle, const char* logical_name, u32 px_size);

// Llamar una vez por frame. Comprueba mtimes como mucho cada 500 ms (SPEC.md #7.4), no en
// cada frame: son syscalls al filesystem.
void hot_reload_update(f32 dt);
