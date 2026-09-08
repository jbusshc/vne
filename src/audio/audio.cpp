#include "audio/audio.h"

#define MINIAUDIO_IMPLEMENTATION
// El proyecto no usa el runtime de C de miniaudio para nada mas exotico (sin threads
// propios: reutiliza el device callback interno). Sin excepciones ni RTTI en el resto del
// proyecto (SPEC.md #4); miniaudio es C puro, no le afecta.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4245 4267 4456 4457)
#endif
#include <miniaudio.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#endif

#include "base/hash.h"
#include "base/heap_guard.h"
#include "base/log.h"
#include "base/pool.h"

namespace {

constexpr u32 k_max_sounds       = 64;
constexpr u32 k_max_catalog      = 64;
constexpr u32 k_max_path         = 128;

struct SoundSlot {
    ma_sound sound;
    bool     initialized = false;
};

struct CatalogEntry {
    u16  track_id = 0;
    char path[k_max_path] = {};
};

ma_engine                     g_engine;
bool                          g_engine_ready = false;
Pool<SoundSlot, k_max_sounds> g_pool;

f32 g_bus_volume[static_cast<u32>(Bus::Count)] = {1.0f, 1.0f, 1.0f, 1.0f};

CatalogEntry g_catalog[k_max_catalog];
u32          g_catalog_count = 0;

// Cache de rutas ya cargadas (sfx repetidos, o el mismo track de musica pedido dos
// veces): evita volver a decodificar del disco, que asignaria heap dentro del bucle de
// frame (SPEC.md #4) cada vez que un guion repite un @sfx/@bgm.
struct CacheEntry {
    u32          path_hash = 0;
    SoundHandle  handle{};
};
CacheEntry g_cache[k_max_sounds];
u32        g_cache_count = 0;

SoundHandle g_current_music;

// voice_id empaqueta indice + generacion del Pool en el u32 que SPEC.md #7.3 fija como
// tipo de retorno de audio_play: los 16 bits bajos son indice+1 (0 = voz invalida), los
// 16 altos son los 16 bits bajos de la generacion del slot. Sin la generacion, un
// voice_id viejo podia acabar parando un sonido distinto si su slot se habia liberado y
// reutilizado entre medias — pool_resolve() ya protege de eso para los SoundHandle, y
// esto le da la misma proteccion a las voces.
constexpr u32 k_voice_index_mask = 0xFFFFu;

u32 voice_id_pack(SoundHandle h) {
    return ((h.gen & k_voice_index_mask) << 16) | ((h.index + 1) & k_voice_index_mask);
}

// Devuelve nullptr si el voice_id es 0, esta fuera de rango, o su generacion ya no
// coincide con la del slot (la voz que representaba ya no existe).
SoundSlot* voice_resolve(u32 voice_id) {
    u32 index_plus_one = voice_id & k_voice_index_mask;
    if (index_plus_one == 0 || index_plus_one - 1 >= k_max_sounds) {
        return nullptr;
    }
    SoundHandle h{};
    h.index = index_plus_one - 1;
    h.gen   = g_pool.gens[h.index];
    if ((h.gen & k_voice_index_mask) != ((voice_id >> 16) & k_voice_index_mask)) {
        return nullptr;
    }
    SoundSlot* slot = pool_resolve<SoundTag>(&g_pool, h);
    return (slot != nullptr && slot->initialized) ? slot : nullptr;
}

f32 effective_volume(Bus bus, f32 volume) {
    f32 master = g_bus_volume[static_cast<u32>(Bus::Master)];
    f32 busv   = g_bus_volume[static_cast<u32>(bus)];
    return volume * busv * master;
}

// Escanea assets_src/ogg (copiado al directorio de build igual que las fuentes y los
// PNG, ver CMakeLists.txt) y arma el catalogo {hash del nombre logico -> ruta}. Sin esto
// GameState.bgm_track_id (u16, SPEC.md #8.2, no puede ser un puntero ni un text_id de un
// guion concreto) no podria resolverse de vuelta a un archivo tras cargar una partida
// creada por un guion distinto al que esta cargado ahora mismo (ver ADR de M6).
void scan_music_catalog() {
    g_catalog_count = 0;
#if defined(_WIN32)
    WIN32_FIND_DATAA find_data;
    HANDLE           find = FindFirstFileA("assets_src/ogg/*", &find_data);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if (find_data.cFileName[0] == '.') {
            continue;
        }
        if (g_catalog_count >= k_max_catalog) {
            break;
        }
        char name_no_ext[k_max_path] = {};
        std::snprintf(name_no_ext, sizeof(name_no_ext), "%s", find_data.cFileName);
        char* dot = std::strrchr(name_no_ext, '.');
        if (dot != nullptr) {
            *dot = '\0';
        }
        CatalogEntry& entry = g_catalog[g_catalog_count];
        entry.track_id      = static_cast<u16>(fnv1a_u32(name_no_ext) % 65536u);
        std::snprintf(entry.path, sizeof(entry.path), "assets_src/ogg/%s", find_data.cFileName);
        g_catalog_count += 1;
    } while (FindNextFileA(find, &find_data));
    FindClose(find);
#else
    DIR* dir = opendir("assets_src/ogg");
    if (dir == nullptr) {
        return;
    }
    struct dirent* entry_dir;
    while ((entry_dir = readdir(dir)) != nullptr && g_catalog_count < k_max_catalog) {
        if (entry_dir->d_name[0] == '.') {
            continue;
        }
        char name_no_ext[k_max_path] = {};
        std::snprintf(name_no_ext, sizeof(name_no_ext), "%s", entry_dir->d_name);
        char* dot = std::strrchr(name_no_ext, '.');
        if (dot != nullptr) {
            *dot = '\0';
        }
        CatalogEntry& entry = g_catalog[g_catalog_count];
        entry.track_id      = static_cast<u16>(fnv1a_u32(name_no_ext) % 65536u);
        std::snprintf(entry.path, sizeof(entry.path), "assets_src/ogg/%s", entry_dir->d_name);
        g_catalog_count += 1;
    }
    closedir(dir);
#endif
}

SoundHandle cache_find(u32 path_hash) {
    for (u32 i = 0; i < g_cache_count; ++i) {
        if (g_cache[i].path_hash == path_hash) {
            return g_cache[i].handle;
        }
    }
    return SoundHandle{};
}

void cache_insert(u32 path_hash, SoundHandle h) {
    if (g_cache_count < k_max_sounds) {
        g_cache[g_cache_count] = CacheEntry{path_hash, h};
        g_cache_count += 1;
    }
}

}  // namespace

void audio_init() {
    ma_result result = ma_engine_init(nullptr, &g_engine);
    if (result != MA_SUCCESS) {
        log_error("audio_init: ma_engine_init fallo (%d)", static_cast<int>(result));
        g_engine_ready = false;
        return;
    }
    g_engine_ready = true;
    pool_init(&g_pool);
    scan_music_catalog();
}

void audio_shutdown() {
    if (!g_engine_ready) {
        return;
    }
    for (u32 i = 0; i < k_max_sounds; ++i) {
        if (g_pool.items[i].initialized) {
            ma_sound_uninit(&g_pool.items[i].sound);
        }
    }
    ma_engine_uninit(&g_engine);
    g_engine_ready = false;
}

void audio_update(f32 dt, f32* out_bgm_position) {
    (void)dt;  // miniaudio mezcla y aplica los fundidos en su propio hilo de dispositivo;
               // no hace falta ningun bombeo manual por frame, solo leer la posicion.
    if (out_bgm_position != nullptr) {
        *out_bgm_position = audio_music_position();
    }
}

AudioLoadResult audio_load(const char* path, bool streaming, SoundHandle* out) {
    *out = SoundHandle{};
    if (!g_engine_ready) {
        return AudioLoadResult::NotFound;
    }

    u32         path_hash = fnv1a_u32(path);
    SoundHandle cached     = cache_find(path_hash);
    if (cached.valid()) {
        *out = cached;
        return AudioLoadResult::Ok;
    }

    // Comprobar que el archivo existe antes de llamar a miniaudio, no despues: pedirle a
    // ma_sound_init_from_file que falle sobre un archivo inexistente dispara un
    // use-after-free real dentro de su gestor de recursos (miniaudio 0.11.21,
    // ma_resource_manager_data_buffer_node_acquire — confirmado con ASan, no es un bug de
    // este proyecto). Evitar la ruta de fallo por completo es mas simple y fiable que
    // reportarlo rio arriba (ver ADR de M6).
    std::FILE* probe = std::fopen(path, "rb");
    if (probe == nullptr) {
        log_error("audio_load: no se encontro '%s'", path);
        return AudioLoadResult::NotFound;
    }
    std::fclose(probe);

    SoundSlot*  slot   = nullptr;
    SoundHandle handle = pool_acquire<SoundTag>(&g_pool, &slot);
    if (!handle.valid()) {
        return AudioLoadResult::OutOfSlots;
    }

    // Cargar/decodificar un archivo asigna heap por como funciona miniaudio (igual que
    // ejecutar Lua, ADR-0032): la primera vez que un @sfx/@bgm nuevo se dispara dentro
    // del bucle de frame, este es el unico otro punto exceptuado de la regla de cero heap
    // (SPEC.md #4). Cargas repetidas del mismo path usan la cache de arriba y no llegan
    // aqui.
    heap_guard_suspend();
    u32 flags = streaming ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
    ma_result result =
        ma_sound_init_from_file(&g_engine, path, flags, nullptr, nullptr, &slot->sound);
    heap_guard_resume();

    if (result != MA_SUCCESS) {
        pool_release<SoundTag>(&g_pool, handle);
        log_error("audio_load: no se pudo cargar '%s' (%d)", path, static_cast<int>(result));
        return AudioLoadResult::NotFound;
    }
    slot->initialized = true;
    cache_insert(path_hash, handle);
    *out = handle;
    return AudioLoadResult::Ok;
}

u32 audio_play(SoundHandle s, Bus bus, f32 volume, bool loop) {
    SoundSlot* slot = pool_resolve<SoundTag>(&g_pool, s);
    if (slot == nullptr || !slot->initialized) {
        return 0;
    }
    ma_sound_seek_to_pcm_frame(&slot->sound, 0);
    ma_sound_set_volume(&slot->sound, effective_volume(bus, volume));
    ma_sound_set_looping(&slot->sound, loop ? MA_TRUE : MA_FALSE);
    ma_sound_start(&slot->sound);
    return voice_id_pack(s);
}

void audio_stop(u32 voice_id, f32 fade_seconds) {
    SoundSlot* slot = voice_resolve(voice_id);
    if (slot == nullptr) {
        return;
    }
    if (fade_seconds <= 0.0f) {
        ma_sound_stop(&slot->sound);
        return;
    }
    ma_uint64 fade_frames =
        static_cast<ma_uint64>(fade_seconds * static_cast<f32>(ma_engine_get_sample_rate(&g_engine)));
    ma_sound_set_fade_in_pcm_frames(&slot->sound, -1.0f, 0.0f, fade_frames);
    ma_sound_set_stop_time_in_pcm_frames(
        &slot->sound, ma_engine_get_time_in_pcm_frames(&g_engine) + fade_frames);
}

void audio_set_bus_volume(Bus b, f32 v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    g_bus_volume[static_cast<u32>(b)] = v;
}

void audio_crossfade_music(SoundHandle next, f32 seconds) {
    if (g_current_music.valid()) {
        audio_stop(voice_id_pack(g_current_music), seconds);
    }

    SoundSlot* slot = pool_resolve<SoundTag>(&g_pool, next);
    if (slot == nullptr || !slot->initialized) {
        return;
    }
    ma_uint64 fade_frames =
        static_cast<ma_uint64>(seconds * static_cast<f32>(ma_engine_get_sample_rate(&g_engine)));
    ma_sound_seek_to_pcm_frame(&slot->sound, 0);
    ma_sound_set_looping(&slot->sound, MA_TRUE);
    f32 target_volume = effective_volume(Bus::Music, 1.0f);
    ma_sound_set_volume(&slot->sound, 0.0f);
    ma_sound_start(&slot->sound);
    ma_sound_set_fade_in_pcm_frames(&slot->sound, 0.0f, target_volume, fade_frames);
    g_current_music = next;
}

void audio_stop_music(f32 fade_seconds) {
    if (!g_current_music.valid()) {
        return;
    }
    audio_stop(voice_id_pack(g_current_music), fade_seconds);
    g_current_music = SoundHandle{};
}

f32 audio_music_position() {
    SoundSlot* slot = pool_resolve<SoundTag>(&g_pool, g_current_music);
    if (slot == nullptr || !slot->initialized) {
        return 0.0f;
    }
    f32 cursor = 0.0f;
    ma_sound_get_cursor_in_seconds(&slot->sound, &cursor);
    return cursor;
}

void audio_seek_music(f32 seconds) {
    SoundSlot* slot = pool_resolve<SoundTag>(&g_pool, g_current_music);
    if (slot == nullptr || !slot->initialized) {
        return;
    }
    ma_uint64 frame =
        static_cast<ma_uint64>(seconds * static_cast<f32>(ma_engine_get_sample_rate(&g_engine)));
    ma_sound_seek_to_pcm_frame(&slot->sound, frame);
}

bool audio_load_track(u16 track_id, SoundHandle* out) {
    for (u32 i = 0; i < g_catalog_count; ++i) {
        if (g_catalog[i].track_id == track_id) {
            return audio_load(g_catalog[i].path, true, out) == AudioLoadResult::Ok;
        }
    }
    log_error("audio_load_track: track_id %u no esta en el catalogo (assets_src/ogg)", track_id);
    return false;
}
