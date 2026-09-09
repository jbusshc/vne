#include <doctest/doctest.h>

// Ver test_lexer.cpp: doctest necesita <ostream> completo para imprimir cadenas en un
// mensaje de fallo de CHECK.
#include <ostream>
#include <string_view>

#include "base/arena.h"
#include "base/hash.h"
#include "text/catalog.h"

// assets_baked/es.vnl y ja.vnl ya estan horneados por el propio build (CMakeLists.txt:
// regla vne_bake_catalog_<idioma>), igual que assets_baked/demo.vnc para los tests de VM.

TEST_CASE("catalog_load: carga el .vnl horneado y resuelve una clave conocida") {
    Arena a = arena_create(256 * 1024, "test_catalog");
    REQUIRE(catalog_load("ja.vnl", &a) == CatalogLoadResult::Ok);

    u32 key = fnv1a_u32("Linea de dialogo de prueba numero 1 del capitulo uno.");
    const char* translated = catalog_find(key);
    REQUIRE(translated != nullptr);
    CHECK(std::string_view(translated).find("[JA-placeholder]") == 0);

    catalog_clear();
    arena_destroy(&a);
}

TEST_CASE("catalog_find: una clave que no esta en el catalogo activo devuelve nullptr") {
    Arena a = arena_create(256 * 1024, "test_catalog");
    REQUIRE(catalog_load("ja.vnl", &a) == CatalogLoadResult::Ok);

    CHECK(catalog_find(0xDEADBEEFu) == nullptr);

    catalog_clear();
    arena_destroy(&a);
}

TEST_CASE("catalog_find: sin catalogo activo (idioma base) siempre devuelve nullptr") {
    catalog_clear();
    u32 key = fnv1a_u32("Linea de dialogo de prueba numero 1 del capitulo uno.");
    CHECK(catalog_find(key) == nullptr);
}

TEST_CASE("catalog_resolve: cae al texto base cuando no hay traduccion (M10, nunca vacio)") {
    catalog_clear();
    CHECK(std::string_view(catalog_resolve(0x12345678u, "texto base")) == "texto base");
}

TEST_CASE("catalog_load: un archivo inexistente falla sin crashear y limpia el catalogo") {
    Arena a = arena_create(256 * 1024, "test_catalog");
    CHECK(catalog_load("no_existe.vnl", &a) == CatalogLoadResult::NotFound);
    CHECK(catalog_find(1) == nullptr);
    arena_destroy(&a);
}

TEST_CASE("catalog_generation: cambia con cada catalog_load/catalog_clear (M10, "
          "idioma en caliente)") {
    catalog_clear();
    u32 gen0 = catalog_generation();

    Arena a = arena_create(256 * 1024, "test_catalog");
    REQUIRE(catalog_load("es.vnl", &a) == CatalogLoadResult::Ok);
    u32 gen1 = catalog_generation();
    CHECK(gen1 != gen0);

    catalog_clear();
    u32 gen2 = catalog_generation();
    CHECK(gen2 != gen1);

    arena_destroy(&a);
}
