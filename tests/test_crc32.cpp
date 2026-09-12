#include <doctest/doctest.h>

#include "core/crc32.h"

TEST_CASE("crc32: vector de prueba estandar '123456789'") {
    const char* data = "123456789";
    CHECK(crc32(data, 9) == 0xCBF43926u);
}

TEST_CASE("crc32: cadena vacia da 0") {
    CHECK(crc32("", 0) == 0u);
}

TEST_CASE("crc32: datos distintos dan resultados distintos") {
    const char* a = "abc";
    const char* b = "abd";
    CHECK(crc32(a, 3) != crc32(b, 3));
}
