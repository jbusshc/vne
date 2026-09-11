#pragma once
#include "base/handle.h"
#include "base/types.h"

// Audio (SPEC.md #7.3): miniaudio decodifica OGG/WAV/FLAC y mezcla. La musica se
// reproduce en streaming, los efectos se cargan enteros. No sabe nada de miniaudio fuera
// de audio.cpp (misma idea que gfx/texture_internal.h para sokol_gfx).

enum class Bus : u8 { Master, Music, Sfx, Voice, Count };

enum class AudioLoadResult : u8 { Ok, NotFound, BadFormat, OutOfSlots };

void audio_init();
void audio_shutdown();

// Avanza el fundido de musica en curso y refleja bgm_position en *out_bgm_position (para
// que quien tenga el GameState activo lo mantenga al dia sin que audio.cpp conozca
// GameState directamente, ver vm.cpp). No asigna heap: se llama una vez por frame.
void audio_update(f32 dt, f32* out_bgm_position);

AudioLoadResult audio_load(const char* path, bool streaming, SoundHandle* out);

// Desde M12 un efecto es polifonico: reproducirlo mientras ya suena superpone una copia
// nueva en vez de reiniciar la que sonaba (la simplificacion de M6 se levanta). Las voces
// se crean todas en audio_load, asi que audio_play no asigna heap y sigue valiendo dentro
// del bucle de frame. La musica (cargada con streaming = true) mantiene el comportamiento
// de M6: una sola instancia, que se reinicia.
//
// Devuelve un voice_id (etiqueta + indice + generacion empaquetados, ver voice_id_pack en
// audio.cpp) o 0 si el handle no resuelve a nada. audio_stop() valida esa generacion, asi
// que un voice_id de una reproduccion ya terminada no puede parar el sonido equivocado.
u32  audio_play(SoundHandle s, Bus bus, f32 volume, bool loop);
void audio_stop(u32 voice_id, f32 fade_seconds);
void audio_set_bus_volume(Bus b, f32 v);

// Cuantas copias de `s` estan sonando ahora mismo. Existe para poder verificar el criterio
// de polifonia de M12 desde un test sin exponer miniaudio fuera de audio.cpp; el juego no
// la necesita.
u32 audio_active_voice_count(SoundHandle s);

// Asignaciones acumuladas hechas por miniaudio desde audio_init(). heap_guard NO las ve:
// solo sobrecarga operator new/delete, y miniaudio es C y llama a malloc directamente. Con
// esto se puede comprobar de verdad que audio_play() no asigna dentro del bucle de frame
// (SPEC.md #4) en vez de darlo por supuesto. Siempre disponible, tambien en Ship: es una
// suma de un u64, no instrumentacion cara.
u64 audio_alloc_count();

// Fundido cruzado de musica (criterio de M6: "sin clicks al hacer crossfade"): la pista
// actual (si hay una) baja de volumen mientras `next` sube, ambas sonando a la vez
// durante `seconds`. bgm_position se reinicia a 0 para `next`.
void audio_crossfade_music(SoundHandle next, f32 seconds);

// Posicion aproximada (segundos) de la pista de musica activa, para GameState.bgm_position.
f32 audio_music_position();

// Salta la pista de musica activa a una posicion aproximada (segundos), para restaurar
// bgm_position tras cargar una partida o hacer rollback (SPEC.md #12, M6).
void audio_seek_music(f32 seconds);

// Detiene la musica actual con fundido, sin empezar una nueva (CmdKind::StopBgm).
void audio_stop_music(f32 fade_seconds);

// Resuelve un track_id (SPEC.md #8.2: GameState.bgm_track_id es u16, no puede depender
// del string_pool de un guion concreto) contra el catalogo de assets_src/ogg escaneado
// en audio_init(), y lo carga (o devuelve el ya cargado). Ver ADR de M6 en
// docs/DECISIONS.md.
bool audio_load_track(u16 track_id, SoundHandle* out);
