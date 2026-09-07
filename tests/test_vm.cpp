#include <doctest/doctest.h>

#include <cstdio>
#include <string>

#include "script/compiler.h"
#include "script/parser.h"
#include "test_config.h"
#include "vm/vm.h"

TEST_CASE("vm: Wait no completa antes de tiempo y completa exactamente al llegar") {
    Cmd cmds[2]{};
    cmds[0].kind         = CmdKind::Wait;
    cmds[0].wait.seconds = 1.0f;
    cmds[1].kind         = CmdKind::End;
    CompiledScript script{cmds, 2, "", 0};

    GameState state{};

    CHECK_FALSE(vm_update(&state.vm, &state, script, 0.5f));
    CHECK(state.vm.pc == 0);
    CHECK_FALSE(vm_update(&state.vm, &state, script, 0.4f));
    CHECK(state.vm.pc == 0);
    CHECK(vm_update(&state.vm, &state, script, 0.2f));  // 0.5+0.4+0.2 >= 1.0: Wait y End
    CHECK(state.vm.pc == 1);
}

TEST_CASE("vm: Jump mueve el pc sin gastar dt en el comando saltado") {
    Cmd cmds[3]{};
    cmds[0].kind            = CmdKind::Jump;
    cmds[0].jump.target_pc = 2;
    cmds[1].kind            = CmdKind::Wait;
    cmds[1].wait.seconds    = 100.0f;  // no deberia ejecutarse nunca
    cmds[2].kind            = CmdKind::End;
    CompiledScript script{cmds, 3, "", 0};

    GameState state{};
    CHECK(vm_update(&state.vm, &state, script, 0.016f));
    CHECK(state.vm.pc == 2);
}

TEST_CASE("vm: skip_to_end completa Show al instante sin esperar el fade") {
    Cmd cmds[2]{};
    cmds[0].kind           = CmdKind::Show;
    cmds[0].show.actor_id = 1;
    cmds[0].show.pose_id  = 1;
    cmds[0].show.slot     = 0;
    cmds[0].show.fade     = 5.0f;
    cmds[1].kind           = CmdKind::End;
    CompiledScript script{cmds, 2, "", 0};

    GameState state{};
    CHECK_FALSE(vm_update(&state.vm, &state, script, 0.016f));
    CHECK(state.actors[0].alpha < 1.0f);

    vm_skip_current(&state.vm, &state, script);
    CHECK(state.actors[0].alpha == doctest::Approx(1.0f));
    CHECK(state.vm.pc == 1);
}

TEST_CASE("vm: skip_to_end completa Hide al instante y libera el slot") {
    Cmd cmds[2]{};
    cmds[0].kind        = CmdKind::Hide;
    cmds[0].hide.slot   = 0;
    cmds[0].hide.fade   = 5.0f;
    cmds[1].kind        = CmdKind::End;
    CompiledScript script{cmds, 2, "", 0};

    GameState state{};
    state.actors[0].actor_id = 7;
    state.actors[0].alpha    = 1.0f;

    vm_skip_current(&state.vm, &state, script);
    CHECK(state.actors[0].alpha == doctest::Approx(0.0f));
    CHECK(state.actors[0].actor_id == 0);
}

TEST_CASE("vm: el guion de prueba de 200+ lineas se ejecuta completo (SPEC.md #12)") {
    std::string path = std::string(VNE_SOURCE_DIR) + "/assets_src/scripts/demo.vns";
    std::FILE*  f    = std::fopen(path.c_str(), "rb");
    REQUIRE(f != nullptr);
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::string source(static_cast<usize>(size), '\0');
    REQUIRE(std::fread(source.data(), 1, static_cast<usize>(size), f) ==
            static_cast<usize>(size));
    std::fclose(f);

    // El propio archivo de prueba debe tener al menos 200 lineas, o no esta probando lo
    // que el criterio de SPEC.md #12 pide.
    u32 line_count = 1;
    for (char c : source) {
        if (c == '\n') line_count += 1;
    }
    CHECK(line_count >= 200);

    ParseResult parsed = parse_script(source, "demo.vns");
    REQUIRE(parsed.ok());
    CompileResult compiled = compile_instructions(parsed.instructions, "demo.vns");
    REQUIRE(compiled.ok());

    CompiledScript script{compiled.data.cmds.data(),
                           static_cast<u32>(compiled.data.cmds.size()),
                           compiled.data.string_pool.data(),
                           static_cast<u32>(compiled.data.string_pool.size())};

    GameState state{};
    bool      finished = false;
    u32       steps    = 0;
    while (!finished && steps < script.cmd_count + 1) {
        CmdKind kind = script.cmds[state.vm.pc].kind;
        vm_skip_current(&state.vm, &state, script);
        steps += 1;
        if (kind == CmdKind::End) {
            finished = true;
        }
    }

    CHECK(finished);
    CHECK(steps == script.cmd_count);
}
