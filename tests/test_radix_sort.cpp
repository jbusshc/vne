#include <doctest/doctest.h>

#include "base/radix_sort.h"

TEST_CASE("radix_sort_u64: ordena claves ascendentes") {
    Arena a = arena_create(4096, "test");

    RadixSortEntry entries[6] = {
        {500, 0}, {10, 1}, {999999, 2}, {0, 3}, {42, 4}, {10, 5},
    };
    radix_sort_u64(entries, 6, &a);

    for (u32 i = 1; i < 6; ++i) {
        CHECK(entries[i - 1].key <= entries[i].key);
    }

    arena_destroy(&a);
}

TEST_CASE("radix_sort_u64: es estable entre claves iguales") {
    Arena a = arena_create(4096, "test");

    // Dos entradas con la misma clave: el orden relativo original (index 1 antes que
    // index 5) debe conservarse.
    RadixSortEntry entries[3] = {{10, 1}, {5, 9}, {10, 5}};
    radix_sort_u64(entries, 3, &a);

    REQUIRE(entries[1].key == 10);
    REQUIRE(entries[2].key == 10);
    CHECK(entries[1].index == 1);
    CHECK(entries[2].index == 5);

    arena_destroy(&a);
}

TEST_CASE("radix_sort_u64: preserva todos los indices como una permutacion") {
    Arena a = arena_create(4096, "test");

    RadixSortEntry entries[5] = {{7, 0}, {3, 1}, {3, 2}, {1, 3}, {9, 4}};
    radix_sort_u64(entries, 5, &a);

    bool seen[5] = {false, false, false, false, false};
    for (const auto& e : entries) {
        REQUIRE(e.index < 5);
        CHECK_FALSE(seen[e.index]);
        seen[e.index] = true;
    }

    arena_destroy(&a);
}

TEST_CASE("radix_sort_u64: no falla con count 0") {
    Arena a = arena_create(4096, "test");
    radix_sort_u64(nullptr, 0, &a);
    arena_destroy(&a);
}
