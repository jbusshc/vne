#include "vm/save.h"

#include "base/hash.h"
#include "vm/symbols_load.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "audio/audio.h"
#include "base/crc32.h"
#include "base/log.h"

namespace {
constexpr u32 k_vnsave_magic = 0x56534E56u;  // 'VNSV'

// Tamano de GameState en la version 1 del formato (M4-M8, antes de MapMode): map_id es
// el primer campo que M9 anadio al final de la struct (ver state.h), asi que todo lo que
// viene antes tiene exactamente el mismo layout que en v1 — no hace falta un struct
// GameStateV1 aparte, el propio offsetof marca la frontera.
constexpr usize k_gamestate_v1_size = offsetof(GameState, map_id);

// SPEC.md #8.3: "las funciones migrate_vN_to_vN+1 se escriben en cuanto se rompe
// compatibilidad, nunca despues". v1 no tenia mapa activo: migrar es simplemente dejar
// map_id/player_x/player_y en su valor por defecto (0, "sin mapa activo"), ya
// garantizado por el zero-init de *out antes de copiar el prefijo v1 encima.
void migrate_v1_to_v2(const u8* v1_bytes, GameState* out) {
    *out = GameState{};
    std::memcpy(out, v1_bytes, k_gamestate_v1_size);
}

// v2 -> v3 (M13). El LAYOUT no cambia ni un byte: lo que cambia es el SIGNIFICADO de
// actor_id, pose_id y bg_id. Hasta M13 eran indices de un interner que empezaba en 0; ahora
// empiezan en 1, porque el 0 esta reservado para "slot vacio" (SPEC.md #8.2) y con base 0 el
// primer actor de cada guion era indistinguible de un hueco (ver ADR-0062).
//
// No se pueden mapear: el interner es local a una compilacion concreta del guion y no viaja
// en la partida. Y dejarlos tal cual seria PEOR que perderlos — el viejo id 1 resolveria
// ahora al nombre del actor 0, dibujando el personaje equivocado sin ningun aviso. Asi que
// se limpian: los slots quedan vacios y el fondo sin poner, y el siguiente @show/@bg del
// guion los repone. Se pierde lo que hubiera en pantalla en el momento de guardar; el resto
// de la partida (pc, variables, flags, musica, mapa, posicion del jugador) sobrevive intacto.
void migrate_v2_to_v3(GameState* state) {
    for (u32 i = 0; i < k_max_actor_slots; ++i) {
        state->actors[i] = ActorSlot{};
    }
    state->bg_id = 0;
}

// v3 -> v4 (M14, ADR-0067). Los ids de variable y bandera pasan de `fnv1a(nombre) %
// capacidad` a ser el indice en la tabla de simbolos del proyecto, asi que un v3 tiene sus
// valores en huecos que ya no significan lo mismo.
//
// Y aqui SI se puede migrar de verdad, a diferencia de v2 -> v3: la tabla de simbolos tiene
// todos los nombres, y el hueco viejo de cada uno se puede recalcular con la formula
// antigua. Para cada nombre se copia el valor de donde estaba a donde va ahora. Lo unico
// que se pierde son los valores de nombres que ya no usa ningun guion, que es justo lo que
// deberia perderse.
//
// Si dos nombres colisionaban en el esquema viejo, los dos leen del mismo hueco y acaban
// con el mismo valor. No es recuperable ni tiene por que serlo: en el v3 ese valor ya era
// el de una variable pisando a la otra.
void migrate_v3_to_v4(GameState* state) {
    i32 old_vars[k_max_vars];
    u8  old_flags[k_max_flags / 8];
    std::memcpy(old_vars, state->vars, sizeof(old_vars));
    std::memcpy(old_flags, state->flags, sizeof(old_flags));
    std::memset(state->vars, 0, sizeof(state->vars));
    std::memset(state->flags, 0, sizeof(state->flags));

    for (u32 id = 1; id <= symbols_count(SymKind::Var); ++id) {
        const char* name     = symbols_name(SymKind::Var, static_cast<u16>(id));
        u32         old_slot = fnv1a_u32(name) % k_max_vars;
        state->vars[id]      = old_vars[old_slot];
    }
    for (u32 id = 1; id <= symbols_count(SymKind::Flag); ++id) {
        const char* name     = symbols_name(SymKind::Flag, static_cast<u16>(id));
        u32         old_slot = fnv1a_u32(name) % k_max_flags;
        if ((old_flags[old_slot / 8] & (1u << (old_slot % 8))) != 0) {
            state->flags[id / 8] = static_cast<u8>(state->flags[id / 8] | (1u << (id % 8)));
        }
    }
}
}  // namespace

SaveResult save_game(const char* path, const GameState& state, const Backlog& backlog,
                      const u8* thumbnail_qoi, u32 thumbnail_size) {
    std::FILE* file = std::fopen(path, "wb");
    if (file == nullptr) {
        log_error("save_game: no se pudo abrir '%s' para escribir", path);
        return SaveResult::WriteError;
    }

    const u32 state_size     = sizeof(GameState);
    const u32 state_checksum = crc32(&state, sizeof(GameState));

    bool ok = true;
    ok &= std::fwrite(&k_vnsave_magic, sizeof(u32), 1, file) == 1;
    ok &= std::fwrite(&k_savegame_version, sizeof(u32), 1, file) == 1;
    ok &= std::fwrite(&state_size, sizeof(u32), 1, file) == 1;
    ok &= std::fwrite(&state_checksum, sizeof(u32), 1, file) == 1;
    ok &= std::fwrite(&state, sizeof(GameState), 1, file) == 1;
    ok &= std::fwrite(&thumbnail_size, sizeof(u32), 1, file) == 1;
    if (thumbnail_size > 0) {
        ok &= std::fwrite(thumbnail_qoi, 1, thumbnail_size, file) == thumbnail_size;
    }

    BacklogEntry ordered[k_backlog_capacity];
    backlog_get_ordered(backlog, ordered);
    ok &= std::fwrite(&backlog.count, sizeof(u32), 1, file) == 1;
    if (backlog.count > 0) {
        ok &= std::fwrite(ordered, sizeof(BacklogEntry), backlog.count, file) == backlog.count;
    }

    std::fclose(file);
    if (!ok) {
        log_error("save_game: escritura incompleta en '%s'", path);
        return SaveResult::WriteError;
    }
    return SaveResult::Ok;
}

LoadResult load_game(const char* path, GameState* out_state, Backlog* out_backlog) {
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        log_error("load_game: no se encontro '%s'", path);
        return LoadResult::NotFound;
    }

    u32 magic = 0, version = 0, state_size = 0, checksum = 0;
    bool ok = true;
    ok &= std::fread(&magic, sizeof(u32), 1, file) == 1;
    ok &= std::fread(&version, sizeof(u32), 1, file) == 1;
    ok &= std::fread(&state_size, sizeof(u32), 1, file) == 1;
    ok &= std::fread(&checksum, sizeof(u32), 1, file) == 1;
    if (!ok || magic != k_vnsave_magic) {
        std::fclose(file);
        log_error("load_game: '%s' no es un .vnsave valido (magic incorrecto)", path);
        return LoadResult::BadFormat;
    }
    if (version != k_savegame_version && version != 1u && version != 2u && version != 3u) {
        // Las migraciones se ENCADENAN (SPEC.md #8.3): v1 -> v2 -> v3 -> v4. Cualquier
        // version por debajo de la mas vieja soportada se rechaza con un mensaje claro en
        // vez de cargarse a medias.
        std::fclose(file);
        log_error("load_game: '%s' es version %u, se esperaba %u (sin migracion desde ahi)",
                  path, version, k_savegame_version);
        return LoadResult::UnsupportedVersion;
    }

    GameState state{};
    if (version == 1u) {
        if (state_size != static_cast<u32>(k_gamestate_v1_size)) {
            std::fclose(file);
            log_error("load_game: '%s' dice ser v1 pero su GameState mide %u bytes, se "
                      "esperaban %zu",
                      path, state_size, k_gamestate_v1_size);
            return LoadResult::BadFormat;
        }
        u8 v1_bytes[k_gamestate_v1_size];
        if (std::fread(v1_bytes, 1, k_gamestate_v1_size, file) != k_gamestate_v1_size) {
            std::fclose(file);
            log_error("load_game: '%s' esta truncado (bloque de estado v1)", path);
            return LoadResult::BadFormat;
        }
        if (crc32(v1_bytes, k_gamestate_v1_size) != checksum) {
            std::fclose(file);
            log_error("load_game: '%s' no supero el checksum (archivo corrupto)", path);
            return LoadResult::ChecksumMismatch;
        }
        migrate_v1_to_v2(v1_bytes, &state);
        log_info("load_game: '%s' migrado de v1 a v2 (map_id/player_x/player_y a su "
                  "valor por defecto)",
                  path);
    } else {
        if (state_size != sizeof(GameState)) {
            std::fclose(file);
            log_error("load_game: '%s' tiene un GameState de %u bytes, se esperaban %zu", path,
                      state_size, sizeof(GameState));
            return LoadResult::BadFormat;
        }
        if (std::fread(&state, sizeof(GameState), 1, file) != 1) {
            std::fclose(file);
            log_error("load_game: '%s' esta truncado (bloque de estado)", path);
            return LoadResult::BadFormat;
        }
        if (crc32(&state, sizeof(GameState)) != checksum) {
            std::fclose(file);
            log_error("load_game: '%s' no supero el checksum (archivo corrupto)", path);
            return LoadResult::ChecksumMismatch;
        }
    }

    // Cadena de migraciones: un v1 ya paso por migrate_v1_to_v2 arriba, asi que a partir de
    // aqui todo lo que no sea v3 es un v2 que hay que subir (SPEC.md #8.3: "las migraciones
    // se encadenan v2 -> v3 -> v4").
    // Cadena de migraciones (SPEC.md #8.3): v1 -> v2 -> v3 -> v4, cada salto encadenado.
    if (version < 3u) {
        migrate_v2_to_v3(&state);
        log_info("load_game: '%s' migrado a v3 (actores y fondo limpiados: sus ids eran de "
                 "un interner que ya no existe, ver ADR-0062)",
                 path);
    }
    if (version < 4u) {
        migrate_v3_to_v4(&state);
        log_info("load_game: '%s' migrado a v4 (variables y banderas recolocadas a sus ids "
                 "de la tabla de simbolos, ver ADR-0067)",
                 path);
    }

    u32 thumbnail_size = 0;
    if (std::fread(&thumbnail_size, sizeof(u32), 1, file) != 1) {
        std::fclose(file);
        log_error("load_game: '%s' esta truncado (tamano de miniatura)", path);
        return LoadResult::BadFormat;
    }
    if (thumbnail_size > 0) {
        std::fseek(file, static_cast<long>(thumbnail_size), SEEK_CUR);
    }

    u32 backlog_count = 0;
    if (std::fread(&backlog_count, sizeof(u32), 1, file) != 1 ||
        backlog_count > k_backlog_capacity) {
        std::fclose(file);
        log_error("load_game: '%s' esta truncado o corrupto (backlog)", path);
        return LoadResult::BadFormat;
    }
    BacklogEntry ordered[k_backlog_capacity];
    if (backlog_count > 0 &&
        std::fread(ordered, sizeof(BacklogEntry), backlog_count, file) != backlog_count) {
        std::fclose(file);
        log_error("load_game: '%s' esta truncado (entradas de backlog)", path);
        return LoadResult::BadFormat;
    }

    std::fclose(file);

    *out_state = state;
    backlog_load_ordered(out_backlog, ordered, backlog_count);
    return LoadResult::Ok;
}

LoadResult load_save_thumbnail(const char* path, u8* out_qoi, u32 cap, u32* out_size) {
    *out_size = 0;
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        return LoadResult::NotFound;
    }

    u32 magic = 0, version = 0, state_size = 0, checksum = 0;
    bool ok = true;
    ok &= std::fread(&magic, sizeof(u32), 1, file) == 1;
    ok &= std::fread(&version, sizeof(u32), 1, file) == 1;
    ok &= std::fread(&state_size, sizeof(u32), 1, file) == 1;
    ok &= std::fread(&checksum, sizeof(u32), 1, file) == 1;
    (void)checksum;
    if (!ok || magic != k_vnsave_magic || (version != k_savegame_version && version != 1u && version != 2u && version != 3u)) {
        std::fclose(file);
        return LoadResult::BadFormat;
    }
    // Salta el bloque de GameState entero sin decodificarlo: solo hace falta llegar al
    // tamano de miniatura que viene justo despues.
    std::fseek(file, static_cast<long>(state_size), SEEK_CUR);

    u32 thumbnail_size = 0;
    if (std::fread(&thumbnail_size, sizeof(u32), 1, file) != 1) {
        std::fclose(file);
        return LoadResult::BadFormat;
    }
    if (thumbnail_size > 0 && thumbnail_size <= cap) {
        if (std::fread(out_qoi, 1, thumbnail_size, file) != thumbnail_size) {
            std::fclose(file);
            return LoadResult::BadFormat;
        }
        *out_size = thumbnail_size;
    }
    std::fclose(file);
    return LoadResult::Ok;
}

void vm_resync_after_state_change(GameState* state) {
    // M6: restaura la pista de musica y su posicion aproximada tras cargar o hacer
    // rollback (criterio de M6, SPEC.md #12). audio_crossfade_music con fade 0 empieza
    // la pista al instante (sin fundido: no hay nada sonando antes de un F9, es una
    // resincronizacion, no una transicion narrativa); luego se busca hasta bgm_position
    // para que la reanudacion sea aproximada, no siempre desde el principio.
    if (state->bgm_track_id != 0) {
        SoundHandle h{};
        if (audio_load_track(state->bgm_track_id, &h)) {
            audio_crossfade_music(h, 0.0f);
            audio_seek_music(state->bgm_position);
        }
    } else {
        audio_stop_music(0.0f);
    }
    // Volumenes de bus (criterio de M6: "los volumenes de bus persisten"): se reaplican
    // aqui por si el proceso los habia dejado en otro valor (p. ej. tras un F9 que carga
    // una partida con volumenes distintos a los que estaban sonando).
    for (u32 i = 0; i < 4; ++i) {
        audio_set_bus_volume(static_cast<Bus>(i), state->bus_volume[i]);
    }
}
