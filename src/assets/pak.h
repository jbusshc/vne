#pragma once
#include "base/arena.h"
#include "base/types.h"

// Formato .pak y resolucion de una ruta logica a bytes (SPEC.md #7.4/#11), con dos
// backends intercambiables tras la misma interfaz: directorio suelto (una lectura de
// disco por resolucion, para Debug/Dev con hot reload) o .pak resuelto entero a memoria
// una unica vez al montar (para Ship). El resto del motor nunca sabe cual de los dos esta
// activo: solo llama a pak_resolve().
//
// "Mapeado a memoria" de SPEC.md #11 se cumple en el sentido que importa (una sola
// lectura de disco al arrancar, cero E/S por asset despues) pero no es un mmap real del
// sistema operativo: es una lectura completa a g_arena_perm. SDL3 no expone un mmap
// portable de proposito general, y con el tamano de assets de este proyecto (unos pocos
// MB) la diferencia de rendimiento es irrelevante -- ver ADR en docs/DECISIONS.md.
//
// pak_resolve() es seguro desde CUALQUIER hilo, a proposito: el hilo de IO de assets.cpp
// (M11) lo llama fuera del hilo principal, y las arenas del proyecto no son thread-safe
// ni se comparten entre hilos (skill vne-memory-model). Por eso esta API no toma un
// Arena*: el backend suelto reserva con SDL_LoadFile/SDL_free (el mismo allocador que
// SDL3 ya usa internamente, invisible a heap_guard igual que miniaudio, ver
// base/heap_guard.h) en vez de copiar a una arena del motor.

enum class PakEntryType : u8 { Texture, Font, Sound, Script, Map, Locale, Other };

// root termina en ".pak" -> backend empaquetado (lee el .pak entero una vez). Cualquier
// otra cosa se trata como el directorio que CONTIENE tanto assets_baked/ (lo que hornea
// vne_bake) como assets_src/ttf/ y assets_src/ogg/ (fuentes y audio, que no se hornean
// todavia -- SPEC.md #11) -- normalmente ".", el directorio de build. El espacio de
// nombres logico es identico al del backend empaquetado: "ttf/..." y "ogg/..." se buscan
// en assets_src/, cualquier otra cosa en assets_baked/ tal cual (ver bake_pack() en
// tools/bake/main.cpp, que arma el .pak con la misma regla). Sustituye cualquier montaje
// anterior. Solo se llama desde el hilo principal, antes de arrancar el hilo de IO (o con
// el hilo de IO parado).
void pak_mount(const char* root);
void pak_unmount();
bool pak_is_packed();

// Resuelve `logical_path` (relativo al root montado, con '/': "atlas_00.qoi",
// "ttf/NotoSans-subset.ttf", "demo.vnc") a un puntero de solo lectura y su tamano.
//
// *out_owned dice si el llamante es responsable de liberar el buffer con pak_release()
// cuando termine: true en backend suelto (el buffer es suyo, recien leido), false en
// backend empaquetado (el puntero cae dentro del bloque residente del .pak montado,
// nunca se libera aqui).
//
// Devuelve false si la ruta no existe en el backend activo. Nunca es fatal: el llamante
// decide el placeholder (SPEC.md #4).
[[nodiscard]] bool pak_resolve(const char* logical_path, const u8** out_data, usize* out_size,
                                bool* out_owned);

// No-op si owned es false (backend empaquetado). Emparejado siempre con pak_resolve().
void pak_release(const u8* data, bool owned);

// Conveniencia sobre pak_resolve() para el patron mas comun del hilo principal:
// script_load, map_mode::load y catalog_load necesitan que sus bytes vivan tanto como
// `arena` (normalmente g_arena_scene, que se resetea entera al cambiar de escena, sin
// destructores por objeto -- SPEC.md #6.1). En backend suelto copia a `arena` (y libera
// el buffer intermedio de pak_resolve, que si no se copiara se filtraria en cada
// recarga: nada llama arena_reset sobre un buffer que no vive dentro de la arena). En
// backend empaquetado devuelve el puntero residente del .pak directo, sin copiar nada
// (ya vive para siempre, copiarlo seria trabajo de sobra). SOLO para el hilo principal:
// para el hilo de IO usa pak_resolve() a secas (las arenas no son thread-safe).
[[nodiscard]] bool pak_resolve_into_arena(const char* logical_path, Arena* arena,
                                           const u8** out_data, usize* out_size);

// Para el caso raro en que un consumidor necesita una ruta de archivo real, no bytes --
// miniaudio, via audio.cpp, sigue usando ma_sound_init_from_file() en backend suelto para
// no tocar su camino ya probado (ADR-0036: un use-after-free real de miniaudio 0.11.21 se
// evita comprobando con fopen antes de llamar a la libreria, y esa comprobacion depende
// de tener una ruta). Escribe la ruta resuelta en out_path y devuelve true; devuelve
// false sin escribir nada si el backend activo es el empaquetado (ahi no hay ruta de
// archivo real que dar, solo un puntero a memoria).
[[nodiscard]] bool pak_resolve_loose_path(const char* logical_path, char* out_path,
                                           usize out_path_cap);
