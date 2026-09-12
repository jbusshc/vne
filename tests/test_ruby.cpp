#include <doctest/doctest.h>

#include "core/arena.h"
#include "test_fonts.h"
#include "text/layout.h"

TEST_CASE("furigana: {ruby=..} produce quads mas pequenos, encima del texto base") {
    REQUIRE(g_test_font_cjk.valid());
    Arena a = arena_create(256 * 1024, "test");

    // {ruby=かんじ}漢字{/ruby}
    const char* text =
        "{ruby=\xE3\x81\x8B\xE3\x82\x93\xE3\x81\x98}\xE6\xBC\xA2\xE5\xAD\x97{/ruby}";
    TextLayout l = text_layout(g_test_font_cjk, text, 10000.0f, &a);
    REQUIRE(l.count > 0);

    bool found_ruby = false;
    bool found_base = false;
    f32  min_ruby_y = 1e9f;
    f32  max_base_y = -1e9f;
    f32  ruby_h     = 0.0f;
    f32  base_h     = 0.0f;
    for (u32 i = 0; i < l.count; ++i) {
        const GlyphQuad& q = l.quads[i];
        if (q.is_ruby) {
            found_ruby = true;
            if (q.y < min_ruby_y) min_ruby_y = q.y;
            ruby_h = q.h;
        } else {
            found_base = true;
            if (q.y > max_base_y) max_base_y = q.y;
            base_h = q.h;
        }
    }
    CHECK(found_ruby);
    CHECK(found_base);
    CHECK(min_ruby_y < max_base_y);  // el ruby queda encima: Y menor es mas arriba
    CHECK(ruby_h < base_h);          // el ruby se dibuja mas pequeno que el texto base

    arena_destroy(&a);
}

TEST_CASE("furigana: sin {ruby=..} no se genera ningun quad marcado is_ruby") {
    REQUIRE(g_test_font_cjk.valid());
    Arena a = arena_create(256 * 1024, "test");

    const char* text = "\xE6\xBC\xA2\xE5\xAD\x97";  // 漢字, sin marcado
    TextLayout  l     = text_layout(g_test_font_cjk, text, 10000.0f, &a);
    REQUIRE(l.count > 0);
    for (u32 i = 0; i < l.count; ++i) {
        CHECK_FALSE(l.quads[i].is_ruby);
    }

    arena_destroy(&a);
}
