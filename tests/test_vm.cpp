#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "audio/audio.h"
#include "core/hash.h"
#include "script/compiler.h"
#include "script/parser.h"
#include "test_config.h"
#include "vm/symbols_load.h"
#include "vm/save.h"
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

TEST_CASE("vm: Move fija actors[slot].x/y al instante y cmd_timer solo pacea (M12)") {
    // Ver el comentario de cmd_start en vm.cpp: al igual que Bg, Move no interpola desde
    // la posicion anterior -- eso es problema del renderer cuando exista uno -- solo
    // aplica el destino ya y usa cmd_timer/seconds como pausa antes del siguiente comando.
    Cmd cmds[2]{};
    cmds[0].kind         = CmdKind::Move;
    cmds[0].move.slot    = 2;
    cmds[0].move.x       = 100.0f;
    cmds[0].move.y       = 200.0f;
    cmds[0].move.seconds = 1.0f;
    cmds[1].kind         = CmdKind::End;
    CompiledScript script{cmds, 2, "", 0};

    GameState state{};
    CHECK_FALSE(vm_update(&state.vm, &state, script, 0.016f));
    // Se aplico al instante, en el primer frame, no gradualmente:
    CHECK(state.actors[2].x == doctest::Approx(100.0f));
    CHECK(state.actors[2].y == doctest::Approx(200.0f));
    CHECK(state.vm.pc == 0);  // sigue pausado: solo paso 0.016s de 1.0s

    CHECK(vm_update(&state.vm, &state, script, 1.0f));
    CHECK(state.vm.pc == 1);
}

TEST_CASE("vm: skip_to_end completa Move al instante sin esperar la pausa") {
    Cmd cmds[2]{};
    cmds[0].kind         = CmdKind::Move;
    cmds[0].move.slot    = 0;
    cmds[0].move.x       = 5.0f;
    cmds[0].move.y       = 5.0f;
    cmds[0].move.seconds = 10.0f;
    cmds[1].kind         = CmdKind::End;
    CompiledScript script{cmds, 2, "", 0};

    GameState state{};
    vm_skip_current(&state.vm, &state, script);
    CHECK(state.actors[0].x == doctest::Approx(5.0f));
    CHECK(state.vm.pc == 1);
}

TEST_CASE("vm: Transition no toca GameState, solo pacea con cmd_timer (M12)") {
    // A proposito: el renderer lee transition_kind y el umbral directamente de
    // script.cmds[pc] + vm.cmd_timer (ver vm.cpp), sin ningun campo nuevo en GameState.
    Cmd cmds[2]{};
    cmds[0].kind                       = CmdKind::Transition;
    cmds[0].transition.transition_kind = TransitionKind::Wipe;
    cmds[0].transition.seconds         = 0.5f;
    cmds[1].kind                       = CmdKind::End;
    CompiledScript script{cmds, 2, "", 0};

    GameState  state{};
    GameState  before = state;
    CHECK_FALSE(vm_update(&state.vm, &state, script, 0.3f));
    CHECK(state.vm.cmd_timer == doctest::Approx(0.3f));

    // Nada fuera de vm.cmd_timer/cmd_phase cambio: el resto de GameState sigue igual. Se
    // compara sobre una COPIA, no sobre `state` (seguir usandola despues con el timer
    // pisado a 0 arruinaria el resto del test).
    GameState after_for_compare        = state;
    after_for_compare.vm.cmd_timer = before.vm.cmd_timer;
    after_for_compare.vm.cmd_phase = before.vm.cmd_phase;
    CHECK(std::memcmp(&after_for_compare, &before, sizeof(GameState)) == 0);

    CHECK(vm_update(&state.vm, &state, script, 0.2f));
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
    std::string path = std::string(SZ_SOURCE_DIR) + "/assets_src/scripts/demo.vns";
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
    CompileResult compiled = compile_instructions(parsed.instructions, "demo.vns",
                                                symbols_for_single_script(parsed.instructions));
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

TEST_CASE("vm: Bgm actualiza bgm_track_id/bgm_position; StopBgm los resetea (SPEC.md #12)") {
    std::string path = std::string(SZ_SOURCE_DIR) + "/assets_src/scripts/demo_audio.vns";
    std::FILE*  f    = std::fopen(path.c_str(), "rb");
    REQUIRE(f != nullptr);
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::string source(static_cast<usize>(size), '\0');
    REQUIRE(std::fread(source.data(), 1, static_cast<usize>(size), f) ==
            static_cast<usize>(size));
    std::fclose(f);

    ParseResult parsed = parse_script(source, "demo_audio.vns");
    REQUIRE(parsed.ok());
    CompileResult compiled = compile_instructions(parsed.instructions, "demo_audio.vns",
                                                symbols_for_single_script(parsed.instructions));
    REQUIRE(compiled.ok());
    CompiledScript script{compiled.data.cmds.data(),
                           static_cast<u32>(compiled.data.cmds.size()),
                           compiled.data.string_pool.data(),
                           static_cast<u32>(compiled.data.string_pool.size())};

    GameState state{};
    bool      finished = false;
    u32       steps    = 0;
    u16       track_id_after_bgm = 0;
    while (!finished && steps < script.cmd_count + 1) {
        CmdKind kind = script.cmds[state.vm.pc].kind;
        vm_skip_current(&state.vm, &state, script);
        steps += 1;
        if (kind == CmdKind::Bgm && track_id_after_bgm == 0) {
            track_id_after_bgm = state.bgm_track_id;
        }
        if (kind == CmdKind::End) {
            finished = true;
        }
    }
    REQUIRE(finished);
    CHECK(track_id_after_bgm != 0);
    // El guion termina con @stopbgm: bgm_track_id debe quedar en 0.
    CHECK(state.bgm_track_id == 0);
}

TEST_CASE("vm: vm_resync_after_state_change restaura la pista de musica y su posicion "
          "tras cargar una partida (criterio de M6, SPEC.md #12)") {
    GameState state{};
    state.bgm_track_id = static_cast<u16>(fnv1a_u32("tema_a") % 65536u);
    state.bgm_position  = 1.5f;
    state.bus_volume[1]  = 0.25f;  // Bus::Music

    vm_resync_after_state_change(&state);

    CHECK(audio_music_position() == doctest::Approx(1.5f).epsilon(0.1));
}

TEST_CASE("vm: JumpIf salta solo cuando la condicion se cumple") {
    Cmd cmds[3]{};
    cmds[0].kind              = CmdKind::JumpIf;
    cmds[0].jump_if.var_id    = 0;
    cmds[0].jump_if.op        = CmpOp::Ge;
    cmds[0].jump_if.rhs       = 3;
    cmds[0].jump_if.target_pc = 2;
    cmds[1].kind             = CmdKind::Wait;  // instantaneo no sirve: hay que ver si se salta o no
    cmds[1].wait.seconds     = 1.0f;
    cmds[2].kind             = CmdKind::End;
    CompiledScript script{cmds, 3, "", 0};

    GameState state{};
    state.vars[0] = 5;  // >= 3: debe saltar directo a End, saltandose el Wait
    CHECK(vm_update(&state.vm, &state, script, 0.016f));
    CHECK(state.vm.pc == 2);

    GameState state2{};
    state2.vars[0] = 1;  // < 3: no salta, se detiene en el Wait
    CHECK_FALSE(vm_update(&state2.vm, &state2, script, 0.016f));
    CHECK(state2.vm.pc == 1);
}

TEST_CASE("vm: SetVar y AddVar mutan GameState.vars") {
    Cmd cmds[3]{};
    cmds[0].kind             = CmdKind::SetVar;
    cmds[0].set_var.var_id   = 7;
    cmds[0].set_var.value    = 10;
    cmds[1].kind             = CmdKind::AddVar;
    cmds[1].add_var.var_id   = 7;
    cmds[1].add_var.value    = 5;
    cmds[2].kind             = CmdKind::End;
    CompiledScript script{cmds, 3, "", 0};

    GameState state{};
    vm_skip_current(&state.vm, &state, script);
    CHECK(state.vars[7] == 10);
    vm_skip_current(&state.vm, &state, script);
    CHECK(state.vars[7] == 15);
}

TEST_CASE("vm: Call guarda la direccion de retorno y Return la restaura") {
    // pc0: Call -> pc2 (subrutina). pc1: Wait (para poder observar que se volvio aqui,
    // en vez de encadenarse instantaneamente con el resto). pc2: subrutina, Return.
    // pc3: End.
    Cmd cmds[4]{};
    cmds[0].kind           = CmdKind::Call;
    cmds[0].call.target_pc = 2;
    cmds[1].kind           = CmdKind::Wait;
    cmds[1].wait.seconds   = 1.0f;
    cmds[2].kind           = CmdKind::Return;
    cmds[3].kind           = CmdKind::End;
    CompiledScript script{cmds, 4, "", 0};

    GameState state{};
    // Call y Return son instantaneos y se encadenan en la misma llamada; el Wait de
    // pc1 (a donde Return vuelve) es lo primero que de verdad detiene el frame.
    CHECK_FALSE(vm_update(&state.vm, &state, script, 0.016f));
    CHECK(state.vm.pc == 1);
    CHECK(state.vm.call_depth == 0);
}

TEST_CASE("vm: Return sin Call no revienta, solo se ignora y sigue") {
    Cmd cmds[2]{};
    cmds[0].kind = CmdKind::Return;
    cmds[1].kind = CmdKind::End;
    CompiledScript script{cmds, 2, "", 0};

    GameState state{};
    CHECK(vm_update(&state.vm, &state, script, 0.016f));
    CHECK(state.vm.pc == 1);
}

TEST_CASE("vm: Choice no se completa solo, hace falta vm_select_choice") {
    ChoiceOption options[2]{};
    options[0].text_id   = 0;
    options[0].target_pc = 2;
    options[1].text_id       = 0;
    options[1].target_pc     = 3;
    options[1].has_condition = 1;
    options[1].cond_var_id   = 0;
    options[1].cond_op       = CmpOp::Gt;
    options[1].cond_rhs      = 10;

    Cmd cmds[4]{};
    cmds[0].kind                 = CmdKind::Choice;
    cmds[0].choice.first_option = 0;
    cmds[0].choice.option_count = 2;
    cmds[1].kind                 = CmdKind::ChoiceEnd;
    cmds[2].kind                 = CmdKind::Nop;
    cmds[3].kind                 = CmdKind::End;
    CompiledScript script{cmds, 4, "", 0, nullptr, 0, options, 2};

    GameState state{};
    CHECK_FALSE(vm_update(&state.vm, &state, script, 0.016f));
    CHECK(state.vm.pc == 0);  // sigue parado en el Choice

    // Opcion 1 tiene una condicion (var0 > 10) que no se cumple: se rechaza.
    CHECK_FALSE(vm_select_choice(&state.vm, &state, script, 1));
    CHECK(state.vm.pc == 0);

    // Opcion 0 no tiene condicion: se acepta y mueve el pc a su target_pc.
    CHECK(vm_select_choice(&state.vm, &state, script, 0));
    CHECK(state.vm.pc == 2);
}

TEST_CASE("vm: vm_skip_current en un Choice elige la primera opcion visible") {
    ChoiceOption options[2]{};
    options[0].text_id       = 0;
    options[0].target_pc     = 3;
    options[0].has_condition = 1;
    options[0].cond_var_id   = 0;
    options[0].cond_op       = CmpOp::Gt;
    options[0].cond_rhs      = 100;  // nunca se cumple
    options[1].text_id       = 0;
    options[1].target_pc     = 2;  // sin condicion: esta es la que se elige

    Cmd cmds[4]{};
    cmds[0].kind                 = CmdKind::Choice;
    cmds[0].choice.first_option = 0;
    cmds[0].choice.option_count = 2;
    cmds[1].kind                 = CmdKind::ChoiceEnd;
    cmds[2].kind                 = CmdKind::Nop;
    cmds[3].kind                 = CmdKind::End;
    CompiledScript script{cmds, 4, "", 0, nullptr, 0, options, 2};

    GameState state{};
    vm_skip_current(&state.vm, &state, script);
    CHECK(state.vm.pc == 2);
}

TEST_CASE("vm: el guion de ramificacion de M5 (3 ramas, 2 finales) se recorre completo "
          "(SPEC.md #12)") {
    std::string path = std::string(SZ_SOURCE_DIR) + "/assets_src/scripts/demo_branching.vns";
    std::FILE*  f    = std::fopen(path.c_str(), "rb");
    REQUIRE(f != nullptr);
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::string source(static_cast<usize>(size), '\0');
    REQUIRE(std::fread(source.data(), 1, static_cast<usize>(size), f) ==
            static_cast<usize>(size));
    std::fclose(f);

    ParseResult parsed = parse_script(source, "demo_branching.vns");
    REQUIRE(parsed.ok());
    CompileResult compiled = compile_instructions(parsed.instructions, "demo_branching.vns",
                                                symbols_for_single_script(parsed.instructions));
    REQUIRE(compiled.ok());

    CompiledScript script{compiled.data.cmds.data(),
                           static_cast<u32>(compiled.data.cmds.size()),
                           compiled.data.string_pool.data(),
                           static_cast<u32>(compiled.data.string_pool.size()),
                           nullptr,
                           0,
                           compiled.data.choice_options.data(),
                           static_cast<u32>(compiled.data.choice_options.size())};

    // Rama 1: "Intentar ganar mas confianza" -> rama_reintentar -> confianza llega a 6
    // (>=3) -> final_bueno.
    {
        GameState state{};
        bool      finished = false;
        u32       guard    = 0;
        while (!finished && guard < script.cmd_count + 1) {
            const Cmd& cmd = script.cmds[state.vm.pc];
            if (cmd.kind == CmdKind::Choice) {
                REQUIRE(vm_select_choice(&state.vm, &state, script, 0));
                continue;
            }
            vm_skip_current(&state.vm, &state, script);
            guard += 1;
            if (cmd.kind == CmdKind::End) {
                finished = true;
            }
        }
        REQUIRE(finished);
        CHECK(state.vars[symbols_id(SymKind::Var, "confianza")] >= 3);
    }

    // Rama 3: "Ir directo al final" -> rama_directa -> @call/@return -> final_bueno.
    {
        GameState state{};
        bool      finished = false;
        u32       guard    = 0;
        while (!finished && guard < script.cmd_count + 1) {
            const Cmd& cmd = script.cmds[state.vm.pc];
            if (cmd.kind == CmdKind::Choice) {
                REQUIRE(vm_select_choice(&state.vm, &state, script, 2));
                continue;
            }
            vm_skip_current(&state.vm, &state, script);
            guard += 1;
            if (cmd.kind == CmdKind::End) {
                finished = true;
            }
        }
        REQUIRE(finished);
        // subrutina_registro deja intentos_lua vivo via @lua antes de la @choice, y
        // ademas intentos = 99 tras el @call: confirma que Call/Return y LuaCall
        // corrieron de verdad.
        CHECK(state.vars[symbols_id(SymKind::Var, "intentos")] == 99);
    }

    // Rama 2 ("Rendirse") esta condicionada a intentos > 5: en un guion recien empezado
    // (intentos == 1 tras el primer @add) la opcion debe rechazarse.
    {
        GameState state{};
        bool      reached_choice = false;
        u32       guard          = 0;
        while (!reached_choice && guard < script.cmd_count + 1) {
            const Cmd& cmd = script.cmds[state.vm.pc];
            if (cmd.kind == CmdKind::Choice) {
                reached_choice = true;
                break;
            }
            vm_skip_current(&state.vm, &state, script);
            guard += 1;
        }
        REQUIRE(reached_choice);
        CHECK_FALSE(vm_select_choice(&state.vm, &state, script, 1));
    }
}
