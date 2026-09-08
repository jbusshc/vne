#include <doctest/doctest.h>

#include "audio/audio.h"
#include "base/hash.h"

// audio_init() ya se llamo una vez en test_main.cpp (mismo patron que lua_init()): un
// unico ma_engine persistente creado fuera del bucle de frame.

TEST_CASE("audio: cargar y reproducir un sfx da un voice_id valido") {
    SoundHandle h{};
    REQUIRE(audio_load("assets_src/ogg/puerta_cierra.wav", false, &h) == AudioLoadResult::Ok);
    REQUIRE(h.valid());
    u32 voice_id = audio_play(h, Bus::Sfx, 1.0f, false);
    CHECK(voice_id != 0);
    audio_stop(voice_id, 0.0f);
}

TEST_CASE("audio: cargar un archivo inexistente falla sin crashear") {
    SoundHandle h{};
    CHECK(audio_load("assets_src/ogg/no_existe.wav", false, &h) != AudioLoadResult::Ok);
    CHECK_FALSE(h.valid());
}

TEST_CASE("audio: cargar el mismo path dos veces devuelve el mismo handle (cache)") {
    SoundHandle a{}, b{};
    REQUIRE(audio_load("assets_src/ogg/puerta_cierra.wav", false, &a) == AudioLoadResult::Ok);
    REQUIRE(audio_load("assets_src/ogg/puerta_cierra.wav", false, &b) == AudioLoadResult::Ok);
    CHECK(a == b);
}

TEST_CASE("audio: audio_set_bus_volume no revienta con valores fuera de rango") {
    audio_set_bus_volume(Bus::Music, -1.0f);
    audio_set_bus_volume(Bus::Music, 5.0f);
    audio_set_bus_volume(Bus::Music, 0.5f);  // deja un valor razonable para otros tests
}

TEST_CASE("audio: crossfade de musica cambia la pista activa y su posicion se puede leer") {
    SoundHandle a{}, b{};
    REQUIRE(audio_load("assets_src/ogg/tema_a.wav", true, &a) == AudioLoadResult::Ok);
    REQUIRE(audio_load("assets_src/ogg/tema_b.wav", true, &b) == AudioLoadResult::Ok);

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

TEST_CASE("audio: el voice_id lleva la generacion del slot, no solo el indice") {
    SoundHandle h{};
    REQUIRE(audio_load("assets_src/ogg/puerta_cierra.wav", false, &h) == AudioLoadResult::Ok);
    u32 voice_id = audio_play(h, Bus::Sfx, 1.0f, false);
    REQUIRE(voice_id != 0);

    // Antes el voice_id era literalmente indice+1, asi que un id viejo podia acabar
    // parando el sonido de otro handle si su slot se reutilizaba. Ahora los 16 bits
    // altos llevan la generacion del Pool, que para un slot recien cogido es 1.
    CHECK((voice_id & 0xFFFFu) == h.index + 1);
    CHECK((voice_id >> 16) == (h.gen & 0xFFFFu));
    CHECK(voice_id != h.index + 1);

    audio_stop(voice_id, 0.0f);
}

TEST_CASE("audio: audio_stop con un voice_id invalido no hace nada (y no crashea)") {
    audio_stop(0, 0.0f);                 // id nulo
    audio_stop(0xFFFFFFFFu, 0.0f);       // indice fuera de rango
    audio_stop(0x00FF0001u, 0.5f);       // indice valido, generacion que no coincide
}
