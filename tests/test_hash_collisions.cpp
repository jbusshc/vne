#include <doctest/doctest.h>

#include <ostream>
#include <string>

#include "base/hash.h"
#include "script/compiler.h"
#include "script/hash_collisions.h"
#include "script/parser.h"
#include "vm/state.h"

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

namespace {

// Busca dos nombres de variable distintos que colisionen de verdad en GameState.vars
// (fnv1a % 512). No se inventa un par a mano: se buscan, para que el test siga siendo
// valido si algun dia cambia la funcion de hash o la capacidad del array.
bool find_colliding_var_names(std::string* out_a, std::string* out_b) {
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

TEST_CASE("compilador: dos variables que colisionan en el mismo var_id dan error (M13)") {
    // Criterio de SPEC.md #12, M13: "Dos nombres de variable que colisionan en el mismo
    // var_id producen un error al hornear".
    std::string a, b;
    REQUIRE(find_colliding_var_names(&a, &b));
    REQUIRE(fnv1a_u32(a.c_str()) % k_max_vars == fnv1a_u32(b.c_str()) % k_max_vars);
    MESSAGE("par que colisiona: '" << a << "' y '" << b << "' -> hueco "
                                   << (fnv1a_u32(a.c_str()) % k_max_vars));

    std::string source = "@set " + a + " = 1\n@set " + b + " = 2\n@end\n";
    ParseResult parsed = parse_script(source, "colision.vns");
    REQUIRE(parsed.ok());

    CompileResult compiled = compile_instructions(parsed.instructions, "colision.vns");
    REQUIRE_FALSE(compiled.ok());
    REQUIRE(compiled.errors.size() == 1);
    CHECK(compiled.errors[0].file == "colision.vns");
    CHECK(compiled.errors[0].line == 2);  // la segunda, que es la que choca
    // El mensaje nombra las DOS variables: sin eso no se puede arreglar.
    CHECK(compiled.errors[0].message.find(a) != std::string::npos);
    CHECK(compiled.errors[0].message.find(b) != std::string::npos);
}

TEST_CASE("compilador: usar la misma variable muchas veces no da falso positivo") {
    // El caso normal: una variable leida y escrita repetidamente. Si esto fallara, la
    // deteccion seria inservible.
    const char* source =
        "@set confianza = 0\n"
        "@add confianza 1\n"
        "@if confianza >= 1\n"
        "    \"algo\"\n"
        "@end\n"
        "@add confianza -1\n"
        "@end\n";
    ParseResult parsed = parse_script(source, "normal.vns");
    REQUIRE(parsed.ok());

    CompileResult compiled = compile_instructions(parsed.instructions, "normal.vns");
    for (const CompileError& e : compiled.errors) {
        MESSAGE("error inesperado: " << e.message);
    }
    CHECK(compiled.ok());
}

TEST_CASE("hash: cuantos nombres de variable realistas caben antes de la primera colision") {
    // No es un criterio de M13: es una MEDICION, porque el resultado cambia cuanto importa
    // esta deteccion. Con 512 huecos la paradoja del cumpleanos dice que la probabilidad de
    // colision pasa del 50% hacia los 27 nombres distintos — o sea, un juego real con tres
    // docenas de variables es bastante probable que choque.
    const char* words[] = {
        "confianza", "valor",  "miedo",  "oro",     "hp",      "mana",   "dia",    "hora",
        "piso",      "llave",  "puerta", "carta",   "secreto", "rumor",  "amistad", "rencor",
        "pista",     "farol",  "tren",   "lluvia",  "nombre",  "edad",   "turno",  "racha",
        "puntos",    "vidas",  "nivel",  "fase",    "combo",   "suerte", "sed",    "hambre",
        "frio",      "calor",  "ruido",  "luz",     "sombra",  "eco",    "humo",   "ceniza",
    };
    constexpr u32 k_word_count = sizeof(words) / sizeof(words[0]);

    HashCollisionCheck check;
    std::string        previous;
    u32                names_until_collision = 0;
    for (u32 i = 0; i < k_word_count; ++i) {
        u32 id = fnv1a_u32(words[i]) % k_max_vars;
        if (!check.add(words[i], id, &previous)) {
            names_until_collision = i + 1;
            MESSAGE("colision con " << names_until_collision << " nombres realistas: '"
                                     << std::string(words[i]) << "' y '" << previous
                                     << "' en el hueco " << id);
            break;
        }
    }
    if (names_until_collision == 0) {
        MESSAGE("sin colision en " << k_word_count << " nombres realistas");
    }
    // Sin CHECK sobre el numero: depende de la funcion de hash y no es un contrato. Lo que
    // importa es que quede medido en la salida de los tests.
    CHECK(k_word_count > 0);
}
