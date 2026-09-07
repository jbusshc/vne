#include "game/vn_mode.h"

#include <SDL3/SDL.h>

#include "gfx/gfx.h"
#include "vm/script_load.h"

namespace {

bool confirm_pressed(const InputState& input) {
    return input.key_pressed[SDL_SCANCODE_SPACE] || input.key_pressed[SDL_SCANCODE_RETURN] ||
           input.key_pressed[SDL_SCANCODE_KP_ENTER];
}

bool current_is_say(const VnMode& vn) {
    return vn.state->vm.pc < vn.script.cmd_count &&
           vn.script.cmds[vn.state->vm.pc].kind == CmdKind::Say;
}

void rebuild_layout_if_needed(VnMode* vn) {
    if (!current_is_say(*vn) || vn->layout_pc == vn->state->vm.pc) {
        return;
    }
    const Cmd& cmd  = vn->script.cmds[vn->state->vm.pc];
    const char* text = script_string(vn->script, cmd.say.text_id);
    // text_layout es la funcion cara de este modulo (skill vne-rendering): solo se llama
    // aqui, cuando la linea de dialogo cambia, nunca por frame.
    vn->current_layout   = text_layout(vn->font, text, 1700.0f, vn->layout_arena);
    vn->layout_pc         = vn->state->vm.pc;
    vn->visible_glyphs_f = 0.0f;
}

}  // namespace

void VnMode::update(const InputState& input, f32 dt) {
    if (finished || script.cmd_count == 0) {
        return;
    }

    if (input.key_pressed[SDL_SCANCODE_S]) {
        skip_mode = !skip_mode;
    }
    if (input.key_pressed[SDL_SCANCODE_A]) {
        auto_mode = !auto_mode;
    }

    if (skip_mode) {
        // Modo skip (SPEC.md #12: 1000 comandos en menos de 1 segundo): vm_skip_current
        // resuelve cada comando al instante, incluido un Say pendiente (su
        // skip_to_end ya limpia waiting_for_input). Se para solo si el guion termina o
        // si el pc cae en un Choice (nadie puede elegir por el jugador en automatico).
        for (u32 i = 0; i < k_skip_steps_per_frame; ++i) {
            if (state->vm.pc >= script.cmd_count) {
                finished = true;
                break;
            }
            if (script.cmds[state->vm.pc].kind == CmdKind::Choice) {
                skip_mode = false;
                break;
            }
            CmdKind kind = script.cmds[state->vm.pc].kind;
            vm_skip_current(&state->vm, state, script);
            if (kind == CmdKind::End) {
                finished = true;
                break;
            }
        }
        rebuild_layout_if_needed(this);
        visible_glyphs_f = static_cast<f32>(current_layout.count);
        return;
    }

    rebuild_layout_if_needed(this);

    bool waiting_on_say = current_is_say(*this) && state->vm.waiting_for_input != 0;
    bool typewriter_done = visible_glyphs_f >= static_cast<f32>(current_layout.count);

    if (waiting_on_say) {
        if (!typewriter_done) {
            visible_glyphs_f += k_typewriter_glyphs_per_second * dt;
            // El primer confirmar mientras el efecto de maquina de escribir esta en
            // marcha lo completa al instante en vez de avanzar de linea (convencion
            // estandar de novela visual): igual que skip_to_end pero solo para el
            // texto, no para el resto del comando.
            if (confirm_pressed(input)) {
                visible_glyphs_f = static_cast<f32>(current_layout.count);
            }
        } else if (auto_mode) {
            auto_hold_timer += dt;
            if (auto_hold_timer >= k_auto_hold_seconds) {
                auto_hold_timer = 0.0f;
                vm_confirm_say(state);
            }
        } else if (confirm_pressed(input)) {
            vm_confirm_say(state);
        }
    }

    if (vm_update(&state->vm, state, script, dt)) {
        finished = true;
    }
}

void VnMode::render() {
    if (script.cmd_count == 0 || !current_is_say(*this)) {
        return;
    }

    // Caja de dialogo: un rectangulo solido simple (sin atlas todavia, SPEC.md #7.1 capa
    // DialogueBox) mas el texto encima (capa DialogueText). Placeholder deliberado: el
    // arte real de UI llega con el pipeline de assets (fuera de alcance de M7).
    Sprite box{};
    box.tex   = gfx_white_texture();
    box.dst_x = 60.0f;
    box.dst_y = 800.0f;
    box.dst_w = static_cast<f32>(k_virtual_width) - 120.0f;
    box.dst_h = 220.0f;
    box.color = 0xCC1A1A1Au;
    box.layer = static_cast<u16>(GfxLayer::DialogueBox);
    gfx_draw_sprite(box);

    u32 visible = static_cast<u32>(visible_glyphs_f);
    text_draw(current_layout, 90.0f, 830.0f, visible);
}
