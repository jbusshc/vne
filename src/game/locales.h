#pragma once
#include "base/types.h"

// Los idiomas del proyecto, en una tabla (M14). Sustituye al `locale_index == 1` que habia
// en menu_mode.cpp, que asumia que **cualquier idioma que no fuera espanol era japones** y
// por tanto necesitaba la fuente CJK: con un tercer idioma latino, ese caso especial habria
// cargado la fuente equivocada en silencio.
//
// `id` es lo que se guarda en config.ini, no el indice: el indice cambia si se reordena o se
// añade un idioma, y entonces una preferencia guardada pasaria a significar otro idioma.
// Mismo razonamiento que ADR-0034 con las pistas de musica y ADR-0067 con los simbolos —
// **un identificador persistido no puede ser una posicion**.

struct LocaleDesc {
    const char* id;          // "es", "ja": lo que persiste en config.ini
    const char* display;     // lo que ve el jugador en el menu
    const char* vnl;         // catalogo horneado; nullptr = idioma base, sin traduccion
    bool        needs_cjk;   // que fuente usar (la latina o la CJK)
};

inline constexpr LocaleDesc k_locales[] = {
    // El idioma base tambien tiene catalogo (M14). Antes era `nullptr` y se caia al texto del
    // guion, lo que obligaba a que cualquier cosa que mostrara texto tuviera el guion a mano
    // — imposible para el backlog, que guarda lineas de guiones que quiza ya no estan
    // cargados. Con es.vnl, resolver una linea es solo su key_hash.
    {"es", "Espanol", "es.vnl", false},
    {"ja", "Nihongo (placeholder)", "ja.vnl", true},
};
inline constexpr u32 k_locale_count = sizeof(k_locales) / sizeof(k_locales[0]);

// Indice del idioma con ese id, o 0 (el idioma base) si no se reconoce. Nunca falla: un
// config.ini editado a mano con un idioma que ya no existe cae al base en vez de dejar el
// juego sin texto.
u32 locale_index_from_id(const char* id);
