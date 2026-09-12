#pragma once
#include "base/types.h"

// Catalogo de mapas por map_id (M13). Mismo problema y misma solucion que el catalogo de
// musica de M6 (ADR-0034): `GameState.map_id` es un `u16` fijo por SPEC.md #8.2, tiene que
// sobrevivir a un guardado, y no puede ser un indice en ninguna tabla que dependa del guion
// o del orden en que el sistema de archivos devuelva los nombres.
//
// Hasta M13 `map_id` estaba puesto a 1 a mano en main.cpp con el comentario "unico mapa del
// proyecto por ahora, cualquier valor distinto de 0 basta": guardar y cargar funcionaba solo
// porque siempre se cargaba el mismo mapa pasara lo que pasara.
//
// El id es `fnv1a_u32(nombre_sin_extension) % 65536`, igual que una pista de musica. Las
// colisiones se detectan al hornear (ADR-0063).

// Escanea los mapas disponibles. En backend suelto recorre assets_baked/ buscando `.vnm`;
// en empaquetado lee `map_catalog.bin`, que escribe `vne_bake pack` (no hay directorio que
// recorrer dentro de un .pak). Llamar una vez al arrancar, despues de montar el backend.
void map_catalog_init();

// Nombre logico (p. ej. "demo_map.vnm") del mapa con ese id, o nullptr si no esta.
const char* map_catalog_resolve(u16 map_id);

// El id que le corresponde a un nombre sin extension ("demo_map"). No consulta el catalogo:
// es la funcion de hash, para que quien crea una partida nueva pueda fijar map_id sin
// depender de que el catalogo ya este cargado.
u16 map_catalog_id_of(const char* name_no_ext);

// Cuantos mapas hay. Para diagnostico y tests.
u32 map_catalog_count();
