#include <doctest/doctest.h>

#include <cstdio>
#include <ostream>
#include <string>

#include <SDL3/SDL.h>

#include "base/arena.h"
#include "game/config.h"
#include "game/locales.h"
#include "game/menu_mode.h"
#include "text/catalog.h"
#include "vm/state.h"

// Preferencias del jugador (M14). El criterio de SPEC.md #12 es "cambiar el idioma, cerrar el
// proceso y volver a abrirlo mantiene el idioma elegido". No se puede cerrar y reabrir el
// proceso dentro de un test, pero si se puede hacer lo equivalente y mas estricto: simular la
// pulsacion de tecla que cambia el idioma, comprobar que el archivo queda escrito, borrar el
// estado en memoria y volver a cargarlo como haria un arranque nuevo.

namespace {

// Deja el directorio como estaba: config.ini es del directorio de trabajo y otros tests (o
// el propio juego) podrian encontrarselo.
struct ConfigFileGuard {
    ~ConfigFileGuard() { std::remove("config.ini"); }
};

std::string read_config_file() {
    std::FILE* f = std::fopen("config.ini", "rb");
    if (f == nullptr) {
        return {};
    }
    std::string out;
    char        buf[512];
    usize       n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        out.append(buf, n);
    }
    std::fclose(f);
    return out;
}

}  // namespace

TEST_CASE("config: el idioma sobrevive a cerrar y reabrir (criterio M14)") {
    ConfigFileGuard guard;
    std::remove("config.ini");

    // El catalogo se carga en g_arena_perm, NO en una arena local: catalog_load guarda
    // punteros dentro de ella y el catalogo es global, asi que destruir una arena local
    // dejaria el catalogo apuntando a memoria liberada y reventaria cualquier test
    // posterior que resuelva un texto. (Paso: SIGSEGV en test_typewriter_timing.)
    Arena     arena = arena_create(256 * 1024, "test_config_layouts");
    GameState state{};

    MenuMode menu{};
    menu.state          = &state;
    menu.scratch_arena  = &arena;
    menu.catalog_arena  = &g_arena_perm;
    menu.selected      = 4;  // la fila de idioma
    REQUIRE(menu.locale_index == 0);

    // La misma pulsacion que haria el jugador.
    InputState input{};
    input.key_pressed[SDL_SCANCODE_RIGHT] = true;
    menu.update(input, 1.0f / 60.0f);
    REQUIRE(menu.locale_index == 1);

    // Se escribe AL CAMBIAR, no al salir: si el proceso muere de forma anormal, la
    // preferencia ya esta a salvo.
    std::string contents = read_config_file();
    INFO("config.ini: " << contents);
    CHECK(contents.find("locale=ja") != std::string::npos);

    // Y ahora el "volver a abrirlo": se borra lo que hay en memoria y se relee del archivo,
    // que es exactamente lo que hace main.cpp al arrancar.
    g_config = Config{};
    REQUIRE(g_config.locale_index == 0);
    config_load();
    CHECK(g_config.locale_index == 1);

    // El catalogo es estado GLOBAL: se deja como estaba para no cambiarle el idioma a los
    // tests que vengan detras (mismo cuidado que test_pak.cpp con el backend montado).
    catalog_clear();
    arena_destroy(&arena);
}

TEST_CASE("config: se guarda el ID del idioma, no su indice") {
    // Un indice deja de significar lo mismo en cuanto se reordena o se añade un idioma, y
    // entonces una preferencia guardada pasaria a apuntar a otro. Mismo razonamiento que
    // ADR-0034 con las pistas y ADR-0067 con los simbolos.
    ConfigFileGuard guard;
    g_config               = Config{};
    g_config.locale_index = 1;
    config_save();

    std::string contents = read_config_file();
    CHECK(contents.find("locale=ja") != std::string::npos);
    CHECK(contents.find("locale=1") == std::string::npos);
}

TEST_CASE("config: los volumenes de bus dan la vuelta completa") {
    ConfigFileGuard guard;
    g_config                = Config{};
    g_config.bus_volume[0] = 0.25f;
    g_config.bus_volume[2] = 0.75f;
    config_save();

    g_config = Config{};
    config_load();
    CHECK(g_config.bus_volume[0] == doctest::Approx(0.25f).epsilon(0.01));
    CHECK(g_config.bus_volume[2] == doctest::Approx(0.75f).epsilon(0.01));
}

TEST_CASE("config: un archivo con basura no impide jugar, cae a los valores por defecto") {
    // Es un archivo que el jugador edita a mano: una coma de mas no puede dejarlo sin poder
    // jugar. El parser es TOTAL — lo que no entiende lo ignora.
    ConfigFileGuard guard;
    std::FILE*      f = std::fopen("config.ini", "wb");
    REQUIRE(f != nullptr);
    std::fputs("esto no es un ini\n"
               "[seccion sin cerrar\n"
               "= sin clave\n"
               "sin_igual\n"
               "locale=idioma_que_no_existe\n"
               "master=no_es_un_numero\n"
               "music=99999\n",
               f);
    std::fclose(f);

    g_config = Config{};
    config_load();
    CHECK(g_config.locale_index == 0);                     // idioma desconocido -> el base
    CHECK(g_config.bus_volume[0] == doctest::Approx(0.0f));  // "no_es_un_numero" -> 0
    CHECK(g_config.bus_volume[1] == doctest::Approx(1.0f));  // 99999 recortado a 1.0
}

TEST_CASE("config: un archivo que no existe deja los valores por defecto") {
    ConfigFileGuard guard;
    std::remove("config.ini");
    g_config = Config{};
    config_load();
    CHECK(g_config.locale_index == 0);
    CHECK(g_config.bus_volume[0] == doctest::Approx(1.0f));
    CHECK_FALSE(g_config.fullscreen);
}

TEST_CASE("locales: el id se resuelve a indice y lo desconocido cae al base") {
    CHECK(locale_index_from_id("es") == 0);
    CHECK(locale_index_from_id("ja") == 1);
    CHECK(locale_index_from_id("klingon") == 0);
    CHECK(locale_index_from_id(nullptr) == 0);
}
