#include "core/heap_guard.h"

#include "core/assert.h"
#include "core/log.h"

thread_local u64 g_frame_alloc_count = 0;
// thread_local por la misma razon que g_frame_alloc_count (ver heap_guard.h): aunque hoy
// solo el hilo principal llama a suspend/resume, dejarlo global compartido seria una
// lectura/escritura sin sincronizar desde el punto de vista del hilo de IO en cuanto ese
// hilo exista (M11) — inofensivo en la practica porque su contador nunca se consulta,
// pero es una carrera de datos real bajo el modelo de memoria de C++. Con thread_local
// deja de serlo, sin coste ni cambio de comportamiento observable.
// Profundidad, no un bool. Las excepciones SI se anidan, y con un bool el resume interno
// reactivaba el contador mientras el ambito externo seguia esperando estar suspendido:
//
//   - `@lua` suspende y, si el guion llama a vn.play_sfx, audio_load suspende y reanuda
//     dentro (existe desde M6, nunca se noto porque las asignaciones de Lua son de
//     terceros en C y hasta ADR-0058 tampoco se contaban).
//   - hot_reload_update suspende y rebake_atlas_and_reload() suspende y reanuda dentro.
//
// Con un contador, solo el resume que cierra el ultimo suspend vuelve a encender el guard.
static thread_local u32 g_heap_guard_depth = 0;

namespace {

constexpr u32 k_source_count = static_cast<u32>(HeapSource::Count);

thread_local u64 g_by_source[k_source_count] = {};

const char* source_name(u32 i) {
    switch (static_cast<HeapSource>(i)) {
        case HeapSource::Engine:    return "motor";
        case HeapSource::Sdl:       return "SDL3";
        case HeapSource::Sokol:     return "sokol";
        case HeapSource::FreeType:  return "FreeType";
        case HeapSource::MiniAudio: return "miniaudio";
        case HeapSource::Qoi:       return "qoi";
        case HeapSource::ImGui:     return "ImGui";
        case HeapSource::Count:     break;
    }
    return "?";
}

}  // namespace

void heap_guard_reset_frame() {
    g_frame_alloc_count = 0;
    for (u32 i = 0; i < k_source_count; ++i) {
        g_by_source[i] = 0;
    }
}

u64 heap_guard_count_by_source(HeapSource source) {
    return g_by_source[static_cast<u32>(source)];
}

void heap_guard_check_frame() {
    if (g_frame_alloc_count == 0) {
        return;
    }
    // El desglose va antes del assert: si el assert aborta, este log es lo unico que
    // queda para saber quien fue.
    for (u32 i = 0; i < k_source_count; ++i) {
        if (g_by_source[i] != 0) {
            log_error("heap dentro del frame: %llu de %s",
                      static_cast<unsigned long long>(g_by_source[i]), source_name(i));
        }
    }
    SZ_ASSERT(g_frame_alloc_count == 0, "asignacion de heap dentro del frame");
}

void heap_guard_suspend() {
    g_heap_guard_depth += 1;
}

void heap_guard_resume() {
    // Un resume sin suspend es un bug de emparejamiento en el llamante. Se avisa en vez de
    // desbordar el contador a 0xFFFFFFFF, que dejaria el guard suspendido para siempre y
    // convertiria la regla de cero heap en decorativa sin que nadie se enterara.
    SZ_ASSERT(g_heap_guard_depth > 0, "heap_guard_resume sin su heap_guard_suspend");
    if (g_heap_guard_depth > 0) {
        g_heap_guard_depth -= 1;
    }
}

void heap_guard_count_alloc(HeapSource source) {
#if defined(SZ_DEBUG)
    if (g_heap_guard_depth == 0) {
        g_frame_alloc_count += 1;
        g_by_source[static_cast<u32>(source)] += 1;
    }
#else
    (void)source;
#endif
}

#if defined(SZ_DEBUG)

#include <cstdlib>

// Sin excepciones en este proyecto (-fno-exceptions): un fallo de asignacion aborta en
// vez de lanzar std::bad_alloc.
void* operator new(usize size) {
    if (g_heap_guard_depth == 0) {
        g_frame_alloc_count += 1;
        g_by_source[static_cast<u32>(HeapSource::Engine)] += 1;
    }
    void* p = std::malloc(size == 0 ? 1 : size);
    SZ_ASSERT(p != nullptr, "operator new: sin memoria");
    return p;
}

void* operator new[](usize size) {
    return operator new(size);
}

void operator delete(void* p) noexcept {
    std::free(p);
}

void operator delete[](void* p) noexcept {
    std::free(p);
}

void operator delete(void* p, usize) noexcept {
    std::free(p);
}

void operator delete[](void* p, usize) noexcept {
    std::free(p);
}

#endif  // SZ_DEBUG
