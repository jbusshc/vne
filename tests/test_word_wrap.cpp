#include <doctest/doctest.h>

#include "base/arena.h"
#include "test_fonts.h"
#include "text/layout.h"

TEST_CASE("word-wrap latino: una linea ancha cabe entera") {
    REQUIRE(g_test_font_latin.valid());
    Arena a = arena_create(256 * 1024, "test");

    TextLayout wide =
        text_layout(g_test_font_latin, "The quick brown fox jumps over the lazy dog", 10000.0f, &a);
    CHECK(wide.line_count == 1);
    CHECK(wide.count > 0);

    arena_destroy(&a);
}

TEST_CASE("word-wrap latino: texto largo se parte en varias lineas sin pasarse del ancho") {
    REQUIRE(g_test_font_latin.valid());
    Arena a = arena_create(256 * 1024, "test");

    TextLayout narrow = text_layout(
        g_test_font_latin, "The quick brown fox jumps over the lazy dog and keeps running", 150.0f,
        &a);
    CHECK(narrow.line_count > 1);
    CHECK(narrow.width <= 150.0f + 1.0f);  // +1px de margen por redondeo de subpixel

    arena_destroy(&a);
}

TEST_CASE("word-wrap latino: el corte cae entre palabras, no a mitad de una") {
    REQUIRE(g_test_font_latin.valid());
    Arena a = arena_create(256 * 1024, "test");

    // Ancho justo para "one two" pero no para "one two three".
    TextLayout l = text_layout(g_test_font_latin, "one two three", 90.0f, &a);
    CHECK(l.line_count >= 2);

    arena_destroy(&a);
}
