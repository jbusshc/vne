#include <doctest/doctest.h>

// doctest necesita std::basic_ostream completo para poder imprimir un std::string_view
// en un mensaje de fallo de CHECK; <string_view> por si solo solo lo declara adelantado.
#include <ostream>

#include "script/lexer.h"

TEST_CASE("lexer: quita comentarios y recorta espacios") {
    auto lines = lex_lines("  @wait 1.0  # comentario\n\n:: etiqueta\n");
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].number == 1);
    CHECK(lines[0].text == "@wait 1.0");
    CHECK(lines[1].number == 3);
    CHECK(lines[1].text == ":: etiqueta");
}

TEST_CASE("lexer: un '#' dentro de comillas no es un comentario") {
    auto lines = lex_lines("\"Habitacion #3\"\n");
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].text == "\"Habitacion #3\"");
}

TEST_CASE("lexer: lineas vacias se omiten") {
    auto lines = lex_lines("\n\n   \n@end\n");
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].number == 4);
}

TEST_CASE("lexer: la indentacion se cuenta en multiplos de 4 espacios") {
    auto lines = lex_lines("@if x > 0\n    marta: hola\n        @end\n@end\n");
    REQUIRE(lines.size() == 4);
    CHECK(lines[0].indent == 0);
    CHECK(lines[1].indent == 1);
    CHECK(lines[2].indent == 2);
    CHECK(lines[3].indent == 0);
}
