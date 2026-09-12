#include <doctest/doctest.h>

#include "base/hash.h"
#include "base/rng.h"
#include "script/lua_bindings.h"
#include "vm/symbols_load.h"
#include "vm/state.h"
#include "vm/vm.h"

// lua_init() ya se llamo una vez en test_main.cpp (analogo a rollback_init/backlog_reset):
// sol2 exige un unico interprete persistente creado fuera del bucle de frame (SPEC.md #9.4).

TEST_CASE("lua: vn.set_var/get_var leen y escriben GameState.vars por hash de nombre") {
    GameState state{};
    lua_run("vn.set_var('confianza', 7)", &state, nullptr);
    CHECK(state.vars[symbols_id(SymKind::Var, "confianza")] == 7);

    lua_run("vn.set_var('confianza', vn.get_var('confianza') + 1)", &state, nullptr);
    CHECK(state.vars[symbols_id(SymKind::Var, "confianza")] == 8);
}

TEST_CASE("lua: vn.set_flag/get_flag leen y escriben bits de GameState.flags") {
    GameState state{};
    CHECK_FALSE(state.flags[0] != 0);
    lua_run("vn.set_flag('conocio_a_marta', true)", &state, nullptr);

    GameState state2 = state;
    lua_run("assert(vn.get_flag('conocio_a_marta') == true)", &state2, nullptr);
    lua_run("assert(vn.get_flag('otra_bandera_cualquiera') == false)", &state2, nullptr);
}

TEST_CASE("lua: vn.random() avanza rng_state de forma deterministica") {
    GameState state{};
    state.rng_state = 12345u;
    u32 expected     = state.rng_state;
    expected         = rng_next(&expected);

    lua_run("vn.set_var('r', vn.random() % 1000000)", &state, nullptr);
    // No comparamos el valor exacto que ve Lua (pasa por double y modulo), solo que
    // rng_state avanzo exactamente un paso, igual que rng_next() en C++.
    CHECK(state.rng_state == expected);
}

TEST_CASE("lua: vn.jump mueve el pc a la etiqueta pedida") {
    ScriptLabel labels[1]{};
    labels[0].name_hash = fnv1a_u32("capitulo_2");
    labels[0].pc         = 5;

    Cmd cmds[1]{};
    cmds[0].kind            = CmdKind::LuaCall;
    CompiledScript script{cmds, 1, "", 0, labels, 1, nullptr, 0};

    GameState state{};
    lua_run("vn.jump('capitulo_2')", &state, &script);
    // lua_run replica el mismo -1 que Jump/Call: el +1 automatico de vm_update tras
    // completar el LuaCall deja el pc final en target_pc.
    CHECK(state.vm.pc == 4);
}
