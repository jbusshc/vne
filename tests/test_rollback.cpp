#include <doctest/doctest.h>

#include "vm/rollback.h"

// Instancias locales, no el g_rollback global: cada test es independiente (rollback_init
// solo pide una Arena, no tiene por que ser la global de perm, pero reutilizarla es
// inofensivo porque estos tests no le importan a nadie mas).
TEST_CASE("rollback: retroceder devuelve exactamente el estado anterior") {
    RollbackBuffer rb;
    rollback_init(&rb);

    GameState s0{};
    s0.bg_id = 10;
    GameState s1{};
    s1.bg_id = 20;
    rollback_capture(&rb, s0);
    rollback_capture(&rb, s1);

    GameState out{};
    REQUIRE(rollback_back(&rb, &out));
    CHECK(out.bg_id == 10);
    CHECK_FALSE(rollback_back(&rb, &out));  // no hay mas historia hacia atras
}

TEST_CASE("rollback: retroceder y luego avanzar vuelve al mismo estado, sin divergencias") {
    RollbackBuffer rb;
    rollback_init(&rb);

    GameState s0{};
    s0.bg_id = 1;
    GameState s1{};
    s1.bg_id = 2;
    GameState s2{};
    s2.bg_id = 3;
    rollback_capture(&rb, s0);
    rollback_capture(&rb, s1);
    rollback_capture(&rb, s2);

    GameState out{};
    REQUIRE(rollback_back(&rb, &out));
    CHECK(out.bg_id == 2);
    REQUIRE(rollback_back(&rb, &out));
    CHECK(out.bg_id == 1);
    REQUIRE(rollback_forward(&rb, &out));
    CHECK(out.bg_id == 2);
    REQUIRE(rollback_forward(&rb, &out));
    CHECK(out.bg_id == 3);
    CHECK_FALSE(rollback_forward(&rb, &out));
}

TEST_CASE("rollback: capturar despues de retroceder descarta el futuro pendiente") {
    RollbackBuffer rb;
    rollback_init(&rb);

    GameState s0{};
    s0.bg_id = 1;
    GameState s1{};
    s1.bg_id = 2;
    rollback_capture(&rb, s0);
    rollback_capture(&rb, s1);

    GameState out{};
    REQUIRE(rollback_back(&rb, &out));  // ahora en s0

    GameState s_new{};
    s_new.bg_id = 99;
    rollback_capture(&rb, s_new);  // descarta s1, que era el "futuro" pendiente

    CHECK_FALSE(rollback_forward(&rb, &out));
    REQUIRE(rollback_back(&rb, &out));
    CHECK(out.bg_id == 1);
}

TEST_CASE("rollback: 64 pasos de capacidad, la mas vieja se descarta al llenarse") {
    RollbackBuffer rb;
    rollback_init(&rb);

    for (u32 i = 0; i < k_rollback_capacity + 10; ++i) {
        GameState s{};
        s.bg_id = static_cast<u16>(i);
        rollback_capture(&rb, s);
    }

    GameState out{};
    u32       steps_back = 0;
    while (rollback_back(&rb, &out)) {
        steps_back += 1;
    }
    // El presente cuenta como una de las k_rollback_capacity instantaneas: se puede
    // retroceder k_rollback_capacity - 1 veces desde ahi.
    CHECK(steps_back == k_rollback_capacity - 1);
    // Se capturaron 74 estados (0..73); solo sobreviven los ultimos 64 (10..73).
    CHECK(out.bg_id == 10);
}
