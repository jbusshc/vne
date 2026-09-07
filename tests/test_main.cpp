#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "base/arena.h"
#include "gfx/gfx.h"
#include "platform/window.h"
#include "test_config.h"
#include "test_fonts.h"
#include "text/font.h"

// Runner manual en vez de DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN: los tests de texto (M2)
// necesitan un FontHandle real, y glyph_cache sube el atlas de glifos a una textura de
// sokol_gfx, asi que hace falta un contexto grafico real (aunque sea una ventana oculta)
// antes de correr ningun test. Se crea una vez aqui, no por test.

FontHandle g_test_font_latin;
FontHandle g_test_font_cjk;

int main(int argc, char** argv) {
    doctest::Context context;
    context.applyCommandLine(argc, argv);

    g_arena_perm  = arena_create(64ull * 1024 * 1024, "test_perm");
    g_arena_scene = arena_create(64ull * 1024 * 1024, "test_scene");
    g_arena_frame = arena_create(8ull * 1024 * 1024, "test_frame");

    PlatformWindow window{};
    bool           have_window = platform_window_create(&window, "vne tests", 64, 64);
    bool           have_gfx    = have_window && gfx_init(&window);
    if (have_gfx) {
        // Algunos tests de M2 llaman a text_draw() (que llama a gfx_draw_sprite()), asi
        // que hace falta al menos un gfx_begin_frame() para que la cola de sprites de
        // g_arena_frame este inicializada.
        gfx_begin_frame();
        g_test_font_latin = text_load_font(VNE_SOURCE_DIR "/assets_src/ttf/NotoSans.ttf", 32);
        g_test_font_cjk   = text_load_font(VNE_SOURCE_DIR "/assets_src/ttf/NotoSansJP.ttf", 32);
    }

    int result = context.run();

    if (context.shouldExit()) {
        return result;
    }

    if (have_gfx) {
        gfx_shutdown();
    }
    if (have_window) {
        platform_window_destroy(&window);
    }
    arena_destroy(&g_arena_frame);
    arena_destroy(&g_arena_scene);
    arena_destroy(&g_arena_perm);

    return result;
}
