#pragma once
#include <string>
#include <vector>

#include "base/types.h"
#include "script/parser.h"

// Tabla de simbolos del proyecto (M14). **Un unico sitio donde un nombre se convierte en un
// id**, construido al hornear y compartido por todos los guiones.
//
// Antes habia seis mecanismos distintos para lo mismo, y todos tiraban el nombre al fabricar
// el id, de dos maneras igual de malas:
//
//   - `fnv1a(nombre) % capacidad` para variables, flags, pistas y mapas: dos nombres podian
//     caer en el mismo hueco. Medido en M13 (ADR-0063): `puntos` y `rumor` colisionan con
//     solo 25 nombres realistas, porque 512 huecos usados como espacio de hash valen, por la
//     paradoja del cumpleanos, unos 27 nombres y no 512.
//   - un interner local a CADA compilacion para actores, poses, fondos y hablantes: el id 3
//     significaba cosas distintas en dos guiones, asi que `GameState.actors[]` no podia
//     sobrevivir a un guardado si se cargaba con otro guion.
//
// Con una tabla del proyecto los dos problemas desaparecen de raiz en vez de mitigarse: el
// id es el INDICE en la tabla, asignado en orden, asi que no puede colisionar; es el mismo
// en todos los guiones; y el nombre se puede recuperar, que es lo que permite dibujar un
// actor o decirle a Lua que la variable que pide no existe.
//
// Y `k_max_vars = 512` (SPEC.md #8.2) deja de ser un espacio de hash y pasa a ser lo que
// aparenta: un tope de 512 variables distintas. Pasarse es un error al hornear con un
// mensaje claro, no una corrupcion probabilistica.
//
// Uso exclusivo de herramientas offline para construirla; el runtime solo la lee
// (src/vm/symbols_load.h).

enum class SymbolKind : u8 {
    Var,      // GameState.vars[], tope k_max_vars
    Flag,     // GameState.flags[], tope k_max_flags
    Actor,    // ActorSlot.actor_id
    Pose,     // ActorSlot.pose_id
    Bg,       // GameState.bg_id
    Speaker,  // Cmd::say.speaker_id
    Count,
};

// El id 0 esta RESERVADO en todas las tablas y no se asigna a ningun nombre: SPEC.md #8.2
// usa `actor_id == 0` para "slot vacio" y `bg_id == 0` para "sin fondo". Reservarlo en todas
// por igual evita tener que recordar en cual si y en cual no (en M13 costo un bug real: el
// primer actor de cada guion era indistinguible de un hueco vacio).
constexpr u16 k_symbol_id_none = 0;

struct SymbolTable {
    // names[kind][id - 1]: los nombres en orden de id.
    std::vector<std::string> names[static_cast<u32>(SymbolKind::Count)];

    // Devuelve el id de `name`, dandole uno nuevo si no lo tenia. Devuelve
    // k_symbol_id_none y deja *out_overflow a true si se paso del tope de esa clase.
    u16 intern(SymbolKind kind, const std::string& name, bool* out_overflow);

    // Devuelve el id existente, o k_symbol_id_none si el nombre no esta. No lo añade.
    u16 find(SymbolKind kind, const std::string& name) const;

    u32 count(SymbolKind kind) const;
};

// Tope de cada clase, el que fija SPEC.md #8.2 para lo que vive en GameState.
u32 symbol_kind_capacity(SymbolKind kind);

// Nombre legible de la clase, para los mensajes de error.
const char* symbol_kind_name(SymbolKind kind);

// Serializa a `.vnsym`. Formato:
//   magic 'VNSY' (4) | version (4) | count[SymbolKind::Count] (4 cada uno)
//   por cada clase, en orden: los nombres terminados en '\0', en orden de id
bool write_vnsym(const std::string& path, const SymbolTable& table);

// Nombres que usa un cuerpo `@lua` (escaneo de subcadenas, no un parser de Lua). Publico
// para poder testearlo por separado.
void symbols_collect_from_lua(const std::string& code, SymbolTable* table);

// Recorre las instrucciones ya parseadas de un guion y mete en la tabla todos los nombres
// que use. Se llama una vez por guion ANTES de compilar ninguno: por eso el paso de
// simbolos es su propio comando (`vne_bake symbols`) y no algo que haga `vne_bake script`,
// que ve un guion cada vez y no podria dar ids estables entre todos.
//
// Devuelve false y llena *out_error si alguna clase se pasa de su tope.
bool symbols_collect_from_script(const std::vector<ParsedInstr>& instructions,
                                  const std::string& file_name, SymbolTable* table,
                                  std::string* out_error);

// Lee un `.vnsym` ya horneado. Uso offline (`vne_bake script` lo necesita para compilar con
// los ids del proyecto); el runtime tiene su propio lector sin std::string (vm/symbols_load.h).
bool read_vnsym(const std::string& path, SymbolTable* out);

// Tabla construida a partir de un solo guion. Para tests y usos de una sola pieza, donde no
// hay proyecto que compartir: los ids son igual de validos, solo que su alcance es ese
// guion. El pipeline real SIEMPRE pasa la tabla del proyecto.
SymbolTable symbols_for_single_script(const std::vector<ParsedInstr>& instructions);
