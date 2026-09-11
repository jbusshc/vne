#include <doctest/doctest.h>

#include <cstdio>
#include <vector>

#include "assets/pak.h"
#include "audio/audio.h"
#include "base/hash.h"

// audio_init() ya se llamo una vez en test_main.cpp (mismo patron que lua_init()): un
// unico ma_engine persistente creado fuera del bucle de frame.

TEST_CASE("audio: cargar y reproducir un sfx da un voice_id valido") {
    SoundHandle h{};
    REQUIRE(audio_load("ogg/puerta_cierra.wav", false, &h) == AudioLoadResult::Ok);
    REQUIRE(h.valid());
    u32 voice_id = audio_play(h, Bus::Sfx, 1.0f, false);
    CHECK(voice_id != 0);
    audio_stop(voice_id, 0.0f);
}

TEST_CASE("audio: cargar un archivo inexistente falla sin crashear") {
    SoundHandle h{};
    CHECK(audio_load("ogg/no_existe.wav", false, &h) != AudioLoadResult::Ok);
    CHECK_FALSE(h.valid());
}

TEST_CASE("audio: cargar el mismo path dos veces devuelve el mismo handle (cache)") {
    SoundHandle a{}, b{};
    REQUIRE(audio_load("ogg/puerta_cierra.wav", false, &a) == AudioLoadResult::Ok);
    REQUIRE(audio_load("ogg/puerta_cierra.wav", false, &b) == AudioLoadResult::Ok);
    CHECK(a == b);
}

TEST_CASE("audio: audio_set_bus_volume no revienta con valores fuera de rango") {
    audio_set_bus_volume(Bus::Music, -1.0f);
    audio_set_bus_volume(Bus::Music, 5.0f);
    audio_set_bus_volume(Bus::Music, 0.5f);  // deja un valor razonable para otros tests
}

TEST_CASE("audio: crossfade de musica cambia la pista activa y su posicion se puede leer") {
    SoundHandle a{}, b{};
    REQUIRE(audio_load("ogg/tema_a.wav", true, &a) == AudioLoadResult::Ok);
    REQUIRE(audio_load("ogg/tema_b.wav", true, &b) == AudioLoadResult::Ok);

    audio_crossfade_music(a, 0.0f);
    CHECK(audio_music_position() >= 0.0f);

    audio_crossfade_music(b, 0.5f);
    audio_seek_music(1.0f);
    CHECK(audio_music_position() == doctest::Approx(1.0f).epsilon(0.05));

    audio_stop_music(0.0f);
}

TEST_CASE("audio: el catalogo de musica resuelve un track_id de vuelta a un archivo") {
    u16         track_id = static_cast<u16>(fnv1a_u32("tema_a") % 65536u);
    SoundHandle h{};
    CHECK(audio_load_track(track_id, &h));
    CHECK(h.valid());
}

TEST_CASE("audio: un track_id que no esta en el catalogo se rechaza sin crashear") {
    SoundHandle h{};
    CHECK_FALSE(audio_load_track(0xFFFFu, &h));
}

TEST_CASE("audio: un voice_id caducado no puede parar una reproduccion posterior") {
    SoundHandle h{};
    REQUIRE(audio_load("ogg/puerta_cierra.wav", false, &h) == AudioLoadResult::Ok);

    // El voice_id lleva una generacion justamente para esto. Antes de M10 era literalmente
    // indice+1, asi que un id viejo podia acabar parando otro sonido; con la polifonia de
    // M12 el riesgo crece, porque las voces se reciclan constantemente entre efectos.
    u32 first = audio_play(h, Bus::Sfx, 1.0f, false);
    REQUIRE(first != 0);
    audio_stop(first, 0.0f);
    REQUIRE(audio_active_voice_count(h) == 0);

    // La voz que acaba de quedar libre es la primera que vuelve a elegirse, asi que
    // `second` apunta al MISMO ma_sound que `first`: lo unico que los distingue es la
    // generacion de reproduccion.
    u32 second = audio_play(h, Bus::Sfx, 1.0f, false);
    REQUIRE(second != 0);
    CHECK(second != first);
    CHECK(audio_active_voice_count(h) == 1);

    audio_stop(first, 0.0f);  // id caducado: no debe tocar la reproduccion viva
    CHECK(audio_active_voice_count(h) == 1);

    audio_stop(second, 0.0f);  // el id bueno si la para
    CHECK(audio_active_voice_count(h) == 0);
}

TEST_CASE("audio: el mismo efecto disparado 5 veces da 5 voces simultaneas (criterio M12)") {
    SoundHandle h{};
    REQUIRE(audio_load("ogg/puerta_cierra.wav", false, &h) == AudioLoadResult::Ok);
    REQUIRE(audio_active_voice_count(h) == 0);

    // SPEC.md #12, M12: "El mismo @sfx disparado 5 veces en 100 ms produce 5 voces
    // simultaneas". Las cinco llamadas seguidas caen muy por debajo de esos 100 ms; lo que
    // se comprueba es que la quinta sigue sumando en vez de reiniciar a la primera, que es
    // lo que hacia el codigo de M6.
    u32 ids[5] = {};
    for (u32 i = 0; i < 5; ++i) {
        ids[i] = audio_play(h, Bus::Sfx, 1.0f, false);
        REQUIRE(ids[i] != 0);
        CHECK(audio_active_voice_count(h) == i + 1);
    }

    // Y son cinco voces distintas, no la misma devuelta cinco veces.
    for (u32 i = 0; i < 5; ++i) {
        for (u32 j = i + 1; j < 5; ++j) {
            CHECK(ids[i] != ids[j]);
        }
    }

    for (u32 i = 0; i < 5; ++i) {
        audio_stop(ids[i], 0.0f);
    }
    CHECK(audio_active_voice_count(h) == 0);
}

TEST_CASE("audio: reproducir un efecto ya cargado no asigna heap (SPEC.md #4)") {
    SoundHandle h{};
    REQUIRE(audio_load("ogg/puerta_cierra.wav", false, &h) == AudioLoadResult::Ok);

    // Este es el test que justifica crear las 8 voces en audio_load y no por reproduccion:
    // con ma_sound_init_copy (lo evidente) cada disparo asignaria, y heap_guard ni siquiera
    // se enteraria porque no instrumenta malloc. Se mide con el contador del propio
    // miniaudio.
    u64 before = audio_alloc_count();
    u32 ids[5] = {};
    for (u32 i = 0; i < 5; ++i) {
        ids[i] = audio_play(h, Bus::Sfx, 1.0f, false);
    }
    u64 after = audio_alloc_count();
    MESSAGE("asignaciones de miniaudio en 5 audio_play: " << (after - before));
    CHECK(after == before);

    for (u32 i = 0; i < 5; ++i) {
        audio_stop(ids[i], 0.0f);
    }
}

namespace {

// Empaqueta un wav real de assets_src/ogg/ en un .pak de una sola entrada, con el mismo
// formato byte a byte que fabrica test_pak.cpp. Devuelve false si el wav no esta.
bool write_single_sound_pak(const char* pak_path, const char* wav_path,
                             const char* logical_name) {
    std::FILE* src = std::fopen(wav_path, "rb");
    if (src == nullptr) {
        return false;
    }
    std::fseek(src, 0, SEEK_END);
    long wav_size = std::ftell(src);
    std::fseek(src, 0, SEEK_SET);
    std::vector<u8> wav(static_cast<usize>(wav_size));
    usize read = std::fread(wav.data(), 1, wav.size(), src);
    std::fclose(src);
    if (read != wav.size()) {
        return false;
    }

    struct PakEntry {
        u64 name_hash;
        u64 offset;
        u32 size;
        u8  type;
        u8  _pad[3];
    };
    static_assert(sizeof(PakEntry) == 24);

    std::FILE* f = std::fopen(pak_path, "wb");
    if (f == nullptr) {
        return false;
    }
    u32 header[3] = {0x4B504E56u /* 'VNPK' */, 1, 1};
    std::fwrite(header, sizeof(u32), 3, f);
    PakEntry entry{fnv1a_u64(logical_name), 3 * sizeof(u32) + sizeof(PakEntry),
                   static_cast<u32>(wav.size()), 0, {}};
    std::fwrite(&entry, sizeof(PakEntry), 1, f);
    std::fwrite(wav.data(), 1, wav.size(), f);
    std::fclose(f);
    return true;
}

}  // namespace

TEST_CASE("audio: la polifonia funciona tambien en backend empaquetado (M11+M12)") {
    // Esta es la rama que importa de verdad: en backend suelto cada voz sale de
    // ma_sound_init_from_file, pero en empaquetado sale de ma_decoder_init_memory +
    // ma_sound_init_from_data_source, que es un camino distinto de miniaudio. Si la
    // polifonia se hubiera hecho con ma_sound_init_copy, esta rama devolveria
    // MA_INVALID_OPERATION (init_copy exige pResourceManagerDataSource, que solo pone
    // ma_sound_init_from_file) y el juego empaquetado — o sea, el que se distribuye —
    // habria perdido la polifonia sin que ningun test en Dev lo notara.
    const char* pak_path     = "test_audio_voices.pak";
    const char* logical_name = "ogg/packed_voices.wav";
    REQUIRE(write_single_sound_pak(pak_path, "assets_src/ogg/puerta_cierra.wav",
                                    logical_name));

    pak_mount(pak_path);
    REQUIRE(pak_is_packed());

    SoundHandle h{};
    REQUIRE(audio_load(logical_name, false, &h) == AudioLoadResult::Ok);

    u64 before = audio_alloc_count();
    u32 ids[5] = {};
    for (u32 i = 0; i < 5; ++i) {
        ids[i] = audio_play(h, Bus::Sfx, 1.0f, false);
        REQUIRE(ids[i] != 0);
    }
    CHECK(audio_active_voice_count(h) == 5);
    CHECK(audio_alloc_count() == before);  // tampoco asigna por este camino

    for (u32 i = 0; i < 5; ++i) {
        audio_stop(ids[i], 0.0f);
    }
    CHECK(audio_active_voice_count(h) == 0);

    pak_mount(".");  // restaura el montaje por defecto del binario de tests (ver test_pak.cpp)
    std::remove(pak_path);
}

TEST_CASE("audio: la musica NO es polifonica (se reinicia, no se superpone)") {
    SoundHandle m{};
    REQUIRE(audio_load("ogg/tema_a.wav", true, &m) == AudioLoadResult::Ok);

    u32 a = audio_play(m, Bus::Music, 1.0f, false);
    u32 b = audio_play(m, Bus::Music, 1.0f, false);
    REQUIRE(a != 0);
    CHECK(a == b);  // mismo slot, misma instancia: no hay generacion nueva que empaquetar
    CHECK(audio_active_voice_count(m) == 1);

    audio_stop(a, 0.0f);
    CHECK(audio_active_voice_count(m) == 0);
}

TEST_CASE("audio: audio_stop con un voice_id invalido no hace nada (y no crashea)") {
    audio_stop(0, 0.0f);                 // id nulo
    audio_stop(0xFFFFFFFFu, 0.0f);       // indice fuera de rango
    audio_stop(0x00FF0001u, 0.5f);       // indice valido, generacion que no coincide
}
