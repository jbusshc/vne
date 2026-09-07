#include "vm/vm.h"

#include "base/log.h"
#include "vm/backlog.h"
#include "vm/rollback.h"

namespace {

constexpr u32 k_max_chained_steps = 4096;  // guarda contra bucles de comandos instantaneos

// Las tres operaciones de cada comando, como funciones libres (skill vne-script-dsl), no
// como metodos. Cada switch esta escrito sin `default` para que -Wswitch obligue a cubrir
// todo CmdKind existente cuando se anada uno nuevo.

void cmd_start(const Cmd& cmd, GameState* state) {
    switch (cmd.kind) {
        case CmdKind::Nop:
        case CmdKind::Label:
        case CmdKind::End:
            break;
        case CmdKind::Say:
            // Instantanea justo antes de ejecutar el comando (SPEC.md #8.3), y entrada
            // de backlog (SPEC.md #8.4): ambas antes de mutar nada de este comando.
            rollback_capture(&g_rollback, *state);
            backlog_push(&g_backlog, cmd.say.speaker_id, cmd.say.text_id, 0xFFFFu);
            // El avance por input real lo maneja VnMode (M7); aqui solo se registra que
            // hay una linea pendiente de mostrar.
            state->vm.waiting_for_input = 1;
            break;
        case CmdKind::Show: {
            u8 slot = cmd.show.slot < k_max_actor_slots ? cmd.show.slot : 0;
            ActorSlot& actor = state->actors[slot];
            actor.actor_id   = cmd.show.actor_id;
            actor.pose_id    = cmd.show.pose_id;
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
            return true;
        case CmdKind::Say:
            // Instantaneo en M3: no hay todavia una UI real que espere un clic (M7).
            state->vm.waiting_for_input = 0;
            return true;
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
    }
    return true;
}

void cmd_skip_to_end(const Cmd& cmd, GameState* state) {
    switch (cmd.kind) {
        case CmdKind::Nop:
        case CmdKind::Label:
        case CmdKind::End:
        case CmdKind::Jump:
        case CmdKind::Wait:
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
            break;
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
            cmd_start(cmd, state);
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
        cmd_start(cmd, state);
    }
    cmd_skip_to_end(cmd, state);
    vm->cmd_phase = 0;
    if (cmd.kind != CmdKind::End) {
        vm->pc += 1;
    }
}
