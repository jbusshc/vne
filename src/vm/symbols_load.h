#pragma once
#include "core/arena.h"
#include "core/types.h"

// Lector en runtime de la tabla de simbolos del proyecto (`project.vnsym`, M14). El escritor
// vive en src/script/symbols.h y solo lo usa `sz_bake`; aqui no hay std::string ni
// asignacion, como en el resto del runtime.
//
// La tabla es UN solo sitio donde un nombre se convierte en un id, compartido por todos los
// guiones. Sustituye a los seis mecanismos distintos que habia antes (hash modulo para
// variables, flags, pistas y mapas; un interner local a cada compilacion para actores,
// poses, fondos y hablantes), que tenian el mismo defecto de raiz: tiraban el nombre al
// fabricar el id. Ver ADR-0067.

// Debe coincidir con SymbolKind en src/script/symbols.h (duplicado a proposito, mismo
// patron que el resto de formatos: lo escribe una herramienta y lo lee el juego).
enum class SymKind : u8 { Var, Flag, Actor, Pose, Bg, Speaker, Count };

// Carga la tabla a traves del backend de assets activo (nombre logico, no ruta). Deja los
// datos residentes en `arena`. Devuelve false si falta o esta corrupta; el juego sigue
// funcionando, simplemente no podra resolver nombres (los actores no se dibujaran).
bool symbols_load(Arena* arena);

// Nombre de un id, o "" si el id es 0 ("ninguno") o esta fuera de la tabla. Nunca nullptr:
// el llamante dibuja menos, no revienta (SPEC.md #4).
const char* symbols_name(SymKind kind, u16 id);

// Id de un nombre, o 0 si no esta. Lo usa Lua (vn.get_var/set_var), que trabaja con nombres
// en vez de ids. Busqueda lineal sobre unos pocos cientos de entradas y fuera del bucle de
// frame; no asigna.
u16 symbols_id(SymKind kind, const char* name);

u32 symbols_count(SymKind kind);
