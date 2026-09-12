#include <doctest/doctest.h>

#include <ostream>
#include <string>

#include "core/hash.h"
#include "script/compiler.h"
#include "script/hash_collisions.h"
#include "script/symbols.h"
#include "script/parser.h"
#include "formats/state.h"

// Deteccion de colisiones de hash al hornear (M13). ADR-0029/0034/0047 aceptaron el riesgo
// sin ninguna deteccion: dos nombres que caen en el mismo id se pisan en silencio y el
// sintoma no apunta al sitio del problema.

TEST_CASE("HashCollisionCheck: el mismo nombre repetido no es colision") {
    HashCollisionCheck check;
    std::string        previous;
    CHECK(check.add("valor", 7, &previous));
    CHECK(check.add("valor", 7, &previous));  // otra vez el mismo: legitimo
}

TEST_CASE("HashCollisionCheck: dos nombres distintos en el mismo id si lo son") {
    HashCollisionCheck check;
    std::string        previous;
    REQUIRE(check.add("primera", 7, &previous));
    CHECK_FALSE(check.add("segunda", 7, &previous));
    // Devuelve el nombre que ya estaba, para que el mensaje pueda nombrar a los DOS: con
    // uno solo, quien lee el error no sabe con que ha chocado.
    CHECK(previous == "primera");
}

TEST_CASE("HashCollisionCheck: ids distintos no interfieren") {
    HashCollisionCheck check;
    std::string        previous;
    CHECK(check.add("a", 1, &previous));
    CHECK(check.add("b", 2, &previous));
    CHECK(check.add("c", 3, &previous));
}

// --- Las colisiones de variables y banderas YA NO EXISTEN (M14, ADR-0067) --------------
//
// M13 las detectaba al hornear porque `fnv1a % 512` podia mandar dos nombres al mismo hueco
// (medido: `puntos` y `rumor` con solo 25 nombres). M14 las elimina de raiz: el id es el
// indice en la tabla de simbolos del proyecto, asignado en orden, asi que dos nombres
// distintos no pueden compartirlo. Estos tests fijan esa propiedad.
//
// HashCollisionCheck sigue vivo para las pistas de musica y los mapas, que siguen usando
// hash porque sus ids vienen del nombre de archivo y no de un guion.

namespace {

// Dos nombres que colisionaban de verdad bajo el esquema viejo (fnv1a % k_max_vars).
bool find_old_colliding_pair(std::string* out_a, std::string* out_b) {
    std::string by_id[k_max_vars];
    for (u32 i = 0; i < 200000; ++i) {
        std::string name = "v" + std::to_string(i);
        u32         id   = fnv1a_u32(name.c_str()) % k_max_vars;
        if (!by_id[id].empty()) {
            *out_a = by_id[id];
            *out_b = name;
            return true;
        }
        by_id[id] = name;
    }
    return false;
}

}  // namespace

TEST_CASE("simbolos: dos nombres que colisionaban en el esquema viejo tienen ids distintos") {
    std::string a, b;
    REQUIRE(find_old_colliding_pair(&a, &b));
    // Colisionaban de verdad con el hash: esa era la premisa del problema.
    REQUIRE(fnv1a_u32(a.c_str()) % k_max_vars == fnv1a_u32(b.c_str()) % k_max_vars);
    MESSAGE("par que colisionaba con el hash viejo: '" << a << "' y '" << b << "'");

    std::string source   = "@set " + a + " = 1\n@set " + b + " = 2\n@end\n";
    ParseResult parsed   = parse_script(source, "simbolos.vns");
    REQUIRE(parsed.ok());
    SymbolTable symbols  = symbols_for_single_script(parsed.instructions);

    // Con la tabla, ids distintos y consecutivos. Y compila sin un solo error: ya no hay
    // nada que detectar porque no hay nada que pueda ir mal.
    u16 id_a = symbols.find(SymbolKind::Var, a);
    u16 id_b = symbols.find(SymbolKind::Var, b);
    CHECK(id_a != 0);
    CHECK(id_b != 0);
    CHECK(id_a != id_b);

    CompileResult compiled = compile_instructions(parsed.instructions, "simbolos.vns", symbols);
    for (const CompileError& e : compiled.errors) {
        MESSAGE("error inesperado: " << e.message);
    }
    CHECK(compiled.ok());
}

TEST_CASE("simbolos: el id 0 esta reservado y nunca se asigna a un nombre") {
    // SPEC.md #8.2 usa actor_id == 0 para "slot vacio" y bg_id == 0 para "sin fondo".
    // Reservarlo en TODAS las clases por igual evita tener que recordar en cual si y en
    // cual no: en M13 esa asimetria costo un bug real (el primer actor de cada guion era
    // indistinguible de un hueco vacio).
    SymbolTable table;
    bool        overflow = false;
    CHECK(table.intern(SymbolKind::Var, "primera", &overflow) == 1);
    CHECK(table.intern(SymbolKind::Actor, "marta", &overflow) == 1);
    CHECK(table.find(SymbolKind::Var, "no_existe") == k_symbol_id_none);
    CHECK_FALSE(overflow);
}

TEST_CASE("simbolos: el mismo nombre siempre da el mismo id; nombres distintos, ids distintos") {
    SymbolTable table;
    bool        overflow = false;
    u16         a1       = table.intern(SymbolKind::Var, "confianza", &overflow);
    u16         b        = table.intern(SymbolKind::Var, "miedo", &overflow);
    u16         a2       = table.intern(SymbolKind::Var, "confianza", &overflow);
    CHECK(a1 == a2);
    CHECK(a1 != b);
    CHECK(table.count(SymbolKind::Var) == 2);
}

TEST_CASE("simbolos: pasarse del tope de una clase se avisa, no se corrompe en silencio") {
    // 512 variables dejan de ser un espacio de hash (util al 5%) para ser una cuenta real:
    // caben 512 y la 513 es un error con mensaje, no una corrupcion probabilistica.
    SymbolTable table;
    bool        overflow = false;
    for (u32 i = 0; i < k_max_vars; ++i) {
        table.intern(SymbolKind::Var, "v" + std::to_string(i), &overflow);
        REQUIRE_FALSE(overflow);
    }
    CHECK(table.count(SymbolKind::Var) == k_max_vars);

    u16 id = table.intern(SymbolKind::Var, "la_que_sobra", &overflow);
    CHECK(overflow);
    CHECK(id == k_symbol_id_none);
}

