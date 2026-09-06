#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>

#include "base/arena.h"

TEST_CASE("arena_alloc respeta la alineacion pedida") {
    Arena a = arena_create(1024, "test");

    void* p1 = arena_alloc(&a, 3, 16);
    void* p2 = arena_alloc(&a, 8, 16);

    CHECK(reinterpret_cast<uintptr_t>(p1) % 16 == 0);
    CHECK(reinterpret_cast<uintptr_t>(p2) % 16 == 0);

    arena_destroy(&a);
}

TEST_CASE("arena_alloc devuelve nullptr al agotar el espacio") {
    Arena a = arena_create(16, "test");

    void* p1 = arena_alloc(&a, 16, 1);
    CHECK(p1 != nullptr);

    void* p2 = arena_alloc(&a, 1, 1);
    CHECK(p2 == nullptr);

    arena_destroy(&a);
}

TEST_CASE("arena_reset libera todo el espacio usado") {
    Arena a = arena_create(64, "test");

    arena_alloc(&a, 32, 1);
    CHECK(a.used == 32);

    arena_reset(&a);
    CHECK(a.used == 0);

    void* p = arena_alloc(&a, 64, 1);
    CHECK(p != nullptr);

    arena_destroy(&a);
}

#if defined(VN_DEBUG)
TEST_CASE("arena_reset rellena la memoria con 0xCD en VN_DEBUG") {
    Arena a = arena_create(16, "test");

    u8* p = static_cast<u8*>(arena_alloc(&a, 16, 1));
    std::memset(p, 0xAB, 16);

    arena_reset(&a);

    for (usize i = 0; i < 16; ++i) {
        CHECK(a.base[i] == 0xCD);
    }

    arena_destroy(&a);
}
#endif
