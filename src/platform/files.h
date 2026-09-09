#pragma once
#include "base/arena.h"
#include "base/types.h"

// Filesystem portable sobre SDL3 (SPEC.md #2, "Como se programa la portabilidad": lo
// especifico del sistema operativo vive detras de platform/, que es la unica capa que
// habla con SDL3 para filesystem entre otras cosas). Codigo de runtime (vne_base/
// vne_game/vne_tests) pasa por aqui, nunca por <windows.h>/<dirent.h> directamente.
//
// tools/bake/ es la excepcion deliberada: enlaza sin SDL3 a proposito (ver el comentario
// de su propio CMakeLists.txt, "no abre ventana ni dibuja nada") y mantiene su propio
// listado minimo de directorio con su rama #if _WIN32/#else ya escrita. No lo consolides
// aqui: forzaria una dependencia de SDL3 sobre una herramienta que la evita a proposito.

// true si el path existe (archivo o directorio). No distingue "no existe" de otros
// errores de acceso: este proyecto no necesita esa distincion en ningun sitio todavia.
bool file_exists(const char* path);

// Ultima modificacion, en nanosegundos desde epoch (SDL_Time). 0 si el archivo no existe.
// Reemplaza a stat().st_mtime (resolucion de 1 segundo) donde hace falta detectar cambios
// finos, como el watcher de M11 (SPEC.md #7.4: "comprueba mtimes cada 500 ms").
i64 file_mtime_ns(const char* path);

// Lee el archivo entero en `arena` (skill vne-memory-model: nunca via operator new/malloc
// del motor). Devuelve false y deja *out_data en nullptr si no se pudo leer -- no existe,
// o la arena no tiene espacio; el llamante decide que hacer, igual que arena_alloc_n en
// general.
[[nodiscard]] bool file_read_all(const char* path, Arena* arena, u8** out_data,
                                  usize* out_size);

// Invoca callback(userdata, name_no_ext, full_name) por cada archivo de `dir` cuyo nombre
// termine en `extension` (con el punto, p.ej. ".ogg"; nullptr o "" = todos). Ambos nombres
// llegan sin ruta; name_no_ext ademas sin extension, listo para fnv1a_u32 (mismo patron
// que el catalogo de musica de M6, ver audio.cpp) -- full_name es para cuando el llamante
// necesita reabrir el archivo y no puede asumir la extension (distintos archivos en el
// mismo directorio pueden no compartirla, como pasa hoy en assets_src/ogg/ con .wav de
// prueba). No entra en subdirectorios. Puntero a funcion crudo, no std::function (skill
// vne-cpp-style): esto no corre en el bucle de frame, pero la regla del proyecto es una
// sola convencion en todo el motor, no una por caso de uso.
using FileListCallback = void (*)(void* userdata, const char* name_no_ext,
                                   const char* full_name);
void dir_list_by_extension(const char* dir, const char* extension, FileListCallback callback,
                            void* userdata);
