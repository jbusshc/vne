#pragma once
#include <string>
#include <unordered_map>

#include "core/types.h"

// Deteccion de colisiones de hash al hornear (M13). Varios sitios del motor resuelven un
// nombre a un id por `fnv1a % capacidad` sin tabla de interning, y los ADR que lo decidieron
// **aceptaron el riesgo de colision sin ninguna deteccion**:
//
//   ADR-0029  nombres de variable y de flag  -> fnv1a % 512 / % 2048
//   ADR-0034  pistas de musica               -> fnv1a % 65536
//   ADR-0047  claves de catalogo             -> fnv1a completo de 32 bits
//
// Una colision hoy no da ningun sintoma en el sitio del problema: dos variables distintas
// comparten hueco y el guion se comporta como si una escribiera sobre la otra, o dos pistas
// de musica se confunden al restaurar una partida. Es un bug de logica imposible de rastrear
// desde el sintoma. Detectarlo cuesta un diccionario en una herramienta offline.
//
// Es deliberado que esto viva solo en `sz_content`: el juego no comprueba nada de
// esto en runtime, igual que no valida nombres de asset (skill vne-script-dsl, "cero parsing
// en release").
struct HashCollisionCheck {
    // Registra `name` bajo `id`. Devuelve true si no hay problema (nombre nuevo, o el mismo
    // nombre repetido, que no es colision). Si otro nombre DISTINTO ya ocupaba ese id,
    // devuelve false y deja ese nombre en *out_previous.
    bool add(const std::string& name, u32 id, std::string* out_previous);

private:
    std::unordered_map<u32, std::string> by_id_;
};
