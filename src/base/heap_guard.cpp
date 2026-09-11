#include "base/heap_guard.h"

#include "base/assert.h"
#include "base/log.h"

thread_local u64 g_frame_alloc_count = 0;
// thread_local por la misma razon que g_frame_alloc_count (ver heap_guard.h): aunque hoy
// solo el hilo principal llama a suspend/resume, dejarlo global compartido seria una
// lectura/escritura sin sincronizar desde el punto de vista del hilo de IO en cuanto ese
// hilo exista (M11) — inofensivo en la practica porque su contador nunca se consulta,
// pero es una carrera de datos real bajo el modelo de memoria de C++. Con thread_local
// deja de serlo, sin coste ni cambio de comportamiento observable.
static thread_local bool g_heap_guard_suspended = false;

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
    VN_ASSERT(g_frame_alloc_count == 0, "asignacion de heap dentro del frame");
}

void heap_guard_suspend() {
    g_heap_guard_suspended = true;
}

void heap_guard_resume() {
    g_heap_guard_suspended = false;
}

void heap_guard_count_alloc(HeapSource source) {
#if defined(VN_DEBUG)
    if (!g_heap_guard_suspended) {
        g_frame_alloc_count += 1;
        g_by_source[static_cast<u32>(source)] += 1;
    }
#else
    (void)source;
#endif
}

#if defined(VN_DEBUG)

#include <cstdlib>

// Sin excepciones en este proyecto (-fno-exceptions): un fallo de asignacion aborta en
// vez de lanzar std::bad_alloc.
void* operator new(usize size) {
    if (!g_heap_guard_suspended) {
        g_frame_alloc_count += 1;
        g_by_source[static_cast<u32>(HeapSource::Engine)] += 1;
    }
    void* p = std::malloc(size == 0 ? 1 : size);
    VN_ASSERT(p != nullptr, "operator new: sin memoria");
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

#endif  // VN_DEBUG
