#include "base/heap_guard.h"

#include "base/assert.h"

u64 g_frame_alloc_count = 0;
static bool g_heap_guard_suspended = false;

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
