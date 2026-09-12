#pragma once
#include "base/types.h"

// Preferencias del jugador (M14, SPEC.md #12). **Viven FUERA de GameState** y ese es el
// punto: el skill vne-serializable-state lo dice explicitamente — "¿es preferencia del
// jugador que no afecta a la partida? va en config.ini, aparte". El idioma o el volumen no
// son parte de la partida y no deben viajar en un `.vnsave` ni deshacerse con un rollback.
//
// Es un archivo de TEXTO, y es la unica excepcion deliberada a "sin parsear texto en builds
// de release" (CLAUDE.md): esa regla existe por los datos de juego, que se hornean
// precisamente para no parsearlos. Un config.ini tiene que ser texto para que el jugador lo
// pueda abrir y editar, lo pide asi SPEC.md #12, y son unos cientos de bytes leidos una vez
// al arrancar — nunca dentro del bucle de frame. Ver ADR-0068.

struct Config {
    // Indice en k_locales (game/locales.h). En el archivo se guarda el ID ("es"), no el
    // indice: un indice deja de significar lo mismo en cuanto se reordena la tabla.
    u32  locale_index = 0;
    bool fullscreen   = false;
    f32  bus_volume[4] = {1.0f, 1.0f, 1.0f, 1.0f};  // Master, Music, Sfx, Voice
};

extern Config g_config;

// Lee "config.ini" del directorio de trabajo. Un archivo que falta, esta a medias o trae
// basura NO es un error: se queda lo que hubiera por defecto y el juego arranca igual. Una
// preferencia corrupta no puede impedir jugar.
void config_load();

// Escribe "config.ini". Se llama al cambiar una preferencia, no solo al salir: si el juego
// se cierra de forma anormal, la preferencia ya esta guardada.
void config_save();
