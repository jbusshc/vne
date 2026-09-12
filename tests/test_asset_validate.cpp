#include <doctest/doctest.h>

#include <ostream>
#include <string>

#include "script/asset_validate.h"
#include "script/parser.h"

// Validacion de nombres de asset (M13, cierra ADR-0022). El registro se fabrica a mano en
// vez de leer el atlas horneado: asi los tests fijan la REGLA, no el contenido de
// assets_src/png/, que puede cambiar sin que la regla cambie.

namespace {

AssetRegistry registry_with(std::initializer_list<const char*> names) {
    AssetRegistry r;
    for (const char* n : names) {
        r.sprite_names.emplace_back(n);
    }
    return r;
}

ParsedInstr show(const char* actor, const char* pose, u32 line) {
    ParsedInstr i{};
    i.kind  = InstrKind::Show;
    i.actor = actor;
    i.pose  = pose;
    i.line  = line;
    return i;
}

ParsedInstr bg(const char* name, u32 line) {
    ParsedInstr i{};
    i.kind = InstrKind::Bg;
    i.bg   = name;
    i.line = line;
    return i;
}

}  // namespace

TEST_CASE("asset_validate: la convencion de nombres es actor_<actor>_<pose> y bg_<nombre>") {
    CHECK(asset_sprite_name_for_actor("marta", "neutral") == "actor_marta_neutral");
    CHECK(asset_sprite_name_for_background("fondo_dia") == "bg_fondo_dia");

    // El prefijo existe para que actores y fondos no compartan espacio de nombres: un fondo
    // "marta" y un actor "marta" tienen que poder convivir en el mismo atlas plano.
    CHECK(asset_sprite_name_for_actor("marta", "x") != asset_sprite_name_for_background("marta"));
}

TEST_CASE("asset_validate: un actor y un fondo que existen no producen ningun error") {
    AssetRegistry r = registry_with({"actor_marta_neutral", "bg_fondo_dia"});
    std::vector<ParsedInstr> instrs = {show("marta", "neutral", 3), bg("fondo_dia", 4)};

    std::vector<ParseError> errors;
    validate_asset_names(instrs, "guion.vns", r, &errors);
    CHECK(errors.empty());
}

TEST_CASE("asset_validate: un actor desconocido es error con archivo y linea") {
    // Este es el criterio de M13 tal cual: "@show con un actor que no esta en el registro
    // falla la compilacion con archivo y linea, igual que ya hace una etiqueta desconocida".
    AssetRegistry            r      = registry_with({"actor_marta_neutral"});
    std::vector<ParsedInstr> instrs = {show("mrata", "neutral", 42)};

    std::vector<ParseError> errors;
    validate_asset_names(instrs, "guion.vns", r, &errors);

    REQUIRE(errors.size() == 1);
    CHECK(errors[0].file == "guion.vns");
    CHECK(errors[0].line == 42);
    // El mensaje nombra el sprite que se buscó, no solo el actor: sin eso, quien escribe el
    // guion no sabe qué archivo tiene que crear para arreglarlo.
    CHECK(errors[0].message.find("mrata") != std::string::npos);
    CHECK(errors[0].message.find("actor_mrata_neutral") != std::string::npos);
}

TEST_CASE("asset_validate: una pose desconocida de un actor que si existe tambien falla") {
    // El registro es por par actor+pose, no por actor: un actor valido con una pose que no
    // tiene sprite dibujaria nada en pantalla, que es el mismo fallo silencioso.
    AssetRegistry            r      = registry_with({"actor_marta_neutral"});
    std::vector<ParsedInstr> instrs = {show("marta", "enfadada", 7)};

    std::vector<ParseError> errors;
    validate_asset_names(instrs, "guion.vns", r, &errors);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].line == 7);
    CHECK(errors[0].message.find("enfadada") != std::string::npos);
}

TEST_CASE("asset_validate: un fondo desconocido es error, y se acumulan todos") {
    AssetRegistry            r = registry_with({"actor_marta_neutral"});
    std::vector<ParsedInstr> instrs = {bg("mansion", 1), show("otro", "pose", 2),
                                        bg("jardin", 3)};

    std::vector<ParseError> errors;
    validate_asset_names(instrs, "guion.vns", r, &errors);

    // Los tres a la vez, no solo el primero: quien escribe el guion prefiere verlos todos
    // de una pasada a arreglarlos de uno en uno.
    REQUIRE(errors.size() == 3);
    CHECK(errors[0].line == 1);
    CHECK(errors[1].line == 2);
    CHECK(errors[2].line == 3);
}

TEST_CASE("asset_validate: instrucciones que no son Show ni Bg se ignoran") {
    AssetRegistry r = registry_with({});

    ParsedInstr say{};
    say.kind = InstrKind::Say;
    say.text = "hola";
    say.line = 1;

    ParsedInstr wait{};
    wait.kind = InstrKind::Wait;
    wait.line = 2;

    std::vector<ParsedInstr> instrs = {say, wait};
    std::vector<ParseError>  errors;
    validate_asset_names(instrs, "guion.vns", r, &errors);
    CHECK(errors.empty());
}

TEST_CASE("asset_validate: leer un atlas que no existe devuelve false y no revienta") {
    AssetRegistry r;
    CHECK_FALSE(AssetRegistry::load_from_atlas_bin("no_existe_este_atlas.bin", &r));
    CHECK(r.sprite_names.empty());
}

TEST_CASE("asset_validate: el atlas horneado del build carga y trae los placeholders") {
    // Este si toca el atlas real: comprueba que el formato v3 que escribe sz_bake es el
    // que lee el validador (son dos binarios distintos con el layout duplicado a
    // proposito, asi que conviene fijarlo con un test).
    AssetRegistry r;
    REQUIRE(AssetRegistry::load_from_atlas_bin("assets_baked/atlas_00.bin", &r));
    CHECK(r.sprite_names.size() > 0);
    CHECK(r.contains("actor_marta_neutral"));
    CHECK(r.contains("bg_fondo_dia"));
    CHECK_FALSE(r.contains("actor_mrata_neutral"));
}
