#include <doctest/doctest.h>

#include <ostream>
#include <string>

#include "core/arena.h"
#include "render/atlas.h"
#include "vm/script_load.h"
#include "formats/state.h"
#include "vm/symbols_load.h"
#include "vm/vm.h"

// Cadena completa que hace falta para que un @bg o un @show se vean en pantalla:
//
//   bg_id / actor_id+pose_id  ->  nombre (tabla de simbolos)  ->  sprite (atlas)
//
// En M13 el eslabon del medio eran tres tablas dentro de CADA .vnc, porque el id era un
// indice de un interner local a esa compilacion. En M14 es la tabla de simbolos del
// proyecto (ADR-0067): una sola para todos los guiones, lo que ademas hace que el id
// signifique lo mismo en todos y que GameState pueda sobrevivir a un guardado entre guiones
// distintos.
//
// Lo unico que este test no cubre es la llamada final a render_draw_sprite, que necesita GPU.

TEST_CASE("escena: los ids del .vnc resuelven por la tabla de simbolos y estan en el atlas") {
    CompiledScript script{};
    REQUIRE(script_load("demo.vnc", &g_arena_scene, &script) == ScriptLoadResult::Ok);
    REQUIRE(atlas_load(&g_arena_perm));
    REQUIRE(symbols_load(&g_arena_perm));

    REQUIRE(symbols_count(SymKind::Actor) > 0);
    REQUIRE(symbols_count(SymKind::Bg) > 0);

    u32 shows_checked = 0;
    u32 bgs_checked   = 0;
    for (u32 i = 0; i < script.cmd_count; ++i) {
        const Cmd& cmd = script.cmds[i];
        if (cmd.kind == CmdKind::Show) {
            // El id 0 significa "slot vacio" (SPEC.md #8.2) y esta reservado en todas las
            // clases, asi que ningun nombre real puede recibirlo.
            CHECK(cmd.show.actor_id != 0);
            CHECK(cmd.show.pose_id != 0);

            std::string actor = symbols_name(SymKind::Actor, cmd.show.actor_id);
            std::string pose  = symbols_name(SymKind::Pose, cmd.show.pose_id);
            CHECK_FALSE(actor.empty());
            CHECK_FALSE(pose.empty());

            AtlasSprite s{};
            std::string sprite = "actor_" + actor + "_" + pose;
            INFO("sprite de actor: " << sprite);
            CHECK(atlas_find(sprite.c_str(), &s));
            shows_checked += 1;
        } else if (cmd.kind == CmdKind::Bg) {
            CHECK(cmd.bg.bg_id != 0);
            std::string bg = symbols_name(SymKind::Bg, cmd.bg.bg_id);
            CHECK_FALSE(bg.empty());

            AtlasSprite s{};
            std::string sprite = "bg_" + bg;
            INFO("sprite de fondo: " << sprite);
            CHECK(atlas_find(sprite.c_str(), &s));
            bgs_checked += 1;
        }
    }

    // Si demo.vns dejara de tener @show/@bg, el test pasaria sin comprobar nada.
    CHECK(shows_checked > 0);
    CHECK(bgs_checked > 0);
    MESSAGE("comprobados " << shows_checked << " @show y " << bgs_checked << " @bg");
}

TEST_CASE("escena: los ids son los MISMOS en dos guiones distintos (ADR-0067)") {
    // Esta es la propiedad que M13 no tenia y que motivo la tabla del proyecto: con ids
    // locales a cada compilacion, el actor 1 de demo.vnc y el actor 1 de
    // demo_transitions.vnc podian ser personajes distintos, y por eso `actors[]` no podia
    // sobrevivir a un guardado si se cargaba con otro guion.
    REQUIRE(symbols_load(&g_arena_perm));

    CompiledScript a{}, b{};
    // Los dos comparten el actor "marta"; demo.vns usa otros, asi que compararlo con
    // cualquiera de estos no probaria nada.
    REQUIRE(script_load("demo_transitions.vnc", &g_arena_scene, &a) == ScriptLoadResult::Ok);
    REQUIRE(script_load("demo_audio.vnc", &g_arena_scene, &b) == ScriptLoadResult::Ok);

    // Se busca un actor que aparezca en los dos y se comprueba que trae el mismo id.
    u32 shared = 0;
    for (u32 i = 0; i < a.cmd_count; ++i) {
        if (a.cmds[i].kind != CmdKind::Show) {
            continue;
        }
        const char* name_a = symbols_name(SymKind::Actor, a.cmds[i].show.actor_id);
        for (u32 j = 0; j < b.cmd_count; ++j) {
            if (b.cmds[j].kind != CmdKind::Show) {
                continue;
            }
            const char* name_b = symbols_name(SymKind::Actor, b.cmds[j].show.actor_id);
            if (std::string(name_a) == std::string(name_b)) {
                CHECK(a.cmds[i].show.actor_id == b.cmds[j].show.actor_id);
                shared += 1;
            }
        }
    }
    // Si los guiones dejaran de compartir un actor, este test pasaria sin comprobar NADA.
    // Paso la primera vez que lo escribi (0 compartidos), asi que la cuenta es parte del
    // test y no solo un mensaje.
    MESSAGE("actores compartidos entre los dos guiones: " << shared);
    CHECK(shared > 0);
}

TEST_CASE("escena: un id fuera de la tabla devuelve cadena vacia, no basura") {
    REQUIRE(symbols_load(&g_arena_perm));
    CHECK(std::string(symbols_name(SymKind::Actor, 0)) == "");       // 0 = ninguno
    CHECK(std::string(symbols_name(SymKind::Bg, 0)) == "");
    CHECK(std::string(symbols_name(SymKind::Actor, 60000)) == "");    // fuera de rango
    CHECK(std::string(symbols_name(SymKind::Pose, 60000)) == "");
}

TEST_CASE("simbolos: nombre -> id -> nombre da la vuelta completa") {
    REQUIRE(symbols_load(&g_arena_perm));
    u16 id = symbols_id(SymKind::Actor, "marta");
    if (id != 0) {
        CHECK(std::string(symbols_name(SymKind::Actor, id)) == "marta");
    }
    CHECK(symbols_id(SymKind::Actor, "no_existe_este_actor") == 0);
    CHECK(symbols_id(SymKind::Actor, nullptr) == 0);
}
