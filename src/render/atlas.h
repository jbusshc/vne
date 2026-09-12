#pragma once
#include "core/arena.h"
#include "core/types.h"

// Registro de sprites del atlas (M13). El atlas horneado por `sz_bake` trae desde la
// version 3 de `atlas_00.bin` una tabla de nombres logicos, y esta tabla ES el registro de
// assets que pide SPEC.md #11: no hay un segundo archivo que diga lo mismo, porque dos
// fuentes de verdad sobre lo mismo solo pueden desincronizarse.
//
// M11 dejo el atlas sin nombres a proposito, razonando que no habia ningun consumidor que
// pidiera un sprite por nombre y que añadir una API sin llamante es lo que SPEC.md #1 dice
// que no se hace. Ahora hay dos consumidores reales: el compilador del DSL, que valida
// `@show`/`@bg` en tiempo de compilacion (cierra ADR-0022, abierto desde M3), y el
// renderizado de fondos y actores.

struct AtlasSprite {
    u16 x, y, w, h;
};

// Carga `atlas_00.bin` a traves del backend de assets activo (nombre logico, no ruta:
// requiere que pak_mount() ya se haya llamado). La tabla queda residente en `arena`.
// Devuelve false y deja el registro vacio si falta o esta corrupto; el juego debe seguir
// funcionando con el placeholder magenta (SPEC.md #4), nunca caerse.
bool atlas_load(Arena* arena);

// Busca un sprite por su nombre logico (el del PNG sin directorio ni extension). Devuelve
// false si no esta. Biseccion sobre la tabla ordenada por hash, sin asignar: se puede
// llamar dentro del bucle de frame.
bool atlas_find(const char* logical_name, AtlasSprite* out);

// Cuantos sprites tiene el registro cargado. Para diagnostico y tests.
u32 atlas_sprite_count();

// Acceso por indice, para el banco de pruebas de M1 (5000 sprites de un atlas en 1 draw
// call), que reparte sprites sin conocer sus nombres.
bool atlas_sprite_at(u32 index, AtlasSprite* out);

// Nombre logico del sprite `index`, o "" si no existe. Lo pide el visor de atlas del editor
// (M15, criterio de SPEC.md #12): hasta ahora la tabla de nombres solo se podia consultar
// en un sentido (nombre -> rectangulo), y un visor necesita el contrario para poder
// enumerar lo que hay dentro.
const char* atlas_name_at(u32 index);
