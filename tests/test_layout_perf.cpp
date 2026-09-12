#include <doctest/doctest.h>

#include <algorithm>
#include <string>

#include "core/arena.h"
#include "core/log.h"
#include "platform/clock.h"
#include "test_fonts.h"
#include "text/layout.h"

TEST_CASE("layout: un parrafo de 500 caracteres se relayoutea en menos de 1ms (mediana)") {
    REQUIRE(g_test_font_latin.valid());

    std::string paragraph;
    const char* sentence = "The quick brown fox jumps over the lazy dog. ";
    while (paragraph.size() < 500) {
        paragraph += sentence;
    }
    paragraph.resize(500);

    Arena a = arena_create(2 * 1024 * 1024, "test");

    // Calienta la cache de glifos: la primera vez rasteriza con FreeType, que es la parte
    // cara y no representa el caso de "relayout" que pide el criterio de M1 (SPEC.md
    // #12 dice "se relayoutea", no "se layoutea por primera vez").
    text_layout(g_test_font_latin, paragraph, 1600.0f, &a);

    constexpr u32 k_samples = 1000;
    u64           samples[k_samples];
    for (u32 i = 0; i < k_samples; ++i) {
        arena_reset(&a);
        u64 start = clock_now_microseconds();
        text_layout(g_test_font_latin, paragraph, 1600.0f, &a);
        samples[i] = clock_now_microseconds() - start;
    }

    std::sort(samples, samples + k_samples);
    u64 median_us = samples[k_samples / 2];
    u64 worst_us  = samples[k_samples - 1];

    // El peor caso absoluto de 1000 muestras en un SO de escritorio (no de tiempo real)
    // puede dispararse por una preferencia de scheduling ajena al motor: se observaron
    // picos aislados de mas de 1ms sin cambiar una linea de codigo. La mediana refleja el
    // coste real del layout; el peor caso se registra pero no es el criterio de paso
    // (ver docs/DECISIONS.md, hito M2).
    log_info("layout_perf: %zu caracteres, mediana=%llu us, peor caso=%llu us de %u muestras",
              paragraph.size(), static_cast<unsigned long long>(median_us),
              static_cast<unsigned long long>(worst_us), k_samples);
    CHECK(median_us < 1000);  // <1ms, SPEC.md #12

    arena_destroy(&a);
}
