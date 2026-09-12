#include <doctest/doctest.h>

#include "core/arena.h"
#include "test_fonts.h"
#include "text/layout.h"

TEST_CASE("marcado: {color=#rrggbb} cambia el color y {/color} lo restaura") {
    REQUIRE(g_test_font_latin.valid());
    Arena a = arena_create(256 * 1024, "test");

    TextLayout l = text_layout(g_test_font_latin, "ab{color=#ff0000}cd{/color}ef", 10000.0f, &a,
                                0xFFFFFFFFu);
    REQUIRE(l.count > 0);
    CHECK(l.quads[0].color == 0xFFFFFFFFu);

    bool found_red  = false;
    bool found_back = false;
    for (u32 i = 0; i < l.count; ++i) {
        if (l.quads[i].color == 0xFF0000FFu) found_red = true;
    }
    found_back = l.quads[l.count - 1].color == 0xFFFFFFFFu;
    CHECK(found_red);
    CHECK(found_back);

    arena_destroy(&a);
}

TEST_CASE("marcado: {b}/{/b} se parsean sin romper el layout") {
    REQUIRE(g_test_font_latin.valid());
    Arena a = arena_create(256 * 1024, "test");

    TextLayout l = text_layout(g_test_font_latin, "normal {b}bold{/b} normal", 10000.0f, &a);
    CHECK(l.count > 0);
    CHECK(l.line_count == 1);

    arena_destroy(&a);
}

TEST_CASE("marcado: {w=n} y {speed=n} se descartan sin producir glifos ni romper el texto") {
    REQUIRE(g_test_font_latin.valid());
    Arena a = arena_create(256 * 1024, "test");

    TextLayout with_tags    = text_layout(g_test_font_latin, "a{w=0.5}b{speed=2}c", 10000.0f, &a);
    TextLayout without_tags = text_layout(g_test_font_latin, "abc", 10000.0f, &a);
    CHECK(with_tags.count == without_tags.count);

    arena_destroy(&a);
}

TEST_CASE("marcado: una llave sin cerrar se trata como texto literal") {
    REQUIRE(g_test_font_latin.valid());
    Arena a = arena_create(256 * 1024, "test");

    TextLayout l = text_layout(g_test_font_latin, "abc { def", 10000.0f, &a);
    CHECK(l.count > 0);

    arena_destroy(&a);
}
