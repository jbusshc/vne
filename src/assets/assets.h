#pragma once
#include "base/handle.h"
#include "base/types.h"

// Sistema de assets (SPEC.md #7.4). Resuelve nombres logicos a recursos por handle,
// apoyandose en assets/pak.h para los bytes (que ya abstrae "directorio suelto" vs
// ".pak"). Lo que esta capa anade encima es el hilo de IO y la cache por nombre.
//
// El hilo de IO SOLO lee bytes. Decodificar (QOI, TTF, audio) y subir a la GPU se queda
// en el hilo principal, dentro de assets_process_completed_loads(): sokol_gfx no es
// thread-safe, asi que sg_make_image no puede llamarse desde otro hilo. Lo que se elimina
// asi es el bloqueo por E/S de disco, que es lo que de verdad causa los tirones que
// SPEC.md #14 quiere evitar.
//
// assets_init() debe llamarse DESPUES de pak_mount() (necesita un backend montado) y
// despues de texture_system_init() (reserva el handle placeholder).
void assets_init();
void assets_shutdown();

// Devuelve de inmediato (sin tocar el disco) un handle valido que ya dibuja el placeholder
// magenta, y encola la carga real; cuando termine, ese MISMO handle pasa a apuntar a la
// textura de verdad, sin que el llamante tenga que volver a preguntar. Pedir dos veces el
// mismo nombre devuelve el mismo handle (cache), sin encolar una segunda carga.
//
// Si la cola de carga esta llena devuelve un handle invalido, que gfx dibuja igualmente
// como el placeholder magenta (texture_gpu_image cae al placeholder cuando un handle no
// resuelve). Nunca falla de forma fatal ni deja de dibujar algo (SPEC.md #4).
TextureHandle assets_texture(const char* logical_name);

// Sincronas, a diferencia de assets_texture: ni una fuente ni un sonido tienen "version
// placeholder" que mostrar mientras cargan (text/font.h: "sin fuente no hay texto que
// dibujar"; audio.h devuelve un handle invalido si falla), asi que diferirlas solo
// complicaria el codigo sin nada que ensenar entretanto. Ambas resuelven por el mismo
// backend que el resto, asi que funcionan igual con .pak que con directorio suelto.
FontHandle  assets_font(const char* logical_name, u32 px_size);
SoundHandle assets_sound(const char* logical_name, bool streaming);

// Integra en el mundo del juego lo que el hilo de IO haya terminado de leer: decodifica y
// sube a la GPU. Llamar una vez por frame desde el hilo principal. Es el UNICO punto donde
// un asset cargado entra en juego.
void assets_process_completed_loads();

// Diagnostico para el editor/HUD (M8): cuantas cargas hay encoladas o en vuelo ahora
// mismo. 0 significa que todo lo pedido ya esta resuelto.
u32 assets_pending_count();
