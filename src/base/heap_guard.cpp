#include "base/heap_guard.h"

#include "base/assert.h"

thread_local u64 g_frame_alloc_count = 0;
// thread_local por la misma razon que g_frame_alloc_count (ver heap_guard.h): aunque hoy
// solo el hilo principal llama a suspend/resume, dejarlo global compartido seria una
// lectura/escritura sin sincronizar desde el punto de vista del hilo de IO en cuanto ese
// hilo exista (M11) — inofensivo en la practica porque su contador nunca se consulta,
// pero es una carrera de datos real bajo el modelo de memoria de C++. Con thread_local
// deja de serlo, sin coste ni cambio de comportamiento observable.
static thread_local bool g_heap_guard_suspended = false;

void heap_guard_reset_frame() {
    g_frame_alloc_count = 0;
}

void heap_guard_check_frame() {
    VN_ASSERT(g_frame_alloc_count == 0, "asignacion de heap dentro del frame");
}

void heap_guard_suspend() {
    g_heap_guard_suspended = true;
}

void heap_guard_resume() {
    g_heap_guard_suspended = false;
}

#if defined(VN_DEBUG)

#include <cstdlib>

// Sin excepciones en este proyecto (-fno-exceptions): un fallo de asignacion aborta en
// vez de lanzar std::bad_alloc.
void* operator new(usize size) {
    if (!g_heap_guard_suspended) {
        g_frame_alloc_count += 1;
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
