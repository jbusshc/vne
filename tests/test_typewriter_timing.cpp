#include <doctest/doctest.h>

#include "base/log.h"
#include "game/vn_mode.h"
#include "text/font.h"
#include "platform/input.h"
#include "test_fonts.h"
#include "text/layout.h"

// Temporizacion de {w=n} y {speed=n} sobre el efecto de maquina de escribir (M12).
// Hasta M11 estos dos marcadores se reconocian y se tiraban.
//
// Necesitan contexto grafico (text_layout rasteriza glifos): g_test_font_latin solo es
// valido si test_main.cpp pudo crear ventana y GPU, igual que el resto de tests de M2.

namespace {

// Un guion minimo de un solo Say cuyo texto vive en un string_pool de verdad, que es lo
// que VnMode espera (script_string indexa dentro de el).
struct SayScriptFixture {
    Cmd            cmds[2]{};
    CompiledScript script{};

    explicit SayScriptFixture(const char* pool_text, u32 pool_size) {
        cmds[0].kind             = CmdKind::Say;
        cmds[0].say.speaker_id  = 0xFFFFu;
        cmds[0].say.text_id     = 0;
        cmds[1].kind             = CmdKind::End;
        script.cmds             = cmds;
        script.cmd_count        = 2;
        script.string_pool      = pool_text;
        script.string_pool_size = pool_size;
    }
};

// Avanza VnMode a pasos fijos y devuelve cuanto tiempo simulado pasa desde que los glifos
// visibles dejan de crecer hasta que vuelven a crecer. Es exactamente la pausa observable
// por un jugador, medida como la medirian sus ojos, no leyendo el estado interno.
f32 measure_stall_seconds(VnMode* vn, GameState* state, f32 dt, u32 max_steps) {
    InputState input{};
    u32        last_visible   = static_cast<u32>(vn->visible_glyphs_f);
    f32        stall           = 0.0f;
    f32        longest_stall  = 0.0f;
    for (u32 i = 0; i < max_steps; ++i) {
        // Parar en cuanto el texto esta entero: a partir de ahi los glifos visibles se
        // congelan para siempre esperando input (ADR-0039), y eso contaria como un
        // "stall" infinito que no tiene nada que ver con {w=}.
        if (vn->visible_glyphs_f >= static_cast<f32>(vn->current_layout.count) &&
            vn->current_layout.count > 0) {
            break;
        }
        vn->update(input, dt);
        u32 visible = static_cast<u32>(vn->visible_glyphs_f);
        if (visible == last_visible) {
            stall += dt;
            if (stall > longest_stall) {
                longest_stall = stall;
            }
        } else {
            stall = 0.0f;
        }
        last_visible = visible;
        if (state->vm.pc != 0) {
            break;  // el Say ya se completo y se avanzo de comando
        }
    }
    return longest_stall;
}

}  // namespace

TEST_CASE("typewriter: {w=0.5} retrasa el texto 0.5 s (criterio de M12)") {
    if (!g_test_font_latin.valid()) {
        return;  // sin GPU en este entorno
    }
    // La pausa va en medio para poder distinguirla del arranque y del final.
    const char       pool[] = "abcdefghij{w=0.5}klmnopqrst";
    SayScriptFixture fixture(pool, sizeof(pool));

    GameState state{};
    Arena     arena = arena_create(1u << 20, "test_typewriter");
    VnMode    vn{};
    vn.state         = &state;
    vn.script        = fixture.script;
    vn.font          = g_test_font_latin;
    vn.layout_arena = &arena;

    constexpr f32 kDt = 1.0f / 240.0f;  // paso fino: el margen del criterio es +-50 ms
    f32 stall = measure_stall_seconds(&vn, &state, kDt, 2000);

    log_info("typewriter: pausa medida de {w=0.5} = %.3f s (criterio: 0.5 +- 0.05)",
             static_cast<double>(stall));
    CHECK(stall > 0.45f);
    CHECK(stall < 0.55f);

    arena_destroy(&arena);
}

TEST_CASE("typewriter: {speed=n} cambia el ritmo de revelado") {
    if (!g_test_font_latin.valid()) {
        return;
    }
    // Mismo texto, misma fuente, mismo dt: lo unico que cambia es el multiplicador. El
    // texto rapido debe tener mas glifos visibles tras el mismo tiempo simulado.
    const char slow_pool[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    const char fast_pool[] = "{speed=4}aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    auto visible_after = [](const char* pool, u32 pool_size, FontHandle font) -> u32 {
        SayScriptFixture fixture(pool, pool_size);
        GameState        state{};
        Arena            arena = arena_create(1u << 20, "test_speed");
        VnMode           vn{};
        vn.state         = &state;
        vn.script        = fixture.script;
        vn.font          = font;
        vn.layout_arena = &arena;

        InputState input{};
        for (u32 i = 0; i < 12; ++i) {  // 12 pasos de 1/240 s
            vn.update(input, 1.0f / 240.0f);
        }
        u32 visible = static_cast<u32>(vn.visible_glyphs_f);
        arena_destroy(&arena);
        return visible;
    };

    u32 slow = visible_after(slow_pool, sizeof(slow_pool), g_test_font_latin);
    u32 fast = visible_after(fast_pool, sizeof(fast_pool), g_test_font_latin);
    log_info("typewriter: glifos visibles tras 50 ms -> normal=%u con {speed=4}=%u", slow,
             fast);
    CHECK(fast > slow);
}

TEST_CASE("text_layout: {w=} y {speed=} generan eventos; sin ellos no hay ninguno") {
    if (!g_test_font_latin.valid()) {
        return;
    }
    Arena arena = arena_create(1u << 20, "test_events");

    TextLayout plain = text_layout(g_test_font_latin, "hola mundo", 1000.0f, &arena);
    CHECK(plain.event_count == 0);

    TextLayout timed =
        text_layout(g_test_font_latin, "hola{w=0.25}mundo{speed=2}final", 1000.0f, &arena);
    REQUIRE(timed.event_count == 2);
    // El primero es la pausa, anclada donde empieza "mundo" (tras los 4 glifos de "hola").
    CHECK(timed.events[0].glyph_index == 4);
    CHECK(timed.events[0].pause_seconds == doctest::Approx(0.25f));
    // El segundo es el cambio de velocidad, ya sin pausa.
    CHECK(timed.events[1].pause_seconds == doctest::Approx(0.0f));
    CHECK(timed.events[1].speed_multiplier == doctest::Approx(2.0f));

    // Un valor no numerico no se acepta en silencio: no genera evento.
    TextLayout bogus = text_layout(g_test_font_latin, "hola{w=rapido}mundo", 1000.0f, &arena);
    CHECK(bogus.event_count == 0);

    arena_destroy(&arena);
}

TEST_CASE("text_layout: {b} usa la fuente en negrita y engorda los glifos (M12)") {
    if (!g_test_font_latin.valid()) {
        return;
    }
    // La misma cara cargada aparte y marcada para engordar el contorno al rasterizar: no
    // hay ningun TTF en negrita entre los assets (ver text_load_font).
    FontHandle bold = text_load_font("ttf/NotoSans.ttf", 32, /*bold=*/true);
    REQUIRE(bold.valid());

    Arena arena = arena_create(1u << 20, "test_bold");

    // Mismo texto, misma fuente base: lo unico que cambia es que uno va entre {b}.
    TextLayout plain = text_layout(g_test_font_latin, "HOLA", 1000.0f, &arena, 0xFFFFFFFFu, bold);
    TextLayout heavy = text_layout(g_test_font_latin, "{b}HOLA{/b}", 1000.0f, &arena,
                                    0xFFFFFFFFu, bold);
    REQUIRE(plain.count > 0);
    REQUIRE(heavy.count == plain.count);

    // Los glifos engordados ocupan mas ancho de tinta que los normales. Se compara el
    // total para no depender de un glifo concreto de la fuente.
    f32 plain_ink = 0.0f, heavy_ink = 0.0f;
    for (u32 i = 0; i < plain.count; ++i) {
        plain_ink += plain.quads[i].w;
        heavy_ink += heavy.quads[i].w;
    }
    log_info("bold: ancho de tinta normal=%.1f negrita=%.1f", static_cast<double>(plain_ink),
             static_cast<double>(heavy_ink));
    CHECK(heavy_ink > plain_ink);

    // Sin fuente en negrita, {b} no falla: se dibuja como texto normal (SPEC.md #4).
    TextLayout fallback = text_layout(g_test_font_latin, "{b}HOLA{/b}", 1000.0f, &arena);
    REQUIRE(fallback.count == plain.count);
    f32 fallback_ink = 0.0f;
    for (u32 i = 0; i < fallback.count; ++i) {
        fallback_ink += fallback.quads[i].w;
    }
    CHECK(fallback_ink == doctest::Approx(plain_ink));

    arena_destroy(&arena);
}

TEST_CASE("modo auto: la espera es proporcional a la longitud de la linea (M12)") {
    // Hasta M11 era fija (1.2 s) para cualquier linea. Funcion pura: se verifica
    // directamente, sin conducir un VnMode con reloj.
    f32 corta  = vn_auto_hold_seconds(5);
    f32 media  = vn_auto_hold_seconds(40);
    f32 larga  = vn_auto_hold_seconds(120);

    CHECK(corta < media);
    CHECK(media < larga);

    // Una linea corta no se queda una eternidad, y una larga da tiempo a leerla.
    CHECK(corta < 1.0f);
    CHECK(larga > 3.0f);

    // Tope duro: ni una linea absurda cuelga el juego.
    CHECK(vn_auto_hold_seconds(100000) == doctest::Approx(k_auto_hold_max_seconds));

    log_info("auto: espera 5 glifos=%.2fs 40=%.2fs 120=%.2fs", static_cast<double>(corta),
             static_cast<double>(media), static_cast<double>(larga));
}
