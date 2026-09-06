#include <doctest/doctest.h>

#include "gfx/gfx.h"

TEST_CASE("letterbox: 16:9 llena toda la ventana sin barras") {
    Recti r = gfx_letterbox_rect(1920, 1080, k_virtual_width, k_virtual_height);
    CHECK(r.x == 0);
    CHECK(r.y == 0);
    CHECK(r.w == 1920);
    CHECK(r.h == 1080);
}

TEST_CASE("letterbox: 4:3 mete barras arriba y abajo") {
    Recti r = gfx_letterbox_rect(1024, 768, k_virtual_width, k_virtual_height);
    CHECK(r.x == 0);
    CHECK(r.w == 1024);
    CHECK(r.h == 576);
    CHECK(r.y == 96);
}

TEST_CASE("letterbox: 21:9 mete barras a los lados") {
    Recti r = gfx_letterbox_rect(2560, 1080, k_virtual_width, k_virtual_height);
    CHECK(r.y == 0);
    CHECK(r.h == 1080);
    CHECK(r.w == 1920);
    CHECK(r.x == 320);
}

TEST_CASE("letterbox: 16:10 mete barras pequenas arriba y abajo") {
    Recti r = gfx_letterbox_rect(1920, 1200, k_virtual_width, k_virtual_height);
    CHECK(r.x == 0);
    CHECK(r.w == 1920);
    CHECK(r.h == 1080);
    CHECK(r.y == 60);
}
