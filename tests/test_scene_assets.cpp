#include <doctest/doctest.h>

#include <ostream>
#include <string>

#include "base/arena.h"
#include "gfx/atlas.h"
#include "vm/script_load.h"
#include "vm/state.h"
#include "vm/vm.h"

// Cadena completa que hace falta para que un @bg o un @show se vean en pantalla (M13):
//
//   bg_id / actor_id+pose_id  ->  nombre (tablas del .vnc v5)  ->  sprite (atlas)
//
// Hasta M13 el primer eslabon no existia: el id era un indice de un interner local al
// guion y no habia forma de volver al nombre, asi que nada podia dibujarse. Este test
// recorre la cadena entera sobre el demo.vnc horneado por el propio build; lo unico que no
// cubre es la llamada final a gfx_draw_sprite, que necesita GPU.

TEST_CASE("escena: los ids del .vnc resuelven a nombres y esos nombres estan en el atlas") {
    CompiledScript script{};
    REQUIRE(script_load("demo.vnc", &g_arena_scene, &script) == ScriptLoadResult::Ok);
    REQUIRE(atlas_load(&g_arena_perm));

    // Las tres tablas tienen contenido: demo.vns usa actores, poses y fondos.
    REQUIRE(script.actor_name_count > 0);
    REQUIRE(script.pose_name_count > 0);
    REQUIRE(script.bg_name_count > 0);

    u32 shows_checked = 0;
    u32 bgs_checked   = 0;
    for (u32 i = 0; i < script.cmd_count; ++i) {
        const Cmd& cmd = script.cmds[i];
        if (cmd.kind == CmdKind::Show) {
            // El id 0 significa "slot vacio" (SPEC.md #8.2). Que el interner empezara en 0
            // hacia al PRIMER actor de cada guion indistinguible de un hueco vacio: era un
            // bug real, invisible mientras nadie dibujaba. Los ids validos empiezan en 1.
            CHECK(cmd.show.actor_id != 0);
            CHECK(cmd.show.pose_id != 0);

            std::string actor = script_actor_name(script, cmd.show.actor_id);
            std::string pose  = script_pose_name(script, cmd.show.pose_id);
            CHECK_FALSE(actor.empty());
            CHECK_FALSE(pose.empty());

            AtlasSprite s{};
            std::string sprite = "actor_" + actor + "_" + pose;
            INFO("sprite de actor: " << sprite);
            CHECK(atlas_find(sprite.c_str(), &s));
            shows_checked += 1;
        } else if (cmd.kind == CmdKind::Bg) {
            CHECK(cmd.bg.bg_id != 0);
            std::string bg = script_bg_name(script, cmd.bg.bg_id);
            CHECK_FALSE(bg.empty());

            AtlasSprite s{};
            std::string sprite = "bg_" + bg;
            INFO("sprite de fondo: " << sprite);
            CHECK(atlas_find(sprite.c_str(), &s));
            bgs_checked += 1;
        }
    }

    // Si demo.vns dejara de tener @show/@bg, el test pasaria sin comprobar nada: se exige
    // que haya recorrido algo de verdad.
    CHECK(shows_checked > 0);
    CHECK(bgs_checked > 0);
    MESSAGE("comprobados " << shows_checked << " @show y " << bgs_checked << " @bg");
}

TEST_CASE("escena: un id fuera de la tabla devuelve cadena vacia, no basura") {
    CompiledScript script{};
    REQUIRE(script_load("demo.vnc", &g_arena_scene, &script) == ScriptLoadResult::Ok);

    CHECK(std::string(script_actor_name(script, 0)) == "");        // 0 = vacio
    CHECK(std::string(script_bg_name(script, 0)) == "");
    CHECK(std::string(script_actor_name(script, 60000)) == "");     // fuera de rango
    CHECK(std::string(script_pose_name(script, 60000)) == "");
    CHECK(std::string(script_bg_name(script, 60000)) == "");
}
