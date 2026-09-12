#include <doctest/doctest.h>

// Ver test_lexer.cpp: doctest necesita <ostream> completo para imprimir cadenas en un
// mensaje de fallo de CHECK.
#include <ostream>
#include <string>
#include <string_view>

#include "script/map_bake.h"

// Tests del escaner de TMX (ADR-0044). Existen porque los tres bugs que cubren se
// colaron cuando este codigo vivia dentro del main.cpp de vne_bake, donde ningun test
// podia alcanzarlo.

namespace {

// TMX minimo del subconjunto soportado: 2x2 tiles, capas "tiles" y "collision" en CSV.
// `objects` se inyecta dentro del <objectgroup>.
std::string make_tmx(const std::string& objects) {
    return
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<map version=\"1.10\" width=\"2\" height=\"2\" tilewidth=\"64\" tileheight=\"64\">\n"
        " <layer id=\"1\" name=\"tiles\" width=\"2\" height=\"2\">\n"
        "  <data encoding=\"csv\">\n1,2,\n3,4\n</data>\n"
        " </layer>\n"
        " <layer id=\"2\" name=\"collision\" width=\"2\" height=\"2\">\n"
        "  <data encoding=\"csv\">\n1,0,\n0,1\n</data>\n"
        " </layer>\n"
        " <objectgroup id=\"3\" name=\"triggers\">\n" +
        objects +
        " </objectgroup>\n"
        "</map>\n";
}

const char* trigger_script(const ParsedMap& map, usize index) {
    return map.string_pool.c_str() + map.triggers[index].script_path_offset;
}

}  // namespace

TEST_CASE("tmx_parse: lee rejilla, tiles y colision del subconjunto soportado") {
    ParsedMap   map;
    std::string error;
    REQUIRE(tmx_parse(make_tmx(""), &map, &error));

    CHECK(map.grid_w == 2);
    CHECK(map.grid_h == 2);
    CHECK(map.tile_size == 64);
    REQUIRE(map.tiles.size() == 4);
    CHECK(map.tiles[0] == 1);
    CHECK(map.tiles[3] == 4);
    REQUIRE(map.collision.size() == 4);
    CHECK(map.collision[0] == 1);
    CHECK(map.collision[1] == 0);
    CHECK(map.collision[3] == 1);
    CHECK(map.triggers.empty());
}

TEST_CASE("tmx_parse: no confunde <objectgroup> con <object> (son prefijo uno del otro)") {
    // El bug original: buscar "<object" encontraba primero "<objectgroup", y el trigger
    // heredaba los atributos de su propio grupo contenedor (quedaba en el tile 0,0).
    ParsedMap   map;
    std::string error;
    REQUIRE(tmx_parse(
        make_tmx("  <object id=\"1\" x=\"128\" y=\"64\" width=\"64\" height=\"64\"/>\n"), &map,
        &error));

    REQUIRE(map.triggers.size() == 1);
    CHECK(map.triggers[0].tile_x == 2);  // 128/64
    CHECK(map.triggers[0].tile_y == 1);  // 64/64
}

TEST_CASE("tmx_parse: un <object/> autocerrado no se come el objeto siguiente") {
    // El bug original: al avanzar hasta el siguiente </object> (que pertenecia al objeto
    // de despues, porque el autocerrado no tiene el suyo), el segundo objeto se saltaba
    // entero y solo se parseaba un trigger.
    ParsedMap   map;
    std::string error;
    REQUIRE(tmx_parse(make_tmx(
                          "  <object id=\"1\" x=\"0\" y=\"0\" width=\"64\" height=\"64\"/>\n"
                          "  <object id=\"2\" x=\"64\" y=\"64\" width=\"64\" height=\"64\">\n"
                          "   <properties>\n"
                          "    <property name=\"script\" value=\"assets_baked/b.vnc\"/>\n"
                          "   </properties>\n"
                          "  </object>\n"),
                      &map, &error));

    REQUIRE(map.triggers.size() == 2);
    CHECK(map.triggers[1].tile_x == 1);
    CHECK(map.triggers[1].tile_y == 1);
}

TEST_CASE("tmx_parse: la <property> de un objeto no se filtra al autocerrado anterior") {
    // Mismo bug que el anterior visto por el otro lado: el objeto autocerrado no tiene
    // </object> que corte la busqueda, asi que se quedaba con la property del siguiente.
    ParsedMap   map;
    std::string error;
    REQUIRE(tmx_parse(make_tmx(
                          "  <object id=\"1\" x=\"0\" y=\"0\" width=\"64\" height=\"64\"/>\n"
                          "  <object id=\"2\" x=\"64\" y=\"0\" width=\"64\" height=\"64\">\n"
                          "   <properties>\n"
                          "    <property name=\"script\" value=\"assets_baked/segundo.vnc\"/>\n"
                          "   </properties>\n"
                          "  </object>\n"),
                      &map, &error));

    REQUIRE(map.triggers.size() == 2);
    CHECK(std::string_view(trigger_script(map, 0)).empty());
    CHECK(std::string_view(trigger_script(map, 1)) == "assets_baked/segundo.vnc");
}

TEST_CASE("tmx_parse: un objectgroup truncado falla con mensaje, no escanea desde el inicio") {
    // El bug original: xml.find('>', ...) devolvia npos y npos+1 se desbordaba a 0, asi
    // que el escaneo reempezaba desde el principio del archivo en vez de fallar.
    std::string truncated =
        "<map width=\"2\" height=\"2\" tilewidth=\"64\" tileheight=\"64\">\n"
        " <layer name=\"tiles\"><data encoding=\"csv\">1,2,3,4</data></layer>\n"
        " <layer name=\"collision\"><data encoding=\"csv\">0,0,0,0</data></layer>\n"
        " <objectgroup id=\"3\" name=\"triggers\"\n";  // sin '>' ni </objectgroup>

    ParsedMap   map;
    std::string error;
    CHECK_FALSE(tmx_parse(truncated, &map, &error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("tmx_parse: un TMX sin las capas que exige el subconjunto falla con mensaje") {
    ParsedMap   map;
    std::string error;

    CHECK_FALSE(tmx_parse("<map width=\"2\" height=\"2\" tilewidth=\"64\"></map>", &map, &error));
    CHECK(error.find("tiles") != std::string::npos);

    CHECK_FALSE(tmx_parse("no soy xml", &map, &error));
    CHECK_FALSE(error.empty());
}

// --- Robustez del escaner (M13) --------------------------------------------------------
//
// Hasta M13 estos cuatro casos producian un .vnm silenciosamente incorrecto o culpaban a la
// causa equivocada: encoding y compression se ignoraban por completo, asi que un CSV
// comprimido pasaba por parse_csv_u16, sacaba numeros del base64 y acababa quejandose del
// TAMANO de la capa. El criterio de SPEC.md #12 para M13 es justamente que se rechace
// nombrando la compresion.

TEST_CASE("tmx_parse: una capa comprimida se rechaza NOMBRANDO la compresion (criterio M13)") {
    std::string tmx =
        "<map width=\"2\" height=\"2\" tilewidth=\"64\">\n"
        " <layer name=\"tiles\">\n"
        "  <data encoding=\"base64\" compression=\"zlib\">eJxjYGBgYGAAAAAFAAE=</data>\n"
        " </layer>\n"
        " <layer name=\"collision\">\n"
        "  <data encoding=\"csv\">1,0,0,1</data>\n"
        " </layer>\n"
        "</map>\n";

    ParsedMap   map;
    std::string error;
    REQUIRE_FALSE(tmx_parse(tmx, &map, &error));
    MESSAGE("mensaje: " << error);
    CHECK(error.find("zlib") != std::string::npos);
    CHECK(error.find("comprimida") != std::string::npos);
    // Y NO culpa al tamano, que es lo que hacia antes.
    CHECK(error.find("tamano") == std::string::npos);
}

TEST_CASE("tmx_parse: gzip y zstd tambien se nombran, no solo zlib") {
    for (const char* algo : {"gzip", "zstd"}) {
        std::string tmx = std::string(
            "<map width=\"2\" height=\"2\" tilewidth=\"64\">\n"
            " <layer name=\"tiles\">\n"
            "  <data encoding=\"base64\" compression=\"") + algo + "\">xxxx</data>\n"
            " </layer>\n"
            "</map>\n";
        ParsedMap   map;
        std::string error;
        REQUIRE_FALSE(tmx_parse(tmx, &map, &error));
        CHECK(error.find(algo) != std::string::npos);
    }
}

TEST_CASE("tmx_parse: un encoding que no es csv se rechaza nombrandolo") {
    std::string tmx =
        "<map width=\"2\" height=\"2\" tilewidth=\"64\">\n"
        " <layer name=\"tiles\">\n"
        "  <data encoding=\"base64\">eJxjYGBgYGAAAAAFAAE=</data>\n"
        " </layer>\n"
        "</map>\n";

    ParsedMap   map;
    std::string error;
    REQUIRE_FALSE(tmx_parse(tmx, &map, &error));
    MESSAGE("mensaje: " << error);
    CHECK(error.find("base64") != std::string::npos);
}

TEST_CASE("tmx_parse: width/height que no cuadran con las celdas del CSV dan los numeros") {
    // El <map> declara 3x3 = 9 celdas pero el CSV solo trae 4. Antes esto daba "capa
    // ausente o de tamano incorrecto", sin decir cuantas esperaba ni cuantas encontro.
    std::string tmx =
        "<map width=\"3\" height=\"3\" tilewidth=\"64\">\n"
        " <layer name=\"tiles\">\n"
        "  <data encoding=\"csv\">1,2,3,4</data>\n"
        " </layer>\n"
        " <layer name=\"collision\">\n"
        "  <data encoding=\"csv\">0,0,0,0,0,0,0,0,0</data>\n"
        " </layer>\n"
        "</map>\n";

    ParsedMap   map;
    std::string error;
    REQUIRE_FALSE(tmx_parse(tmx, &map, &error));
    MESSAGE("mensaje: " << error);
    CHECK(error.find("9") != std::string::npos);  // las que declara
    CHECK(error.find("4") != std::string::npos);  // las que hay
}

TEST_CASE("tmx_parse: dos tilesets se rechazan explicando por que (gid crudo)") {
    std::string tmx =
        "<map width=\"2\" height=\"2\" tilewidth=\"64\">\n"
        " <tileset firstgid=\"1\" source=\"a.tsx\"/>\n"
        " <tileset firstgid=\"50\" source=\"b.tsx\"/>\n"
        " <layer name=\"tiles\">\n"
        "  <data encoding=\"csv\">1,2,3,4</data>\n"
        " </layer>\n"
        " <layer name=\"collision\">\n"
        "  <data encoding=\"csv\">0,0,0,0</data>\n"
        " </layer>\n"
        "</map>\n";

    ParsedMap   map;
    std::string error;
    REQUIRE_FALSE(tmx_parse(tmx, &map, &error));
    MESSAGE("mensaje: " << error);
    CHECK(error.find("tileset") != std::string::npos);
    CHECK(error.find("firstgid") != std::string::npos);
}

TEST_CASE("tmx_parse: un solo tileset sigue siendo valido") {
    // Guarda contra pasarse de estricto: el caso normal tiene exactamente un tileset.
    std::string tmx =
        "<map width=\"2\" height=\"2\" tilewidth=\"64\">\n"
        " <tileset firstgid=\"1\" source=\"a.tsx\"/>\n"
        " <layer name=\"tiles\">\n"
        "  <data encoding=\"csv\">1,2,3,4</data>\n"
        " </layer>\n"
        " <layer name=\"collision\">\n"
        "  <data encoding=\"csv\">0,0,0,0</data>\n"
        " </layer>\n"
        "</map>\n";

    ParsedMap   map;
    std::string error;
    INFO("error: " << error);
    CHECK(tmx_parse(tmx, &map, &error));
}
