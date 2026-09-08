#pragma once
#include "base/arena.h"
#include "base/types.h"

// Catalogo de localizacion (SPEC.md #9.2/#10, M10). Formato .vnl horneado a binario
// (ADR-0046: decision del usuario, no del agente — SPEC.md #14 lo marcaba
// explicitamente). "Todos los textos vienen del catalogo" (criterio de M10) se cumple
// asi: cmd.say.key_hash/ChoiceOption.key_hash (fnv1a del texto original) se buscan aqui
// primero; si no hay traduccion cargada o la clave no esta, cae al texto base horneado
// en el propio guion (nunca texto vacio).
//
// Formato en disco:
//   magic 'VNLC' (4) version (4) entry_count (4) string_pool_size (4)
//   u32 key_hash[entry_count]      (ordenado ascendente, busqueda binaria)
//   u32 text_offset[entry_count]   (indice en string_pool)
//   string_pool (UTF-8 terminado en '\0' por entrada)

constexpr u32 k_vnl_magic   = 0x434C4E56u;  // 'VNLC'
constexpr u32 k_vnl_version = 1;

enum class CatalogLoadResult : u8 { Ok, NotFound, BadFormat };

CatalogLoadResult catalog_load(const char* vnl_path, Arena* arena);

// Sin catalogo activo (idioma base, el mismo en que se autoraron los guiones): cualquier
// lookup cae siempre al texto base. Es el estado inicial.
void catalog_clear();

// nullptr si no hay catalogo activo o la clave no esta en el (el llamante debe caer al
// texto base del guion, nunca mostrar una cadena vacia).
const char* catalog_find(u32 key_hash);

// catalog_find() + fallback en una sola llamada: lo que casi todo el mundo quiere
// (VnMode/BacklogMode, M10 "todos los textos vienen del catalogo").
inline const char* catalog_resolve(u32 key_hash, const char* fallback_text) {
    const char* translated = catalog_find(key_hash);
    return translated != nullptr ? translated : fallback_text;
}

// Se incrementa en cada catalog_load()/catalog_clear() (cambio de idioma). VnMode/
// BacklogMode lo comparan junto a su propio "layout construido para el pc X" para saber
// si tienen que reconstruir el TextLayout aunque el pc no haya cambiado (cambio de
// idioma en caliente, criterio de M10) sin relayoutear en ningun otro frame (skill
// vne-rendering).
u32 catalog_generation();
