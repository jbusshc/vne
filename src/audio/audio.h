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

// Simplificacion deliberada de M6 (ver ADR en docs/DECISIONS.md): cada SoundHandle tiene
// como mucho UNA instancia sonando a la vez. Volver a reproducirlo mientras ya suena lo
// reinicia desde el principio en vez de superponer una segunda copia. Devuelve un
// voice_id valido (el propio indice del handle) o 0 si el handle no resuelve a nada.
u32  audio_play(SoundHandle s, Bus bus, f32 volume, bool loop);
void audio_stop(u32 voice_id, f32 fade_seconds);
void audio_set_bus_volume(Bus b, f32 v);

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
