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

TEST_CASE("parser: @if/@else/@end se traduce a JumpIf+Jump+etiquetas sinteticas") {
    const char* src =
        "@if confianza >= 3\n"
        "    marta: Rama alta.\n"
        "@else\n"
        "    marta: Rama baja.\n"
        "@end\n"
        "@end\n";
    ParseResult r = parse_script(src, "t.vns");
    REQUIRE(r.ok());
    // JumpIf(invertido), Say(alta), Jump(end), Label(else), Say(baja), Label(end), End.
    REQUIRE(r.instructions.size() == 7);
    CHECK(r.instructions[0].kind == InstrKind::JumpIf);
    CHECK(r.instructions[0].condition.var == "confianza");
    CHECK(r.instructions[0].condition.op == CmpOp::Ge);
    CHECK(r.instructions[0].condition.rhs == 3);
    CHECK(r.instructions[0].invert_condition);
    CHECK(r.instructions[0].name == r.instructions[3].name);  // salta al Label(else)
    CHECK(r.instructions[1].kind == InstrKind::Say);
    CHECK(r.instructions[2].kind == InstrKind::Jump);
    CHECK(r.instructions[2].name == r.instructions[5].name);  // salta al Label(end)
    CHECK(r.instructions[3].kind == InstrKind::Label);
    CHECK(r.instructions[4].kind == InstrKind::Say);
    CHECK(r.instructions[5].kind == InstrKind::Label);
    CHECK(r.instructions[6].kind == InstrKind::End);
}

TEST_CASE("parser: @if sin @else tambien cierra bien (dos etiquetas adyacentes)") {
    ParseResult r = parse_script("@if x > 0\n    \"cuerpo\"\n@end\n@end\n", "t.vns");
    REQUIRE(r.ok());
    REQUIRE(r.instructions.size() == 6);  // JumpIf, Say, Jump, Label, Label, End
    CHECK(r.instructions[0].kind == InstrKind::JumpIf);
    CHECK(r.instructions[3].kind == InstrKind::Label);
    CHECK(r.instructions[4].kind == InstrKind::Label);
}

TEST_CASE("parser: @choice con opciones y condicion opcional") {
    const char* src =
        ":: destino_a\n"
        "@end\n"
        ":: destino_b\n"
        "@end\n"
        "@choice\n"
        "    \"Opcion A\" -> destino_a\n"
        "    \"Opcion B\" if valor > 2 -> destino_b\n"
        "@end\n"
        "@end\n";
    ParseResult r = parse_script(src, "t.vns");
    REQUIRE(r.ok());
    // Label(a), End(a-no,es solo Label real: en realidad el primer @end cierra el
    // guion... para evitar esa ambiguedad este test usa jump como cuerpo en vez de @end.
    bool found_choice = false;
    for (const auto& instr : r.instructions) {
        if (instr.kind == InstrKind::Choice) {
            found_choice = true;
            REQUIRE(instr.choice_options.size() == 2);
            CHECK(instr.choice_options[0].text == "Opcion A");
            CHECK(instr.choice_options[0].target == "destino_a");
            CHECK_FALSE(instr.choice_options[0].has_condition);
            CHECK(instr.choice_options[1].text == "Opcion B");
            CHECK(instr.choice_options[1].target == "destino_b");
            REQUIRE(instr.choice_options[1].has_condition);
            CHECK(instr.choice_options[1].condition.var == "valor");
            CHECK(instr.choice_options[1].condition.op == CmpOp::Gt);
            CHECK(instr.choice_options[1].condition.rhs == 2);
        }
    }
    CHECK(found_choice);
}

TEST_CASE("parser: @set, @add, @call, @return, @lua") {
    const char* src =
        ":: rutina\n"
        "@return\n"
        "@set confianza = 5\n"
        "@add confianza 1\n"
        "@call rutina\n"
        "@lua vn.set_var(\"x\", 1)\n"
        "@end\n";
    ParseResult r = parse_script(src, "t.vns");
    REQUIRE(r.ok());
    REQUIRE(r.instructions.size() == 7);
    CHECK(r.instructions[0].kind == InstrKind::Label);
    CHECK(r.instructions[1].kind == InstrKind::Return);
    CHECK(r.instructions[2].kind == InstrKind::SetVar);
    CHECK(r.instructions[2].var == "confianza");
    CHECK(r.instructions[2].value == 5);
    CHECK(r.instructions[3].kind == InstrKind::AddVar);
    CHECK(r.instructions[3].var == "confianza");
    CHECK(r.instructions[3].value == 1);
    CHECK(r.instructions[4].kind == InstrKind::Call);
    CHECK(r.instructions[4].name == "rutina");
    CHECK(r.instructions[5].kind == InstrKind::Lua);
    CHECK(r.instructions[5].text == "vn.set_var(\"x\", 1)");
}

TEST_CASE("parser: @sfx, @bgm y @stopbgm") {
    const char* src =
        "@sfx puerta_cierra.wav\n"
        "@bgm tema_tenso fade 1.0\n"
        "@stopbgm fade 0.5\n"
        "@stopbgm\n"
        "@end\n";
    ParseResult r = parse_script(src, "t.vns");
    REQUIRE(r.ok());
    REQUIRE(r.instructions.size() == 5);
    CHECK(r.instructions[0].kind == InstrKind::Sfx);
    CHECK(r.instructions[0].sound == "puerta_cierra.wav");
    CHECK(r.instructions[1].kind == InstrKind::Bgm);
    CHECK(r.instructions[1].sound == "tema_tenso");
    CHECK(r.instructions[1].fade == doctest::Approx(1.0f));
    CHECK(r.instructions[2].kind == InstrKind::StopBgm);
    CHECK(r.instructions[2].fade == doctest::Approx(0.5f));
    CHECK(r.instructions[3].kind == InstrKind::StopBgm);
    CHECK(r.instructions[3].fade == doctest::Approx(0.0f));
}

TEST_CASE("parser: etiqueta desconocida en un @choice tambien es error de compilacion") {
    ParseResult r = parse_script(
        "@choice\n    \"opcion\" -> nunca_declarada\n@end\n@end\n", "roto.vns");
    REQUIRE_FALSE(r.ok());
}
