#include "base/heap_guard_hooks.h"

#include <SDL3/SDL.h>

#include <cstdlib>

#include "base/heap_guard.h"
#include "base/log.h"

namespace {
// Se incrementa SIEMPRE, este el guard suspendido o no (a diferencia del contador por
// frame). thread_local por la misma razon que el otro: el hilo de IO tambien pasa por aqui.
thread_local u64 g_lifetime[static_cast<u32>(HeapSource::Count)] = {};

void note(HeapSource source) {
    g_lifetime[static_cast<u32>(source)] += 1;
    heap_guard_count_alloc(source);
}
}  // namespace

u64 heap_guard_lifetime_count(HeapSource source) {
    return g_lifetime[static_cast<u32>(source)];
}

void* heap_guard_malloc(usize size, HeapSource source) {
    note(source);
    return std::malloc(size);
}

void* heap_guard_calloc(usize count, usize size, HeapSource source) {
    note(source);
    return std::calloc(count, size);
}

void* heap_guard_realloc(void* p, usize size, HeapSource source) {
    note(source);
    return std::realloc(p, size);
}

void heap_guard_free(void* p) {
    std::free(p);
}

namespace {

// SDL usa sus propios typedefs (SDL_malloc_func y companeros). Las firmas coinciden con
// las de arriba salvo por size_t vs usize, que son el mismo tipo; se envuelven igualmente
// para no depender de esa coincidencia.
void* SDLCALL sdl_malloc(size_t size) {
    return heap_guard_malloc(size, HeapSource::Sdl);
}

void* SDLCALL sdl_calloc(size_t count, size_t size) {
    return heap_guard_calloc(count, size, HeapSource::Sdl);
}

void* SDLCALL sdl_realloc(void* p, size_t size) {
    return heap_guard_realloc(p, size, HeapSource::Sdl);
}

void SDLCALL sdl_free(void* p) {
    heap_guard_free(p);
}

}  // namespace

void heap_guard_install_sdl_hooks() {
    if (!SDL_SetMemoryFunctions(sdl_malloc, sdl_calloc, sdl_realloc, sdl_free)) {
        // No es fatal: solo significa que las asignaciones de SDL siguen sin contarse.
        // Decirlo en voz alta es mejor que dejar el contador mintiendo en silencio, que es
        // justo el problema que este modulo existe para arreglar.
        log_warn("heap_guard: SDL_SetMemoryFunctions fallo (%s); las asignaciones de SDL "
                 "no se contaran",
                 SDL_GetError());
    }
}
