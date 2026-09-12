#include "vm/vm.h"

#include "audio/audio.h"
#include "core/log.h"
#include "vm/backlog.h"
#include "vm/rollback.h"

void (*g_script_call_hook)(const char* code, GameState* state,
                            const CompiledScript* script) = nullptr;

namespace {

constexpr u32 k_max_chained_steps = 4096;  // guarda contra bucles de comandos instantaneos

bool eval_cmp(i32 lhs, CmpOp op, i32 rhs) {
    switch (op) {
        case CmpOp::Eq: return lhs == rhs;
        case CmpOp::Ne: return lhs != rhs;
        case CmpOp::Lt: return lhs < rhs;
        case CmpOp::Le: return lhs <= rhs;
        case CmpOp::Gt: return lhs > rhs;
        case CmpOp::Ge: return lhs >= rhs;
    }
    return false;
}

// Las tres operaciones de cada comando, como funciones libres (skill vne-script-dsl), no
// como metodos. Cada switch esta escrito sin `default` para que -Wswitch obligue a cubrir
// todo CmdKind existente cuando se anada uno nuevo.

void cmd_start(const Cmd& cmd, GameState* state, const CompiledScript& script) {
    switch (cmd.kind) {
        case CmdKind::Nop:
        case CmdKind::Label:
        case CmdKind::End:
        case CmdKind::ChoiceEnd:
            break;
        case CmdKind::Say:
            // Instantanea justo antes de ejecutar el comando (SPEC.md #8.3), y entrada
            // de backlog (SPEC.md #8.4): ambas antes de mutar nada de este comando.
            rollback_capture(&g_rollback, *state);
            backlog_push(&g_backlog, cmd.say.speaker_id, cmd.say.text_id, 0xFFFFu,
                          cmd.say.key_hash);
            // El avance por input real lo maneja VnMode (M7); aqui solo se registra que
            // hay una linea pendiente de mostrar.
            state->vm.waiting_for_input = 1;
            break;
        case CmdKind::Show: {
            u8 slot = cmd.show.slot < k_max_actor_slots ? cmd.show.slot : 0;
            ActorSlot& actor = state->actors[slot];
            bool       was_empty = actor.actor_id == 0;
            actor.actor_id   = cmd.show.actor_id;
            actor.pose_id    = cmd.show.pose_id;
            // Posicion por defecto derivada del slot (M13): coordenadas normalizadas 0..1,
            // los ocho slots repartidos a lo ancho y los pies cerca del borde inferior.
            // Hasta M13 nadie ponia x/y en Show, asi que un actor recien mostrado se quedaba
            // en (0,0); no se notaba porque nada lo dibujaba. Solo se aplica al ocupar un
            // slot vacio, para que un @show que cambia de pose no deshaga un @move previo.
            if (was_empty) {
                actor.x     = (static_cast<f32>(slot) + 0.5f) / static_cast<f32>(k_max_actor_slots);
                actor.y     = 0.75f;
                actor.scale = 1.0f;
            }
            actor.alpha      = cmd.show.fade > 0.0f ? actor.alpha : 1.0f;
            break;
        }
        case CmdKind::Hide:
            break;
        case CmdKind::Bg:
            state->bg_id = cmd.bg.bg_id;
            break;
        case CmdKind::Wait:
            break;
        case CmdKind::Jump:
            // -1 porque vm_update siempre hace pc += 1 tras completar un comando: asi el
            // avance generico deja el pc exactamente en target_pc, sin caso especial en
            // el bucle principal.
            state->vm.pc = cmd.jump.target_pc - 1;
            break;
        case CmdKind::SetVar:
            state->vars[cmd.set_var.var_id] = cmd.set_var.value;
            break;
        case CmdKind::AddVar:
            state->vars[cmd.add_var.var_id] += cmd.add_var.value;
            break;
        case CmdKind::JumpIf:
            if (eval_cmp(state->vars[cmd.jump_if.var_id], cmd.jump_if.op, cmd.jump_if.rhs)) {
                state->vm.pc = cmd.jump_if.target_pc - 1;  // mismo truco que Jump
            }
            break;
        case CmdKind::SetFlag: {
            // M13: mismo bitset que ya usaban vn.set_flag/vn.get_flag desde Lua
            // (lua/lua_bindings.cpp), con el mismo flag_id: los dos caminos tienen que
            // ver la misma bandera o @flag y Lua se contradirian.
            u8& byte = state->flags[cmd.set_flag.flag_id / 8];
            u8  bit  = static_cast<u8>(1u << (cmd.set_flag.flag_id % 8));
            byte     = cmd.set_flag.value != 0 ? static_cast<u8>(byte | bit)
                                               : static_cast<u8>(byte & ~bit);
            break;
        }
        case CmdKind::JumpIfFlag: {
            bool on = (state->flags[cmd.jump_if_flag.flag_id / 8] &
                       (1u << (cmd.jump_if_flag.flag_id % 8))) != 0;
            if (on == (cmd.jump_if_flag.expected != 0)) {
                state->vm.pc = cmd.jump_if_flag.target_pc - 1;  // mismo truco que Jump
            }
            break;
        }
        case CmdKind::Choice:
            // Rollback tambien antes de un Choice (SPEC.md #8.3, ADR-0027 cerrado en
            // M5): el jugador debe poder deshacer una decision igual que una linea de
            // dialogo. No hay entrada de backlog: Choice no es una linea hablada.
            rollback_capture(&g_rollback, *state);
            break;
        case CmdKind::Call:
            if (state->vm.call_depth < k_max_call_depth) {
                // Direccion de retorno: la instruccion siguiente a este Call. vm_update
                // sumara 1 al pc al completar este comando, asi que pc+1 (no pc) es la
                // que hay que guardar.
                state->vm.call_stack[state->vm.call_depth] = state->vm.pc + 1;
                state->vm.call_depth += 1;
            } else {
                log_error("Call: pila de llamadas llena (%u), guion ignorado", k_max_call_depth);
            }
            state->vm.pc = cmd.call.target_pc - 1;
            break;
        case CmdKind::Return:
            if (state->vm.call_depth > 0) {
                state->vm.call_depth -= 1;
                state->vm.pc = state->vm.call_stack[state->vm.call_depth] - 1;
            } else {
                log_error("Return sin Call correspondiente: se ignora, sigue a la "
                           "siguiente instruccion");
            }
            break;
        case CmdKind::LuaCall:
            if (g_script_call_hook != nullptr) {
                g_script_call_hook(script_string(script, cmd.lua_call.fn_id), state, &script);
            } else {
                log_error("LuaCall sin interprete registrado: se ignora (@lua necesita la "
                           "feature de scripting habilitada)");
            }
            break;
        case CmdKind::Sfx: {
            SoundHandle h{};
            if (audio_load(script_string(script, cmd.sfx.text_id), false, &h) ==
                AudioLoadResult::Ok) {
                audio_play(h, Bus::Sfx, 1.0f, false);
            }
            break;
        }
        case CmdKind::Bgm: {
            SoundHandle h{};
            if (audio_load_track(cmd.bgm.track_id, &h)) {
                audio_crossfade_music(h, cmd.bgm.fade);
                state->bgm_track_id = cmd.bgm.track_id;
                state->bgm_position = 0.0f;
            }
            break;
        }
        case CmdKind::StopBgm:
            audio_stop_music(cmd.stop_bgm.fade);
            state->bgm_track_id = 0;
            state->bgm_position = 0.0f;
            break;
        case CmdKind::Move: {
            // Igual que Bg (ver mas abajo): el estado logico se aplica AL INSTANTE aqui,
            // no interpolado. cmd_timer/seconds es solo temporizacion, para cuando exista
            // un renderer que interpole visualmente desde donde el sprite estuviera
            // dibujado hasta este destino ya fijado -- eso es un problema del renderer
            // (que arranca desde su propia posicion en pantalla), no de GameState.
            // Evita ademas anadir "posicion de origen" a VmState solo para esto, que
            // subiria la version de .vnsave sin necesidad real (ver ADR de M12).
            u8 slot          = cmd.move.slot < k_max_actor_slots ? cmd.move.slot : 0;
            state->actors[slot].x = cmd.move.x;
            state->actors[slot].y = cmd.move.y;
            break;
        }
        case CmdKind::Transition:
            // Puramente temporizado (cmd_timer, ver cmd_update). El renderer no necesita
            // ningun campo nuevo de GameState: lee transition_kind y calcula el umbral
            // directamente de script.cmds[pc] + vm.cmd_timer, exactamente como VnMode ya
            // lee el Say actual para dibujar el dialogo (ver game/vn_mode.cpp).
            break;
    }
}

// Devuelve true cuando el comando termina. dt ya viene a 0 para los comandos encadenados
// dentro de la misma llamada a vm_update (ver vm_update): no vuelven a gastar tiempo.
bool cmd_update(const Cmd& cmd, GameState* state, f32 dt) {
    switch (cmd.kind) {
        case CmdKind::Nop:
        case CmdKind::Label:
        case CmdKind::End:
        case CmdKind::Jump:
        case CmdKind::SetVar:
        case CmdKind::AddVar:
        case CmdKind::JumpIf:
        case CmdKind::SetFlag:
        case CmdKind::JumpIfFlag:
        case CmdKind::ChoiceEnd:
        case CmdKind::Call:
        case CmdKind::Return:
        case CmdKind::LuaCall:
        case CmdKind::Sfx:
        case CmdKind::Bgm:
        case CmdKind::StopBgm:
            return true;
        case CmdKind::Say:
            // Cierra ADR-0023 (M3: "Say no bloquea esperando input"): ahora si bloquea
            // de verdad. cmd_start ya puso waiting_for_input a 1; solo se completa
            // cuando algo externo (VnMode, SPEC.md #10) lo pone a 0 con vm_confirm_say().
            return state->vm.waiting_for_input == 0;
        case CmdKind::Choice:
            // Nunca se completa por si solo: hace falta vm_select_choice() (SPEC.md
            // #9.1, no hay timeout ni avance automatico salvo con vm_skip_current, que
            // resuelve la opcion via cmd_skip_to_end en vez de aqui).
            return false;
        case CmdKind::Show: {
            if (cmd.show.fade <= 0.0f) {
                return true;
            }
            u8 slot          = cmd.show.slot < k_max_actor_slots ? cmd.show.slot : 0;
            ActorSlot& actor = state->actors[slot];
            state->vm.cmd_timer += dt;
            f32 t   = state->vm.cmd_timer / cmd.show.fade;
            actor.alpha = t < 1.0f ? t : 1.0f;
            return state->vm.cmd_timer >= cmd.show.fade;
        }
        case CmdKind::Hide: {
            u8 slot          = cmd.hide.slot < k_max_actor_slots ? cmd.hide.slot : 0;
            ActorSlot& actor = state->actors[slot];
            if (cmd.hide.fade <= 0.0f) {
                actor.alpha = 0.0f;
                actor.actor_id = 0;
                return true;
            }
            state->vm.cmd_timer += dt;
            f32 t   = state->vm.cmd_timer / cmd.hide.fade;
            actor.alpha = t < 1.0f ? 1.0f - t : 0.0f;
            bool done = state->vm.cmd_timer >= cmd.hide.fade;
            if (done) {
                actor.actor_id = 0;
            }
            return done;
        }
        case CmdKind::Bg:
            // bg_id ya se aplico en cmd_start; el fade aqui es solo temporizacion (no
            // hay todavia un blend visual de dos fondos que sostener en GameState).
            if (cmd.bg.fade <= 0.0f) {
                return true;
            }
            state->vm.cmd_timer += dt;
            return state->vm.cmd_timer >= cmd.bg.fade;
        case CmdKind::Wait:
            state->vm.cmd_timer += dt;
            return state->vm.cmd_timer >= cmd.wait.seconds;
        case CmdKind::Move:
            // actors[slot].x/y ya se fijaron al instante en cmd_start (igual que Bg):
            // esto es solo la pausa antes de seguir con el siguiente comando.
            if (cmd.move.seconds <= 0.0f) {
                return true;
            }
            state->vm.cmd_timer += dt;
            return state->vm.cmd_timer >= cmd.move.seconds;
        case CmdKind::Transition:
            if (cmd.transition.seconds <= 0.0f) {
                return true;
            }
            state->vm.cmd_timer += dt;
            return state->vm.cmd_timer >= cmd.transition.seconds;
    }
    return true;
}

void cmd_skip_to_end(const Cmd& cmd, GameState* state, const CompiledScript& script) {
    switch (cmd.kind) {
        case CmdKind::Nop:
        case CmdKind::Label:
        case CmdKind::End:
        case CmdKind::Jump:
        case CmdKind::Wait:
        case CmdKind::SetVar:
        case CmdKind::AddVar:
        case CmdKind::JumpIf:
        case CmdKind::SetFlag:
        case CmdKind::JumpIfFlag:
        case CmdKind::ChoiceEnd:
        case CmdKind::Call:
        case CmdKind::Return:
        case CmdKind::LuaCall:
        case CmdKind::Sfx:
        case CmdKind::Bgm:
        case CmdKind::StopBgm:
            break;
        case CmdKind::Say:
            state->vm.waiting_for_input = 0;
            break;
        case CmdKind::Show: {
            u8 slot                = cmd.show.slot < k_max_actor_slots ? cmd.show.slot : 0;
            state->actors[slot].alpha = 1.0f;
            break;
        }
        case CmdKind::Hide: {
            u8 slot                = cmd.hide.slot < k_max_actor_slots ? cmd.hide.slot : 0;
            state->actors[slot].alpha    = 0.0f;
            state->actors[slot].actor_id = 0;
            break;
        }
        case CmdKind::Bg:
        case CmdKind::Move:
        case CmdKind::Transition:
            // Nada que limpiar: Move ya aplico su destino al instante en cmd_start (igual
            // que Bg), y Transition no toca GameState en ningun momento (ver cmd_start).
            break;
        case CmdKind::Choice: {
            // No hay UI que elija por el jugador en --autoplay-script ni en el test
            // obligatorio de M4/M5: se resuelve de forma deterministica a la primera
            // opcion cuya condicion (si tiene) se cumpla, igual que "skip_to_end
            // completa el efecto al instante" para cualquier otro comando.
            for (u8 k = 0; k < cmd.choice.option_count; ++k) {
                const ChoiceOption& opt = script.choice_options[cmd.choice.first_option + k];
                bool visible = opt.has_condition == 0 ||
                               eval_cmp(state->vars[opt.cond_var_id], opt.cond_op, opt.cond_rhs);
                if (visible) {
                    state->vm.pc = opt.target_pc - 1;  // mismo truco que Jump
                    break;
                }
            }
            break;
        }
    }
}

}  // namespace

bool vm_update(VmState* vm, GameState* state, const CompiledScript& script, f32 dt) {
    for (u32 step = 0; step < k_max_chained_steps; ++step) {
        if (vm->pc >= script.cmd_count) {
            return true;
        }
        const Cmd& cmd = script.cmds[vm->pc];
        if (vm->cmd_phase == 0) {
            cmd_start(cmd, state, script);
            vm->cmd_phase = 1;
            vm->cmd_timer = 0.0f;
        }
        bool done = cmd_update(cmd, state, dt);
        dt         = 0.0f;  // los comandos encadenados en esta misma llamada no gastan mas
        if (!done) {
            return false;
        }
        vm->cmd_phase = 0;
        if (cmd.kind == CmdKind::End) {
            return true;
        }
        vm->pc += 1;
    }
    log_error("vm_update: %u comandos instantaneos encadenados sin terminar el frame; "
              "posible bucle de Jump sin avance real",
              k_max_chained_steps);
    return false;
}

void vm_skip_current(VmState* vm, GameState* state, const CompiledScript& script) {
    if (vm->pc >= script.cmd_count) {
        return;
    }
    const Cmd& cmd = script.cmds[vm->pc];
    if (vm->cmd_phase == 0) {
        cmd_start(cmd, state, script);
    }
    cmd_skip_to_end(cmd, state, script);
    vm->cmd_phase = 0;
    if (cmd.kind != CmdKind::End) {
        vm->pc += 1;
    }
}

bool vm_select_choice(VmState* vm, GameState* state, const CompiledScript& script,
                       u8 option_index) {
    if (vm->pc >= script.cmd_count) {
        return false;
    }
    const Cmd& cmd = script.cmds[vm->pc];
    if (cmd.kind != CmdKind::Choice || option_index >= cmd.choice.option_count) {
        return false;
    }
    const ChoiceOption& opt = script.choice_options[cmd.choice.first_option + option_index];
    if (opt.has_condition && !eval_cmp(state->vars[opt.cond_var_id], opt.cond_op, opt.cond_rhs)) {
        return false;
    }
    // A diferencia de Jump/Call (que ajustan pc-1 dentro de cmd_start porque vm_update va
    // a sumar 1 al completar ESE comando), aqui Choice no se completa por si solo: este
    // es el evento externo que lo completa, asi que el pc final es directo, sin -1.
    vm->pc        = opt.target_pc;
    vm->cmd_phase = 0;
    return true;
}

bool vm_choice_option_available(const GameState& state, const CompiledScript& script,
                                 u8 option_index) {
    if (state.vm.pc >= script.cmd_count) {
        return false;
    }
    const Cmd& cmd = script.cmds[state.vm.pc];
    if (cmd.kind != CmdKind::Choice || option_index >= cmd.choice.option_count) {
        return false;
    }
    const ChoiceOption& opt = script.choice_options[cmd.choice.first_option + option_index];
    return opt.has_condition == 0 ||
           eval_cmp(state.vars[opt.cond_var_id], opt.cond_op, opt.cond_rhs);
}
