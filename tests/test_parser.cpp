#include <doctest/doctest.h>

// Ver test_lexer.cpp: doctest necesita <ostream> completo para imprimir std::string en un
// mensaje de fallo de CHECK.
#include <ostream>

#include "script/parser.h"

TEST_CASE("parser: guion basico con todos los comandos de M3 se parsea sin errores") {
    const char* src =
        ":: inicio\n"
        "@bg fondo_a fade 0.5\n"
        "@show personaje pose_a slot 0 fade 0.3\n"
        "personaje: Hola.\n"
        "\"Sin hablante.\"\n"
        "@hide slot 0 fade 0.2\n"
        "@wait 1.0\n"
        "@jump inicio\n"
        "@end\n";
    ParseResult r = parse_script(src, "test.vns");
    CHECK(r.ok());
    REQUIRE(r.instructions.size() == 9);
    CHECK(r.instructions[0].kind == InstrKind::Label);
    CHECK(r.instructions[0].name == "inicio");
    CHECK(r.instructions[1].kind == InstrKind::Bg);
    CHECK(r.instructions[1].bg == "fondo_a");
    CHECK(r.instructions[2].kind == InstrKind::Show);
    CHECK(r.instructions[2].actor == "personaje");
    CHECK(r.instructions[2].pose == "pose_a");
    CHECK(r.instructions[2].slot == 0);
    CHECK(r.instructions[3].kind == InstrKind::Say);
    CHECK(r.instructions[3].speaker == "personaje");
    CHECK(r.instructions[3].text == "Hola.");
    CHECK(r.instructions[4].kind == InstrKind::Say);
    CHECK(r.instructions[4].speaker.empty());
    CHECK(r.instructions[4].text == "Sin hablante.");
    CHECK(r.instructions[5].kind == InstrKind::Hide);
    CHECK(r.instructions[6].kind == InstrKind::Wait);
    CHECK(r.instructions[6].seconds == doctest::Approx(1.0f));
    CHECK(r.instructions[7].kind == InstrKind::Jump);
    CHECK(r.instructions[7].name == "inicio");
    CHECK(r.instructions[8].kind == InstrKind::End);
}

TEST_CASE("parser: @jump a una etiqueta desconocida es error con archivo y linea") {
    ParseResult r = parse_script("@jump nunca_declarada\n@end\n", "roto.vns");
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.errors.size() == 1);
    CHECK(r.errors[0].file == "roto.vns");
    CHECK(r.errors[0].line == 1);
}

TEST_CASE("parser: un comando desconocido es error de compilacion") {
    ParseResult r = parse_script("@no_existe 1 2 3\n", "x.vns");
    REQUIRE_FALSE(r.ok());
    CHECK(r.errors[0].line == 1);
}

TEST_CASE("parser: una cadena de dialogo sin cerrar es error") {
    ParseResult r = parse_script("\"sin cerrar\n", "x.vns");
    REQUIRE_FALSE(r.ok());
}
