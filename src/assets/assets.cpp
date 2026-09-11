#include "assets/assets.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>

#include "assets/pak.h"
#include "audio/audio.h"
#include "base/hash.h"
#include "base/heap_guard.h"
#include "base/log.h"
#include "gfx/texture.h"
#include "text/font.h"

namespace {

// Capacidad de las colas y de la cache. Fijas y pequenas a proposito: son arrays estaticos,
// sin heap (skill vne-memory-model). k_max_in_flight acota a la vez las peticiones
// encoladas y las terminaciones pendientes de integrar, asi que ninguna de las dos colas
// puede desbordarse: assets_texture() rechaza encolar si ya hay k_max_in_flight en vuelo.
constexpr u32 k_max_in_flight  = 64;
constexpr u32 k_max_cache      = 256;
constexpr u32 k_max_name       = 128;

struct LoadRequest {
    char          logical_name[k_max_name] = {};
    TextureHandle handle;
};

struct LoadCompletion {
    TextureHandle handle;
    const u8*     data  = nullptr;  // nullptr si la lectura fallo
    usize         size  = 0;
    bool          owned = false;    // si es true, hay que pak_release() tras usarlo
};

struct CacheEntry {
    u64           name_hash = 0;
    TextureHandle handle;
};

// --- Estado compartido entre hilos, todo bajo g_mutex ---
SDL_Mutex*     g_mutex     = nullptr;
SDL_Condition* g_wake_io   = nullptr;  // el hilo de IO duerme aqui cuando no hay trabajo
SDL_Thread*    g_io_thread = nullptr;
bool           g_running   = false;

LoadRequest g_requests[k_max_in_flight];
u32         g_request_head  = 0;  // proxima a sacar
u32         g_request_count = 0;

LoadCompletion g_completions[k_max_in_flight];
u32            g_completion_head  = 0;
u32            g_completion_count = 0;

// Peticiones aceptadas que todavia no ha integrado assets_process_completed_loads().
// Acota las dos colas de golpe (ver k_max_in_flight).
u32 g_in_flight = 0;

// --- Solo hilo principal, sin mutex ---
CacheEntry g_cache[k_max_cache];
u32        g_cache_count = 0;

TextureHandle cache_find(u64 name_hash) {
    for (u32 i = 0; i < g_cache_count; ++i) {
        if (g_cache[i].name_hash == name_hash) {
            return g_cache[i].handle;
        }
    }
    return TextureHandle{};
}

void cache_insert(u64 name_hash, TextureHandle handle, const char* logical_name) {
    if (g_cache_count >= k_max_cache) {
        log_error("assets: cache de texturas llena (%u), '%s' se recargara cada vez que se "
                  "pida", k_max_cache, logical_name);
        return;
    }
    g_cache[g_cache_count] = CacheEntry{name_hash, handle};
    g_cache_count += 1;
}

// Cuerpo del hilo de IO: espera trabajo, lee bytes, deja el resultado en la cola de
// terminaciones. No decodifica ni toca la GPU (ver assets.h), y NO toca ninguna arena del
// proyecto: pak_resolve() usa el allocador de SDL precisamente por eso.
int io_thread_main(void* /*userdata*/) {
    for (;;) {
        LoadRequest req;

        SDL_LockMutex(g_mutex);
        while (g_request_count == 0 && g_running) {
            SDL_WaitCondition(g_wake_io, g_mutex);
        }
        if (!g_running) {
            SDL_UnlockMutex(g_mutex);
            break;
        }
        req = g_requests[g_request_head];
        g_request_head = (g_request_head + 1) % k_max_in_flight;
        g_request_count -= 1;
        SDL_UnlockMutex(g_mutex);

        // La parte lenta, fuera del mutex a proposito: mientras esto ocurre el hilo
        // principal sigue dibujando frames sin bloquearse.
        const u8* data  = nullptr;
        usize     size  = 0;
        bool      owned = false;
        if (!pak_resolve(req.logical_name, &data, &size, &owned)) {
            data = nullptr;
            size = 0;
        }

        SDL_LockMutex(g_mutex);
        u32 tail = (g_completion_head + g_completion_count) % k_max_in_flight;
        g_completions[tail] = LoadCompletion{req.handle, data, size, owned};
        g_completion_count += 1;
        SDL_UnlockMutex(g_mutex);
    }
    return 0;
}

}  // namespace

void assets_init() {
    if (g_io_thread != nullptr) {
        return;
    }
    g_mutex   = SDL_CreateMutex();
    g_wake_io = SDL_CreateCondition();
    if (g_mutex == nullptr || g_wake_io == nullptr) {
        log_error("assets_init: no se pudo crear el mutex/condicion del hilo de IO");
        return;
    }
    g_running   = true;
    g_io_thread = SDL_CreateThread(io_thread_main, "vne_asset_io", nullptr);
    if (g_io_thread == nullptr) {
        log_error("assets_init: no se pudo crear el hilo de IO; las cargas seran sincronas");
        g_running = false;
    }
}

void assets_shutdown() {
    if (g_io_thread != nullptr) {
        SDL_LockMutex(g_mutex);
        g_running = false;
        SDL_SignalCondition(g_wake_io);
        SDL_UnlockMutex(g_mutex);
        SDL_WaitThread(g_io_thread, nullptr);
        g_io_thread = nullptr;
    }

    // Liberar lo que quedo leido pero sin integrar: en backend suelto esos buffers son
    // del allocador de SDL y nadie mas los va a soltar.
    for (u32 i = 0; i < g_completion_count; ++i) {
        const LoadCompletion& c = g_completions[(g_completion_head + i) % k_max_in_flight];
        pak_release(c.data, c.owned);
    }
    g_completion_count = 0;
    g_completion_head  = 0;
    g_request_count    = 0;
    g_request_head     = 0;
    g_in_flight        = 0;
    g_cache_count      = 0;

    if (g_wake_io != nullptr) {
        SDL_DestroyCondition(g_wake_io);
        g_wake_io = nullptr;
    }
    if (g_mutex != nullptr) {
        SDL_DestroyMutex(g_mutex);
        g_mutex = nullptr;
    }
}

TextureHandle assets_texture(const char* logical_name) {
    u64           hash   = fnv1a_u64(logical_name);
    TextureHandle cached = cache_find(hash);
    if (cached.valid()) {
        return cached;
    }

    // Comprobar el hueco en la cola ANTES de reservar el slot de textura: al reves, cada
    // rechazo por cola llena dejaria un slot del pool reservado que nadie va a usar ni
    // liberar (no hay texture_release: los slots viven lo que el proceso).
    //
    // Comprobar-y-luego-actuar sin mantener el mutex es correcto aqui por quien escribe
    // cada cosa: assets_texture() solo se llama desde el hilo principal (reserva slots de
    // textura, que es de este hilo por definicion), el hilo de IO solo VACIA la cola de
    // peticiones, y g_in_flight solo lo decrementa el propio hilo principal al integrar.
    // Nadie puede llenar la cola entre esta comprobacion y el encolado de mas abajo.
    if (g_io_thread != nullptr) {
        SDL_LockMutex(g_mutex);
        bool queue_full = g_in_flight >= k_max_in_flight;
        SDL_UnlockMutex(g_mutex);
        if (queue_full) {
            log_error("assets_texture: %u cargas en vuelo, '%s' se queda sin cargar",
                      k_max_in_flight, logical_name);
            return TextureHandle{};  // invalido: el llamante dibuja su propio placeholder
        }
    }

    // El handle se reserva YA, apuntando al placeholder: es lo que hace que esta funcion
    // pueda devolver algo dibujable sin haber tocado el disco (criterio de M11).
    TextureHandle handle = texture_reserve_placeholder();

    if (g_io_thread == nullptr) {
        // Sin hilo de IO (no se pudo crear, o assets_init() no se llamo): cargar de forma
        // sincrona es peor para el frame time, pero mucho mejor que no cargar nada.
        const u8* data  = nullptr;
        usize     size  = 0;
        bool      owned = false;
        if (pak_resolve(logical_name, &data, &size, &owned)) {
            texture_finish_load_from_memory(handle, data, size);
            pak_release(data, owned);
        } else {
            log_error("assets_texture: no se encontro '%s'", logical_name);
        }
        cache_insert(hash, handle, logical_name);
        return handle;
    }

    SDL_LockMutex(g_mutex);
    u32          tail = (g_request_head + g_request_count) % k_max_in_flight;
    LoadRequest& req  = g_requests[tail];
    std::snprintf(req.logical_name, sizeof(req.logical_name), "%s", logical_name);
    req.handle = handle;
    g_request_count += 1;
    g_in_flight += 1;
    SDL_SignalCondition(g_wake_io);
    SDL_UnlockMutex(g_mutex);

    cache_insert(hash, handle, logical_name);
    return handle;
}

FontHandle assets_font(const char* logical_name, u32 px_size) {
    return text_load_font(logical_name, px_size);
}

SoundHandle assets_sound(const char* logical_name, bool streaming) {
    SoundHandle handle{};
    audio_load(logical_name, streaming, &handle);
    return handle;
}

void assets_process_completed_loads() {
    // UNA por llamada, no "drena todo lo que haya". Medido en tests/test_assets.cpp:
    // integrar el atlas de prueba (decodificar el QOI y subirlo a la GPU) cuesta ~10 ms,
    // un 62% del presupuesto de 16.6 ms de un frame a 60 Hz. Dos en el mismo frame ya se
    // pasarian, asi que las demas esperan al siguiente: siguen leidas en memoria, solo
    // falta integrarlas, y su textura muestra el placeholder entretanto. Frame times
    // planos antes que throughput de carga (SPEC.md #1, prioridad 1).
    constexpr u32 k_max_integrations_per_call = 1;

    for (u32 done = 0; done < k_max_integrations_per_call; ++done) {
        LoadCompletion completion;

        SDL_LockMutex(g_mutex);
        if (g_completion_count == 0) {
            SDL_UnlockMutex(g_mutex);
            break;
        }
        completion        = g_completions[g_completion_head];
        g_completion_head = (g_completion_head + 1) % k_max_in_flight;
        g_completion_count -= 1;
        g_in_flight -= 1;
        SDL_UnlockMutex(g_mutex);

        // Decodificar y subir a la GPU, fuera del mutex y siempre en el hilo principal
        // (sokol_gfx no es thread-safe). Si la lectura fallo, el slot se queda con el
        // placeholder que ya tenia: nunca es fatal (SPEC.md #4).
        if (completion.data != nullptr) {
            // Misma excepcion a la regla de cero heap que ADR-0035, generalizada de audio
            // a cualquier asset: decodificar un asset recien cargado asigna dentro de
            // codigo de terceros (alli miniaudio al abrir un OGG, aqui qoi al decodificar
            // la imagen) y no hay forma de evitarlo sin reescribir la libreria. Es carga,
            // no trabajo de frame: ocurre una vez por asset, no en cada frame.
            //
            // Antes de M12 esto no se veia — qoi asigna con malloc y el contador solo
            // miraba operator new —, asi que la excepcion no estaba escrita aunque el
            // comportamiento ya era este desde M11. Medido: 1 asignacion de qoi + 2 de
            // SDL por textura integrada.
            heap_guard_suspend();
            texture_finish_load_from_memory(completion.handle, completion.data,
                                             completion.size);
            pak_release(completion.data, completion.owned);
            heap_guard_resume();
        }
    }
}

u32 assets_pending_count() {
    if (g_mutex == nullptr) {
        return 0;
    }
    SDL_LockMutex(g_mutex);
    u32 count = g_in_flight;
    SDL_UnlockMutex(g_mutex);
    return count;
}

bool assets_reload_texture(const char* logical_name) {
    TextureHandle handle = cache_find(fnv1a_u64(logical_name));
    if (!handle.valid()) {
        return false;  // nunca se pidio: no hay nada que recargar
    }

    const u8* data  = nullptr;
    usize     size  = 0;
    bool      owned = false;
    if (!pak_resolve(logical_name, &data, &size, &owned)) {
        log_error("assets_reload_texture: no se encontro '%s'", logical_name);
        return false;
    }
    bool ok = texture_finish_load_from_memory(handle, data, size);
    pak_release(data, owned);
    if (ok) {
        log_info("assets_reload_texture: '%s' recargado en caliente", logical_name);
    }
    return ok;
}
