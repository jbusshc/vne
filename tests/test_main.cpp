#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "assets/pak.h"
#include "base/arena.h"
#include "gfx/gfx.h"
#include "platform/window.h"
#include "test_config.h"
#include "test_fonts.h"
#include "text/font.h"
#include "text/glyph_cache.h"
#include "audio/audio.h"
#include "script/lua_bindings.h"
#include "vm/backlog.h"
#include "vm/rollback.h"

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
    // "." y no VNE_SOURCE_DIR (que en la practica solo hacia falta para los .ttf, ver
    // git blame): el directorio de build ya se autocontiene con assets_baked/ y una
    // copia propia de assets_src/ttf|ogg (CMakeLists.txt las copia ahi), exactamente
    // igual que main.cpp (M11). Antes de audio_init(): scan_music_catalog() pasara a
    // resolver por aqui tambien.
    pak_mount(".");
    rollback_init(&g_rollback);
    backlog_reset(&g_backlog);
    lua_init();
    audio_init();

    PlatformWindow window{};
    bool           have_window = platform_window_create(&window, "vne tests", 64, 64);
    bool           have_gfx    = have_window && gfx_init(&window);
    if (have_gfx) {
        // Algunos tests de M2 llaman a text_draw() (que llama a gfx_draw_sprite()), asi
        // que hace falta al menos un gfx_begin_frame() para que la cola de sprites de
        // g_arena_frame este inicializada.
        glyph_cache_init();
        gfx_begin_frame();
        g_test_font_latin = text_load_font("ttf/NotoSans.ttf", 32);
        g_test_font_cjk   = text_load_font("ttf/NotoSansJP.ttf", 32);
    }

    int result = context.run();

    if (context.shouldExit()) {
        return result;
    }

    audio_shutdown();
    if (have_gfx) {
        glyph_cache_shutdown();
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
