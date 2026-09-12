#include <doctest/doctest.h>

#include "core/arena.h"
#include "test_fonts.h"
#include "text/glyph_cache.h"
#include "text/layout.h"

TEST_CASE("glyph_cache: una segunda subida en el mismo frame no crashea (ADR-0016/0019)") {
    REQUIRE(g_test_font_latin.valid());
    Arena a = arena_create(256 * 1024, "test");

    // sokol_gfx solo admite una sg_update_image por imagen y por frame. Antes de
    // ADR-0019, una segunda llamada a glyph_cache_flush_dirty_pages() en el mismo frame
    // abortaba el proceso; ahora debe ignorarse silenciosamente (con un aviso en el log).
    text_layout(g_test_font_latin, "Zw", 10000.0f, &a);
    glyph_cache_begin_frame();
    glyph_cache_flush_dirty_pages();
    glyph_cache_flush_dirty_pages();  // no debe abortar

    CHECK(true);

    arena_destroy(&a);
}
