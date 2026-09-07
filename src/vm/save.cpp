#include "vm/save.h"

#include <cstdio>

#include "audio/audio.h"
#include "base/crc32.h"
#include "base/log.h"

namespace {
constexpr u32 k_vnsave_magic = 0x56534E56u;  // 'VNSV'
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
    if (version != k_savegame_version) {
        // No hay ninguna migracion escrita todavia (esta es la v1, SPEC.md #8.3: las
        // funciones migrate_vN_to_vN+1 se escriben en cuanto se rompe compatibilidad,
        // nunca antes). Cuando exista una v2 real, aqui se encadenan las migraciones.
        std::fclose(file);
        log_error("load_game: '%s' es version %u, se esperaba %u (sin migracion todavia)",
                  path, version, k_savegame_version);
        return LoadResult::UnsupportedVersion;
    }
    if (state_size != sizeof(GameState)) {
        std::fclose(file);
        log_error("load_game: '%s' tiene un GameState de %u bytes, se esperaban %zu", path,
                  state_size, sizeof(GameState));
        return LoadResult::BadFormat;
    }

    GameState state{};
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
    if (!ok || magic != k_vnsave_magic || version != k_savegame_version) {
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
