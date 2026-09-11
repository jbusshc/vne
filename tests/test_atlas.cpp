#include <doctest/doctest.h>

#include "base/arena.h"
#include "gfx/atlas.h"

// Registro de sprites del atlas (M13). assets_baked/atlas_00.bin ya lo hornea el propio
// build (CMakeLists.txt, regla vne_bake), igual que demo.vnc para los tests de VM.
//
// atlas_load deja la tabla residente en la arena que se le pase y guarda punteros a ella
// en estado global del modulo, asi que la arena NO se puede destruir mientras se use el
// registro: estos tests usan g_arena_perm, que vive lo que vive el proceso, en vez de una
// arena local como hacen los de MapMode.

TEST_CASE("atlas: carga el registro horneado y encuentra un sprite por su nombre") {
    REQUIRE(atlas_load(&g_arena_perm));
    REQUIRE(atlas_sprite_count() > 0);

    // Nombre real del Kenney UI Pack que vive en assets_src/png/ (ADR-0025). El nombre
    // logico es el del archivo sin directorio ni extension.
    AtlasSprite s{};
    REQUIRE(atlas_find("arrow_basic_e", &s));
    CHECK(s.w > 0);
    CHECK(s.h > 0);
}

TEST_CASE("atlas: un nombre que no existe devuelve false, no un sprite equivocado") {
    REQUIRE(atlas_load(&g_arena_perm));

    AtlasSprite s{};
    CHECK_FALSE(atlas_find("no_existe_este_sprite", &s));
    CHECK_FALSE(atlas_find("", &s));
    CHECK_FALSE(atlas_find(nullptr, &s));
}

TEST_CASE("atlas: el acceso por indice cubre todo el registro y respeta el limite") {
    REQUIRE(atlas_load(&g_arena_perm));
    u32 count = atlas_sprite_count();
    REQUIRE(count > 0);

    // Todos los indices validos devuelven un rectangulo con area: si alguno saliera a 0
    // seria una entrada corrupta o mal alineada (el layout de AtlasEntry lleva relleno
    // explicito justo para que eso no pase, ADR-0028).
    u32 with_area = 0;
    for (u32 i = 0; i < count; ++i) {
        AtlasSprite s{};
        if (atlas_sprite_at(i, &s) && s.w > 0 && s.h > 0) {
            with_area += 1;
        }
    }
    CHECK(with_area == count);

    AtlasSprite out_of_range{};
    CHECK_FALSE(atlas_sprite_at(count, &out_of_range));
    CHECK_FALSE(atlas_sprite_at(0xFFFFFFFFu, &out_of_range));
}
