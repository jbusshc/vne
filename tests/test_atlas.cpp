#include <doctest/doctest.h>

#include "core/arena.h"
#include "render/atlas.h"

// Registro de sprites del atlas (M13). assets_baked/atlas_00.bin ya lo hornea el propio
// build (CMakeLists.txt, regla sz_bake), igual que demo.vnc para los tests de VM.
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

TEST_CASE("atlas: los nombres se pueden enumerar por indice, no solo buscar (M15)") {
    // atlas_name_at es el sentido que le faltaba a la tabla de nombres: M13 solo permitia
    // nombre -> rectangulo, y el visor de atlas del editor necesita el contrario para poder
    // listar lo que hay dentro. Sin esto el visor tendria que adivinar los nombres.
    REQUIRE(atlas_load(&g_arena_perm));
    u32 count = atlas_sprite_count();
    REQUIRE(count > 0);

    // Todos los indices dan un nombre no vacio, y ese nombre vuelve a encontrar EL MISMO
    // rectangulo: si el offset en el pool de nombres estuviera mal, esto daria el sprite
    // equivocado o una cadena basura.
    u32 checked = 0;
    for (u32 i = 0; i < count; ++i) {
        const char* name = atlas_name_at(i);
        REQUIRE(name != nullptr);
        CHECK(name[0] != '\0');

        AtlasSprite by_index{};
        AtlasSprite by_name{};
        REQUIRE(atlas_sprite_at(i, &by_index));
        REQUIRE(atlas_find(name, &by_name));
        CHECK(by_name.x == by_index.x);
        CHECK(by_name.y == by_index.y);
        CHECK(by_name.w == by_index.w);
        CHECK(by_name.h == by_index.h);
        checked += 1;
    }
    MESSAGE("sprites con nombre comprobados: " << checked);
    CHECK(checked == count);

    // Fuera de rango devuelve cadena vacia, no un puntero invalido.
    CHECK(atlas_name_at(count)[0] == '\0');
    CHECK(atlas_name_at(0xFFFFFFFFu)[0] == '\0');
}
