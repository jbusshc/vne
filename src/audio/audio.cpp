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

#include "vfs/pak.h"
#include "core/hash.h"
#include "core/heap_guard.h"
#include "core/heap_guard_hooks.h"
#include "core/log.h"
#include "core/pool.h"
#include "platform/files.h"

namespace {

constexpr u32 k_max_sounds       = 64;
constexpr u32 k_max_catalog      = 64;
constexpr u32 k_max_path         = 128;

// Polifonia (M12). Un efecto necesita poder sonar varias veces superpuesto consigo mismo;
// un ma_sound es UNA instancia con UNA posicion de reproduccion, asi que hacen falta N
// objetos ma_sound distintos por sonido, no uno.
//
// Se crean todos en audio_load, nunca en audio_play: crear un ma_sound asigna heap dentro
// de miniaudio, y audio_load ya esta cubierto por la excepcion de ADR-0035 ("primera carga
// de un sonido") mientras que audio_play corre en pleno bucle de frame y no puede asignar.
// Ver el ADR de M12 para por que NO se usa ma_sound_init_copy, que seria lo evidente.
constexpr u32 k_voices_per_sound = 8;    // criterio de M12: 5 simultaneas; 8 deja margen
constexpr u32 k_max_voices       = 128;  // 16 efectos distintos con polifonia completa

// Una voz es una instancia reproducible independiente de un sonido concreto. Se asigna a
// su sonido en audio_load y ya no cambia de dueño: el array es un bump allocator sin
// liberacion, igual que g_cache, porque un sonido cargado vive lo que vive el proceso.
struct Voice {
    ma_sound   sound;
    ma_decoder decoder;      // solo backend empaquetado, ver audio_load
    bool       has_decoder = false;
    bool       initialized = false;
    // Contador de reproducciones de ESTA voz, empaquetado en el voice_id. Sin el, un
    // voice_id viejo podria parar una reproduccion posterior de la misma voz (que con
    // polifonia y reutilizacion de voces deja de ser un caso teorico).
    u32        play_gen = 0;
};

Voice g_voices[k_max_voices];
u32   g_voice_count = 0;

struct SoundSlot {
    // Solo se usa para musica (streaming): un unico ma_sound, sin polifonia. Los efectos
    // no tocan este campo, viven enteros en g_voices.
    ma_sound sound;
    bool     initialized = false;
    // Solo se usa en backend empaquetado (M11, ver audio_load): ma_sound_init_from_file
    // sigue siendo el camino en backend suelto (ADR-0036 depende de una ruta de archivo
    // real, no se toca). El decoder tiene que sobrevivir tanto como sound -- lo referencia
    // como su ma_data_source, igual que raw_bytes en text/font_internal.h con FreeType.
    ma_decoder decoder;
    bool       has_decoder = false;

    // Voces propias (efectos). voice_count == 0 significa "sonido de musica, usa `sound`".
    u32 first_voice = 0;
    u32 voice_count = 0;
    u32 next_voice  = 0;  // round-robin para robar cuando las k_voices_per_sound suenan
};

struct CatalogEntry {
    u16  track_id = 0;
    char path[k_max_path] = {};
};

// Contador de asignaciones hechas por miniaudio (M12), aparte del de heap_guard. El de
// heap_guard se resetea en cada frame y sirve para la regla de cero heap (SPEC.md #4);
// este es acumulativo desde audio_init() para poder medir una operacion concreta en un
// test, sin depender de que haya un bucle de frame corriendo. Los hooks alimentan a los
// dos. Ver ADR-0056/ADR-0057.
u64 g_ma_alloc_count = 0;

void* ma_malloc_counted(size_t size, void* /*user*/) {
    g_ma_alloc_count += 1;
    return heap_guard_malloc(size, HeapSource::MiniAudio);
}

void* ma_realloc_counted(void* p, size_t size, void* /*user*/) {
    g_ma_alloc_count += 1;
    return heap_guard_realloc(p, size, HeapSource::MiniAudio);
}

void ma_free_counted(void* p, void* /*user*/) {
    heap_guard_free(p);
}

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

// voice_id es el u32 que SPEC.md #7.3 fija como tipo de retorno de audio_play. Desde M12
// referencia a uno de DOS espacios distintos, porque musica y efectos ya no se reproducen
// por el mismo camino, y el bit alto dice a cual:
//
//   bit 31     1 = voz de efecto (indice en g_voices), 0 = sonido de musica (slot del Pool)
//   bits 16-30 generacion, 15 bits (del Pool para musica, de reproduccion para efectos)
//   bits 0-15  indice + 1 (0 = voice_id invalido)
//
// La generacion es lo que impide que un voice_id viejo pare el sonido equivocado cuando
// su hueco se reutiliza. Bajo de 16 a 15 bits para dejar sitio a la etiqueta: el periodo
// de vuelta sigue siendo 32768 reproducciones, muy por encima de cualquier uso real.
constexpr u32 k_voice_index_mask = 0xFFFFu;
constexpr u32 k_voice_gen_shift  = 16;
constexpr u32 k_voice_gen_mask   = 0x7FFFu;
constexpr u32 k_voice_sfx_flag   = 0x80000000u;

u32 voice_id_pack(SoundHandle h) {
    return ((h.gen & k_voice_gen_mask) << k_voice_gen_shift) |
           ((h.index + 1) & k_voice_index_mask);
}

u32 voice_id_pack_sfx(u32 voice_index, u32 play_gen) {
    return k_voice_sfx_flag | ((play_gen & k_voice_gen_mask) << k_voice_gen_shift) |
           ((voice_index + 1) & k_voice_index_mask);
}

// Devuelve el ma_sound al que apunta un voice_id, o nullptr si es 0, esta fuera de rango,
// o su generacion ya no coincide (la reproduccion que representaba ya termino o fue
// sustituida). Devuelve el ma_sound y no el slot porque los dos espacios no comparten
// contenedor.
ma_sound* voice_resolve(u32 voice_id) {
    u32 index_plus_one = voice_id & k_voice_index_mask;
    if (index_plus_one == 0) {
        return nullptr;
    }
    u32 index = index_plus_one - 1;
    u32 gen   = (voice_id >> k_voice_gen_shift) & k_voice_gen_mask;

    if ((voice_id & k_voice_sfx_flag) != 0) {
        if (index >= k_max_voices) {
            return nullptr;
        }
        Voice& v = g_voices[index];
        if (!v.initialized || v.play_gen != gen) {
            return nullptr;
        }
        return &v.sound;
    }

    if (index >= k_max_sounds) {
        return nullptr;
    }
    SoundHandle h{};
    h.index = index;
    h.gen   = g_pool.gens[index];
    if ((h.gen & k_voice_gen_mask) != gen) {
        return nullptr;
    }
    SoundSlot* slot = pool_resolve<SoundTag>(&g_pool, h);
    return (slot != nullptr && slot->initialized) ? &slot->sound : nullptr;
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
//
// M11: el listado de directorio ya no tiene su propio #if _WIN32/#else -- vive en
// platform/files.h (SPEC.md #2, deuda senalada explicitamente ahi: "audio.cpp enumera un
// directorio con la API del sistema en lugar de hacerlo a traves de platform/").
void dir_list_callback_music(void* /*userdata*/, const char* name_no_ext,
                              const char* full_name) {
    if (g_catalog_count >= k_max_catalog) {
        return;
    }
    CatalogEntry& entry = g_catalog[g_catalog_count];
    entry.track_id      = static_cast<u16>(fnv1a_u32(name_no_ext) % 65536u);
    // full_name conserva la extension real del archivo (hoy .wav para los tonos de
    // prueba, .ogg en principio para musica de verdad): no se puede asumir una sola,
    // por eso dir_list_by_extension recibe filtro nullptr (todo el directorio) y aqui se
    // usa el nombre tal cual llega, igual que hacia el codigo Win32/dirent original.
    // M11: nombre logico ("ogg/..."), no ruta de archivo literal -- audio_load lo resuelve
    // a traves del backend activo (ver vfs/pak.h), igual que el catalogo empaquetado
    // de mas arriba.
    std::snprintf(entry.path, sizeof(entry.path), "ogg/%s", full_name);
    g_catalog_count += 1;
}

// Debe coincidir con tools/bake/main.cpp (duplicado a proposito, mismo patron que el
// resto de formatos de este proyecto -- ver el comentario junto a bake_pack()).
struct OggCatalogRecord {
    u16  track_id;
    u8   _pad[2];
    char logical_name[64];
};
static_assert(sizeof(OggCatalogRecord) == 68);

void scan_music_catalog() {
    g_catalog_count = 0;

    // M11: en backend empaquetado no hay directorio que escanear (solo existe game.pak),
    // asi que el catalogo se lee ya horneado desde "ogg_catalog.bin" (lo escribe
    // sz_bake pack). En backend suelto se sigue escaneando assets_src/ogg/ en runtime,
    // sin cambios de comportamiento respecto a antes de M11.
    if (pak_is_packed()) {
        const u8* bytes = nullptr;
        usize     size  = 0;
        bool      owned = false;
        if (!pak_resolve("ogg_catalog.bin", &bytes, &size, &owned)) {
            log_error("scan_music_catalog: 'ogg_catalog.bin' no esta en el pak montado");
            return;
        }
        u32 count = static_cast<u32>(size / sizeof(OggCatalogRecord));
        const OggCatalogRecord* records = reinterpret_cast<const OggCatalogRecord*>(bytes);
        for (u32 i = 0; i < count && g_catalog_count < k_max_catalog; ++i) {
            CatalogEntry& entry = g_catalog[g_catalog_count];
            entry.track_id      = records[i].track_id;
            std::snprintf(entry.path, sizeof(entry.path), "%s", records[i].logical_name);
            g_catalog_count += 1;
        }
        pak_release(bytes, owned);
        return;
    }

    dir_list_by_extension("assets_src/ogg", nullptr, dir_list_callback_music, nullptr);
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

// Inicializa UNA instancia reproducible (M11 dejo dos caminos segun el backend activo,
// porque miniaudio no tiene una sola API que sirva para los dos; M12 los extrae aqui para
// poder repetirlos por voz sin duplicar el codigo).
//
// `loose_path != nullptr` es backend suelto; si es nullptr se decodifica desde
// (packed_bytes, packed_size). Asume que el llamante ya suspendio el heap_guard
// (ADR-0035) y, en suelto, ya comprobo con fopen que el archivo existe (ADR-0036).
ma_result sound_instance_init(const char* loose_path, const u8* packed_bytes,
                               usize packed_size, bool streaming, ma_sound* out_sound,
                               ma_decoder* out_decoder, bool* out_has_decoder) {
    *out_has_decoder = false;

    if (loose_path != nullptr) {
        u32 flags = streaming ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
        return ma_sound_init_from_file(&g_engine, loose_path, flags, nullptr, nullptr,
                                        out_sound);
    }

    // Backend empaquetado: no hay ruta de archivo real que darle a miniaudio, asi que se
    // decodifica desde el puntero que ya vive dentro del .pak montado en memoria
    // (pak_resolve nunca devuelve owned=true en este backend -- el puntero vive tanto como
    // el proceso, igual que el .pak entero, asi que el decoder puede referenciarlo sin que
    // nadie gestione su vida util por separado).
    //
    // Sin flags de streaming/decode aqui (esos son de MA_RESOURCE_MANAGER_DATA_SOURCE_
    // FLAG_*, solo se aplican via ma_sound_init_from_file): un ma_decoder ya decodifica de
    // forma incremental por si mismo.
    ma_decoder_config cfg    = ma_decoder_config_init_default();
    ma_result         result = ma_decoder_init_memory(packed_bytes, packed_size, &cfg,
                                                       out_decoder);
    if (result != MA_SUCCESS) {
        return result;
    }
    *out_has_decoder = true;
    result = ma_sound_init_from_data_source(&g_engine, out_decoder, 0, nullptr, out_sound);
    if (result != MA_SUCCESS) {
        ma_decoder_uninit(out_decoder);
        *out_has_decoder = false;
    }
    return result;
}

// Crea hasta k_voices_per_sound instancias independientes para un efecto (M12). Devuelve
// MA_SUCCESS si consiguio al menos una: quedarse corto de voces degrada la polifonia
// (menos simultaneas del mismo efecto), pero nunca impide que el efecto suene.
ma_result voices_init_for_sound(SoundSlot* slot, const char* loose_path,
                                 const u8* packed_bytes, usize packed_size) {
    slot->first_voice = g_voice_count;
    slot->voice_count = 0;
    slot->next_voice  = 0;

    ma_result last = MA_ERROR;
    for (u32 i = 0; i < k_voices_per_sound && g_voice_count < k_max_voices; ++i) {
        Voice& v = g_voices[g_voice_count];
        last = sound_instance_init(loose_path, packed_bytes, packed_size, false, &v.sound,
                                    &v.decoder, &v.has_decoder);
        if (last != MA_SUCCESS) {
            break;
        }
        v.initialized  = true;
        v.play_gen     = 0;
        g_voice_count += 1;
        slot->voice_count += 1;
    }

    if (slot->voice_count == 0) {
        return last;
    }
    if (slot->voice_count < k_voices_per_sound) {
        log_warn("audio: solo %u de %u voces para un efecto (g_voices lleno o fallo de "
                 "miniaudio); sonara con menos copias simultaneas",
                 slot->voice_count, k_voices_per_sound);
    }
    return MA_SUCCESS;
}

}  // namespace

void audio_init() {
    // Los callbacks de asignacion se instalan en el engine, y de ahi los hereda su gestor
    // de recursos interno: cubren tanto ma_sound_init_from_file como ma_decoder_*.
    ma_engine_config cfg                 = ma_engine_config_init();
    cfg.allocationCallbacks.pUserData    = nullptr;
    cfg.allocationCallbacks.onMalloc     = ma_malloc_counted;
    cfg.allocationCallbacks.onRealloc    = ma_realloc_counted;
    cfg.allocationCallbacks.onFree       = ma_free_counted;

    ma_result result = ma_engine_init(&cfg, &g_engine);
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
    // Voces de efectos (M12) primero: son ma_sound independientes, no dependen de ningun
    // slot del Pool y se apagan igual.
    for (u32 i = 0; i < g_voice_count; ++i) {
        if (!g_voices[i].initialized) {
            continue;
        }
        ma_sound_uninit(&g_voices[i].sound);
        // El sound tiene que dejar de referenciar al decoder ANTES de liberarlo (orden de
        // destruccion, mismo principio que face/raw_bytes en font.cpp).
        if (g_voices[i].has_decoder) {
            ma_decoder_uninit(&g_voices[i].decoder);
        }
        g_voices[i].initialized = false;
        g_voices[i].has_decoder = false;
    }
    g_voice_count = 0;

    for (u32 i = 0; i < k_max_sounds; ++i) {
        // Solo la musica tiene un ma_sound propio en el slot; un efecto lo tiene todo en
        // g_voices y aqui no hay nada que apagar (voice_count > 0).
        if (g_pool.items[i].initialized && g_pool.items[i].voice_count == 0) {
            ma_sound_uninit(&g_pool.items[i].sound);
            if (g_pool.items[i].has_decoder) {
                ma_decoder_uninit(&g_pool.items[i].decoder);
            }
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

AudioLoadResult audio_load(const char* logical_name, bool streaming, SoundHandle* out) {
    *out = SoundHandle{};
    if (!g_engine_ready) {
        return AudioLoadResult::NotFound;
    }

    u32         path_hash = fnv1a_u32(logical_name);
    SoundHandle cached     = cache_find(path_hash);
    if (cached.valid()) {
        *out = cached;
        return AudioLoadResult::Ok;
    }

    SoundSlot*  slot   = nullptr;
    SoundHandle handle = pool_acquire<SoundTag>(&g_pool, &slot);
    if (!handle.valid()) {
        return AudioLoadResult::OutOfSlots;
    }

    // M11: dos caminos segun el backend activo (ver vfs/pak.h), porque miniaudio no
    // tiene una unica API que sirva para los dos. M12 los resuelve una sola vez aqui y
    // deja que sound_instance_init() elija, porque un efecto crea N instancias y no una.
    char        loose_buf[512];
    const char* loose_path = pak_resolve_loose_path(logical_name, loose_buf,
                                                     sizeof(loose_buf))
                                 ? loose_buf
                                 : nullptr;
    const u8* bytes = nullptr;
    usize     size  = 0;

    if (loose_path != nullptr) {
        // ADR-0036 depende de esta comprobacion con fopen antes de llamar a miniaudio:
        // pedirle a ma_sound_init_from_file que falle sobre un archivo inexistente
        // dispara un use-after-free real dentro de su gestor de recursos (miniaudio
        // 0.11.21, ma_resource_manager_data_buffer_node_acquire — confirmado con ASan).
        // Se hace una vez, no por voz: todas las voces salen del mismo archivo.
        std::FILE* probe = std::fopen(loose_path, "rb");
        if (probe == nullptr) {
            pool_release<SoundTag>(&g_pool, handle);
            log_error("audio_load: no se encontro '%s'", loose_path);
            return AudioLoadResult::NotFound;
        }
        std::fclose(probe);
    } else {
        bool owned = false;
        if (!pak_resolve(logical_name, &bytes, &size, &owned)) {
            pool_release<SoundTag>(&g_pool, handle);
            log_error("audio_load: no se encontro '%s'", logical_name);
            return AudioLoadResult::NotFound;
        }
    }

    // Cargar/decodificar un archivo asigna heap por como funciona miniaudio (igual que
    // ejecutar Lua, ADR-0032): la primera vez que un @sfx/@bgm nuevo se dispara dentro del
    // bucle de frame, este es el unico otro punto exceptuado de la regla de cero heap
    // (SPEC.md #4). Cargas repetidas del mismo nombre usan la cache de arriba y no llegan
    // aqui. Crear aqui TODAS las voces del efecto, y no una por reproduccion, es lo que
    // mantiene audio_play() libre de asignaciones (ver el ADR de polifonia de M12).
    heap_guard_suspend();
    ma_result result;
    if (streaming) {
        result = sound_instance_init(loose_path, bytes, size, true, &slot->sound,
                                      &slot->decoder, &slot->has_decoder);
    } else {
        result = voices_init_for_sound(slot, loose_path, bytes, size);
    }
    heap_guard_resume();

    if (result != MA_SUCCESS) {
        pool_release<SoundTag>(&g_pool, handle);
        log_error("audio_load: no se pudo cargar '%s' (%d)", logical_name,
                  static_cast<int>(result));
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

    // Musica (streaming): una sola instancia, volver a reproducirla la reinicia.
    // Comportamiento de M6 intacto — la polifonia es de efectos, una pista de musica
    // solapandose consigo misma no es algo que nadie quiera.
    if (slot->voice_count == 0) {
        ma_sound_seek_to_pcm_frame(&slot->sound, 0);
        ma_sound_set_volume(&slot->sound, effective_volume(bus, volume));
        ma_sound_set_looping(&slot->sound, loop ? MA_TRUE : MA_FALSE);
        ma_sound_start(&slot->sound);
        return voice_id_pack(s);
    }

    // Efecto: la primera voz que no este sonando. Una voz que llego al final deja de estar
    // "playing" sola — el propio motor la para al procesarla (ma_engine_node_process llama
    // a ma_sound_stop en cuanto ma_sound_at_end), asi que reciclar voces no necesita
    // ningun barrido por frame.
    u32 chosen = k_max_voices;
    for (u32 i = 0; i < slot->voice_count; ++i) {
        u32 index = slot->first_voice + i;
        if (ma_sound_is_playing(&g_voices[index].sound) == MA_FALSE) {
            chosen = index;
            break;
        }
    }
    // Todas ocupadas: se roba la mas antigua en round-robin en vez de no sonar. Un efecto
    // que se pierde del todo se nota mas que uno que corta a su propia copia mas vieja.
    if (chosen == k_max_voices) {
        chosen           = slot->first_voice + slot->next_voice;
        slot->next_voice = (slot->next_voice + 1) % slot->voice_count;
        ma_sound_stop(&g_voices[chosen].sound);
    }

    Voice& v   = g_voices[chosen];
    v.play_gen = (v.play_gen + 1) & k_voice_gen_mask;
    ma_sound_seek_to_pcm_frame(&v.sound, 0);
    ma_sound_set_volume(&v.sound, effective_volume(bus, volume));
    ma_sound_set_looping(&v.sound, loop ? MA_TRUE : MA_FALSE);
    ma_sound_start(&v.sound);
    return voice_id_pack_sfx(chosen, v.play_gen);
}

u64 audio_alloc_count() {
    return g_ma_alloc_count;
}

u32 audio_active_voice_count(SoundHandle s) {
    SoundSlot* slot = pool_resolve<SoundTag>(&g_pool, s);
    if (slot == nullptr || !slot->initialized) {
        return 0;
    }
    if (slot->voice_count == 0) {
        return ma_sound_is_playing(&slot->sound) != MA_FALSE ? 1u : 0u;
    }
    u32 active = 0;
    for (u32 i = 0; i < slot->voice_count; ++i) {
        if (ma_sound_is_playing(&g_voices[slot->first_voice + i].sound) != MA_FALSE) {
            active += 1;
        }
    }
    return active;
}

void audio_stop(u32 voice_id, f32 fade_seconds) {
    ma_sound* sound = voice_resolve(voice_id);
    if (sound == nullptr) {
        return;
    }
    if (fade_seconds <= 0.0f) {
        ma_sound_stop(sound);
        return;
    }
    ma_uint64 fade_frames =
        static_cast<ma_uint64>(fade_seconds * static_cast<f32>(ma_engine_get_sample_rate(&g_engine)));
    ma_sound_set_fade_in_pcm_frames(sound, -1.0f, 0.0f, fade_frames);
    ma_sound_set_stop_time_in_pcm_frames(
        sound, ma_engine_get_time_in_pcm_frames(&g_engine) + fade_frames);
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
