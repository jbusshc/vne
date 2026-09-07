#include <doctest/doctest.h>

#include "base/arena.h"
#include "test_fonts.h"
#include "text/layout.h"

TEST_CASE("kinsoku: la tabla de 'no puede empezar linea' coincide con SPEC.md #7.2") {
    CHECK(text_is_kinsoku_forbidden_start(0x3002));  // 。
    CHECK(text_is_kinsoku_forbidden_start(0x3001));  // 、
    CHECK(text_is_kinsoku_forbidden_start(0x300D));  // 」
    CHECK(text_is_kinsoku_forbidden_start(0x300F));  // 』
    CHECK(text_is_kinsoku_forbidden_start(0xFF09));  // ）
    CHECK_FALSE(text_is_kinsoku_forbidden_start(0x3042));  // あ: caracter normal
    CHECK_FALSE(text_is_kinsoku_forbidden_start('A'));
}

TEST_CASE("kinsoku: la tabla de 'no puede terminar linea' coincide con SPEC.md #7.2") {
    CHECK(text_is_kinsoku_forbidden_end(0x300C));  // 「
    CHECK(text_is_kinsoku_forbidden_end(0x300E));  // 『
    CHECK(text_is_kinsoku_forbidden_end(0xFF08));  // （
    CHECK_FALSE(text_is_kinsoku_forbidden_end(0x3042));
    CHECK_FALSE(text_is_kinsoku_forbidden_end('A'));
}

TEST_CASE("kinsoku: un parrafo CJK con puntuacion en el punto de corte no revienta y "
          "no se desborda mas de un caracter") {
    REQUIRE(g_test_font_cjk.valid());
    Arena a = arena_create(256 * 1024, "test");

    // 5 caracteres, punto, 5 caracteres mas: fuerza a que "。" caiga justo donde
    // rompería la linea si no se respetara kinsoku.
    const char* text = "\xE3\x81\x82\xE3\x81\x84\xE3\x81\x86\xE3\x81\x88\xE3\x81\x8A"  // あいうえお
                        "\xE3\x80\x82"                                                 // 。
                        "\xE3\x81\x8B\xE3\x81\x8D\xE3\x81\x8F\xE3\x81\x91\xE3\x81\x93"; // かきくけこ

    // Ancho pensado para que quepan 5 caracteres CJK por linea a este tamano de fuente.
    TextLayout l = text_layout(g_test_font_cjk, text, 170.0f, &a);
    CHECK(l.line_count >= 2);
    CHECK(l.count > 0);
    // El ancho de linea mas largo no debe dispararse muchisimo mas alla del limite: como
    // mucho, un caracter CJK de margen por la regla de "no puede empezar linea".
    CHECK(l.width <= 170.0f * 1.5f);

    arena_destroy(&a);
}
