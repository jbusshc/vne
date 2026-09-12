#include <doctest/doctest.h>

#include "core/heap_guard.h"

// Tests del contador de asignaciones por frame (SPEC.md #4). Solo tienen sentido bajo
// SZ_DEBUG: en Ship no hay overload de operator new ni hooks que cuenten, y todas las
// funciones se compilan a nada.
//
// El caso que de verdad importa aqui es el anidamiento de suspend/resume. Fue un bug real
// hasta M12: g_heap_guard_suspended era un bool, asi que el resume de un ambito interno
// reactivaba el contador dejando al externo desprotegido. Pasaba de verdad en dos sitios
// (`@lua` -> vn.play_sfx -> audio_load, y hot_reload_update -> sz_bake) y nadie lo vio
// porque hasta ADR-0058 las asignaciones de terceros ni siquiera se contaban.

#if defined(SZ_DEBUG)

TEST_CASE("heap_guard: cuenta una asignacion de tercero y la resetea por frame") {
    heap_guard_reset_frame();
    CHECK(g_frame_alloc_count == 0);

    heap_guard_count_alloc(HeapSource::Sdl);
    heap_guard_count_alloc(HeapSource::Qoi);
    CHECK(g_frame_alloc_count == 2);
    CHECK(heap_guard_count_by_source(HeapSource::Sdl) == 1);
    CHECK(heap_guard_count_by_source(HeapSource::Qoi) == 1);
    CHECK(heap_guard_count_by_source(HeapSource::Sokol) == 0);

    heap_guard_reset_frame();
    CHECK(g_frame_alloc_count == 0);
    CHECK(heap_guard_count_by_source(HeapSource::Sdl) == 0);
}

TEST_CASE("heap_guard: suspend/resume anidados no dejan huecos sin proteger") {
    heap_guard_reset_frame();

    heap_guard_suspend();  // ambito externo (p. ej. ejecutar @lua)
    heap_guard_count_alloc(HeapSource::MiniAudio);

    heap_guard_suspend();  // ambito interno (p. ej. audio_load desde vn.play_sfx)
    heap_guard_count_alloc(HeapSource::MiniAudio);
    heap_guard_resume();

    // Con el bool de antes de M12, este resume interno habria vuelto a encender el guard y
    // la siguiente asignacion se habria contado aunque seguimos dentro del @lua.
    heap_guard_count_alloc(HeapSource::MiniAudio);
    CHECK(g_frame_alloc_count == 0);

    heap_guard_resume();  // cierra el externo: a partir de aqui si se cuenta
    heap_guard_count_alloc(HeapSource::MiniAudio);
    CHECK(g_frame_alloc_count == 1);

    heap_guard_reset_frame();
}

TEST_CASE("heap_guard: reset_frame no altera la profundidad de suspension") {
    // reset_frame corre al principio de cada frame. Si ademas reseteara la profundidad,
    // una suspension que cruzara el limite de frame se perderia en silencio.
    heap_guard_suspend();
    heap_guard_reset_frame();
    heap_guard_count_alloc(HeapSource::FreeType);
    CHECK(g_frame_alloc_count == 0);

    heap_guard_resume();
    heap_guard_count_alloc(HeapSource::FreeType);
    CHECK(g_frame_alloc_count == 1);

    heap_guard_reset_frame();
}

#endif  // SZ_DEBUG
