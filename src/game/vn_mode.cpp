#include "game/vn_mode.h"

#include <SDL3/SDL.h>

#include "core/log.h"
#include "render/atlas.h"
#include "render/render.h"
#include "text/catalog.h"
#include "vm/script_load.h"
#include "vm/symbols_load.h"

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
    u32 gen = catalog_generation();
    if (!current_is_say(*vn) ||
        (vn->layout_pc == vn->state->vm.pc && vn->layout_locale_gen == gen)) {
        return;
    }
    const Cmd& cmd  = vn->script.cmds[vn->state->vm.pc];
    // Catalogo primero, texto base del guion como respaldo (M10, SPEC.md #10: "todos
    // los textos vienen del catalogo"; nunca cadena vacia si falta la traduccion).
    const char* base_text = script_string(vn->script, cmd.say.text_id);
    const char* text       = catalog_resolve(cmd.say.key_hash, base_text);
    // text_layout es la funcion cara de este modulo (skill vne-rendering): solo se llama
    // aqui, cuando la linea de dialogo cambia (o cambia el idioma activo), nunca por
    // frame sin mas.
    vn->current_layout     = text_layout(vn->font, text, 1700.0f, vn->layout_arena,
                                          0xFFFFFFFFu, vn->bold_font);
    vn->layout_pc           = vn->state->vm.pc;
    vn->layout_locale_gen = gen;
    vn->visible_glyphs_f   = 0.0f;
    // Empezar de cero la temporizacion de {w=}/{speed=}: es propia de esta linea.
    vn->next_event        = 0;
    vn->pause_timer       = 0.0f;
    vn->typewriter_speed = 1.0f;
}

// Avanza el efecto de maquina de escribir aplicando los TypewriterEvent de la linea
// (M12). Devuelve el nuevo valor de visible_glyphs_f.
//
// El bucle consume TODOS los eventos que ya se hayan alcanzado antes de avanzar: varios
// pueden caer en el mismo indice de glifo (p. ej. "{speed=2}{w=0.5}texto"), y una pausa
// que empieza no debe tragarse el evento siguiente.
void advance_typewriter(VnMode* vn, f32 dt) {
    const TextLayout& l = vn->current_layout;

    // Una pausa en curso congela la revelacion, pero sigue consumiendo tiempo.
    if (vn->pause_timer > 0.0f) {
        vn->pause_timer -= dt;
        if (vn->pause_timer > 0.0f) {
            return;
        }
        // Sobra tiempo de este frame: se usa para revelar, no se tira.
        dt              = -vn->pause_timer;
        vn->pause_timer = 0.0f;
    }

    while (vn->next_event < l.event_count &&
           l.events[vn->next_event].glyph_index <= static_cast<u32>(vn->visible_glyphs_f)) {
        const TypewriterEvent& ev = l.events[vn->next_event];
        vn->next_event += 1;
        vn->typewriter_speed = ev.speed_multiplier;
        if (ev.pause_seconds > 0.0f) {
            vn->pause_timer = ev.pause_seconds - dt;
            if (vn->pause_timer > 0.0f) {
                return;  // la pausa se come el resto del frame
            }
            dt              = -vn->pause_timer;
            vn->pause_timer = 0.0f;
        }
    }

    vn->visible_glyphs_f += k_typewriter_glyphs_per_second * vn->typewriter_speed * dt;
}

}  // namespace

const char* vn_button_label(VnButton b) {
    switch (b) {
        case VnButton::Backlog:         return "Historial";
        case VnButton::Save:            return "Guardar";
        case VnButton::Load:            return "Cargar";
        case VnButton::Menu:            return "Menu";
        case VnButton::Auto:            return "Auto";
        case VnButton::Skip:            return "Saltar";
        case VnButton::RollbackBack:    return "<<";
        case VnButton::RollbackForward: return ">>";
        case VnButton::Count:           break;
    }
    return "?";
}

UiRect vn_dialogue_box_rect() {
    return UiRect{60.0f, 800.0f, static_cast<f32>(k_virtual_width) - 120.0f, 220.0f};
}

UiRect vn_button_rect(VnButton b) {
    constexpr f32 k_w   = 150.0f;
    constexpr f32 k_h   = 52.0f;
    constexpr f32 k_gap = 8.0f;
    // Fila justo encima del cuadro de dialogo, alineada a su borde izquierdo. Ocho botones
    // ocupan 8*158-8 = 1256 de los 1800 del cuadro, asi que no hay riesgo de salirse.
    UiRect box = vn_dialogue_box_rect();
    return UiRect{box.x + static_cast<f32>(b) * (k_w + k_gap), box.y - k_h - 12.0f, k_w, k_h};
}

UiRect vn_choice_rect(u32 option_index, u32 option_count) {
    constexpr f32 k_h   = 84.0f;
    constexpr f32 k_gap = 16.0f;
    constexpr f32 k_w   = 1100.0f;
    // Bloque centrado vertical y horizontalmente: las opciones son LA decision del momento,
    // no un adorno en un borde.
    f32 total = static_cast<f32>(option_count) * k_h +
                static_cast<f32>(option_count > 0 ? option_count - 1 : 0) * k_gap;
    f32 top   = (static_cast<f32>(k_virtual_height) - total) * 0.5f;
    return UiRect{(static_cast<f32>(k_virtual_width) - k_w) * 0.5f,
                  top + static_cast<f32>(option_index) * (k_h + k_gap), k_w, k_h};
}

namespace {

bool current_is_choice(const VnMode& vn) {
    return vn.state->vm.pc < vn.script.cmd_count &&
           vn.script.cmds[vn.state->vm.pc].kind == CmdKind::Choice;
}

// Layouts de las opciones del @choice en curso, con las mismas reglas que
// rebuild_layout_if_needed: se reconstruyen al cambiar el pc o el idioma, nunca por frame
// (skill vne-rendering).
void rebuild_choice_layouts_if_needed(VnMode* vn) {
    if (!current_is_choice(*vn)) {
        vn->choice_count      = 0;
        vn->choice_layout_pc = 0xFFFFFFFFu;
        return;
    }
    u32 gen = catalog_generation();
    if (vn->choice_layout_pc == vn->state->vm.pc && vn->choice_layout_locale == gen) {
        return;
    }

    const Cmd& cmd = vn->script.cmds[vn->state->vm.pc];
    // El compilador ya rechaza un @choice con mas de k_max_choice_options (M15), asi que
    // este min es una red de seguridad ante un .vnc de otra epoca, no el limite de verdad.
    u32 count = cmd.choice.option_count < k_max_choice_options
                     ? cmd.choice.option_count
                     : k_max_choice_options;
    for (u32 i = 0; i < count; ++i) {
        const ChoiceOption& opt = vn->script.choice_options[cmd.choice.first_option + i];
        // Por catalogo igual que el dialogo (M10): las opciones son texto de juego, no de
        // interfaz, y tienen su propio key_hash desde M10 precisamente para esto.
        const char* base = script_string(vn->script, opt.text_id);
        const char* text = catalog_resolve(opt.key_hash, base);
        vn->choice_layouts[i] =
            text_layout(vn->font, text, vn_choice_rect(i, count).w - 60.0f, vn->layout_arena,
                         0xFFFFFFFFu, vn->bold_font);
    }
    vn->choice_count          = count;
    vn->choice_layout_pc     = vn->state->vm.pc;
    vn->choice_layout_locale = gen;

    // Arranca en la primera opcion DISPONIBLE, no en la primera sin mas: si la 0 tiene una
    // condicion que no se cumple, dejar el cursor ahi ofreceria algo que al confirmar no
    // hace nada.
    vn->choice_selected = 0;
    for (u32 i = 0; i < count; ++i) {
        if (vm_choice_option_available(*vn->state, vn->script, static_cast<u8>(i))) {
            vn->choice_selected = static_cast<i32>(i);
            break;
        }
    }
}

// Mueve la seleccion saltandose las opciones cuya condicion no se cumple. El bucle da como
// mucho una vuelta completa, asi que con todas las opciones bloqueadas no se cuelga: se
// queda donde estaba.
void move_choice_selection(VnMode* vn, i32 delta) {
    i32 n = static_cast<i32>(vn->choice_count);
    if (n <= 0) {
        return;
    }
    for (i32 step = 1; step <= n; ++step) {
        i32 candidate = ((vn->choice_selected + delta * step) % n + n) % n;
        if (vm_choice_option_available(*vn->state, vn->script, static_cast<u8>(candidate))) {
            vn->choice_selected = candidate;
            return;
        }
    }
}

// Botonera y gestos de raton. Devuelve true si el clic de este frame lo consumio algo de la
// UI: sin eso, pinchar "Guardar" abriria el panel Y avanzaria el dialogo por debajo.
bool handle_mouse_ui(VnMode* vn, const InputState& input) {
    vn->hovered_button = -1;
    for (u32 i = 0; i < k_vn_button_count; ++i) {
        if (ui_hover(vn_button_rect(static_cast<VnButton>(i)), input)) {
            vn->hovered_button = static_cast<i32>(i);
        }
    }

    if (input.mouse_pressed[0] && vn->hovered_button >= 0) {
        switch (static_cast<VnButton>(vn->hovered_button)) {
            case VnButton::Backlog: vn->ui_request = VnUiRequest::Backlog; break;
            case VnButton::Save:    vn->ui_request = VnUiRequest::Save; break;
            case VnButton::Load:    vn->ui_request = VnUiRequest::Load; break;
            case VnButton::Menu:    vn->ui_request = VnUiRequest::Menu; break;
            case VnButton::Auto:    vn->auto_mode = !vn->auto_mode; break;
            case VnButton::Skip:    vn->skip_mode = !vn->skip_mode; break;
            case VnButton::RollbackBack:
                vn->ui_request = VnUiRequest::RollbackBack;
                break;
            case VnButton::RollbackForward:
                vn->ui_request = VnUiRequest::RollbackForward;
                break;
            case VnButton::Count: break;
        }
        return true;
    }

    // Las dos convenciones de raton que tiene cualquier novela visual, y que evitan que
    // jugar solo con raton dependa de acertar un boton de 150x52: rueda arriba abre el
    // historial, clic derecho abre el menu.
    if (input.mouse_wheel_y > 0.0f) {
        vn->ui_request = VnUiRequest::Backlog;
        return true;
    }
    if (input.mouse_pressed[2]) {
        vn->ui_request = VnUiRequest::Menu;
        return true;
    }
    return false;
}

// Elegir una opcion con teclado o con raton. click_consumed viene de handle_mouse_ui: un
// clic que ya pulso un boton no debe elegir tambien una opcion.
void handle_choice_input(VnMode* vn, const InputState& input, bool click_consumed) {
    if (input.key_pressed[SDL_SCANCODE_UP]) {
        move_choice_selection(vn, -1);
    }
    if (input.key_pressed[SDL_SCANCODE_DOWN]) {
        move_choice_selection(vn, 1);
    }

    // El hover mueve la MISMA seleccion que las flechas, en vez de pintar dos cursores
    // distintos: el jugador ve siempre un unico "esto es lo que vas a elegir".
    for (u32 i = 0; i < vn->choice_count; ++i) {
        if (ui_hover(vn_choice_rect(i, vn->choice_count), input) &&
            vm_choice_option_available(*vn->state, vn->script, static_cast<u8>(i))) {
            vn->choice_selected = static_cast<i32>(i);
        }
    }

    bool pick = confirm_pressed(input);
    if (!click_consumed && input.mouse_pressed[0]) {
        for (u32 i = 0; i < vn->choice_count; ++i) {
            if (ui_hover(vn_choice_rect(i, vn->choice_count), input)) {
                pick = true;
                break;
            }
        }
    }
    // Atajo por numero, como cualquier novela visual. SDL_SCANCODE_1..9 son consecutivos.
    for (u32 i = 0; i < vn->choice_count; ++i) {
        if (input.key_pressed[SDL_SCANCODE_1 + i]) {
            vn->choice_selected = static_cast<i32>(i);
            pick                = true;
        }
    }

    if (pick) {
        // Devuelve false si la opcion tiene una condicion que no se cumple; entonces no
        // pasa nada y el jugador sigue eligiendo, que es el comportamiento correcto.
        vm_select_choice(&vn->state->vm, vn->state, vn->script,
                          static_cast<u8>(vn->choice_selected));
    }
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

    // Raton (M15). Va antes que todo lo demas para que un clic sobre un boton no cuente
    // ademas como "avanzar el dialogo" ni como "elegir una opcion".
    bool click_consumed = handle_mouse_ui(this, input);

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
        // Completar la linea de golpe cancela cualquier {w=n} en curso: si no, el texto
        // ya estaria entero en pantalla pero el avance seguiria bloqueado esperando una
        // pausa que ya no tiene sentido.
        pause_timer = 0.0f;
        return;
    }

    rebuild_layout_if_needed(this);
    rebuild_choice_layouts_if_needed(this);

    // Un Choice no se completa con el paso del tiempo (vm_update devuelve false para
    // siempre): lo resuelve vm_select_choice, y hasta M15 nadie lo llamaba desde el juego.
    if (choice_count > 0) {
        handle_choice_input(this, input, click_consumed);
        if (vm_update(&state->vm, state, script, dt)) {
            finished = true;
        }
        return;
    }

    bool waiting_on_say = current_is_say(*this) && state->vm.waiting_for_input != 0;
    bool typewriter_done = visible_glyphs_f >= static_cast<f32>(current_layout.count);
    // Un clic izquierdo que no consumio la UI avanza el dialogo, igual que espacio o
    // intro: es LA interaccion de una novela visual con raton.
    bool confirm = confirm_pressed(input) || (input.mouse_pressed[0] && !click_consumed);

    if (waiting_on_say) {
        if (!typewriter_done) {
            advance_typewriter(this, dt);
            // El primer confirmar mientras el efecto de maquina de escribir esta en
            // marcha lo completa al instante en vez de avanzar de linea (convencion
            // estandar de novela visual): igual que skip_to_end pero solo para el
            // texto, no para el resto del comando.
            if (confirm) {
                visible_glyphs_f = static_cast<f32>(current_layout.count);
                // Completar la linea de golpe cancela cualquier {w=n} en curso: si no, el
                // texto ya estaria entero en pantalla pero el avance seguiria bloqueado
                // esperando una pausa que ya no tiene sentido.
                pause_timer = 0.0f;
            }
        } else if (auto_mode) {
            auto_hold_timer += dt;
            if (auto_hold_timer >= vn_auto_hold_seconds(current_layout.count)) {
                auto_hold_timer = 0.0f;
                vm_confirm_say(state);
            }
        } else if (confirm) {
            vm_confirm_say(state);
        }
    }

    if (vm_update(&state->vm, state, script, dt)) {
        finished = true;
    }
}

namespace {

// La capa gfx no puede incluir formats/cmd.h (SPEC.md #5: gfx esta por debajo de vm), asi que
// la traduccion entre los dos enums vive aqui, en la capa que ve a ambos.
RenderTransitionMask to_gfx_mask(TransitionKind kind) {
    switch (kind) {
        case TransitionKind::Fade:     return RenderTransitionMask::Fade;
        case TransitionKind::Wipe:     return RenderTransitionMask::Wipe;
        case TransitionKind::Dissolve: return RenderTransitionMask::Dissolve;
    }
    return RenderTransitionMask::Fade;
}

}  // namespace


namespace {

// Concatena en un buffer fijo sin asignar y sin pasar por snprintf. Trunca en silencio si
// no cabe; los nombres logicos reales son de unos pocos bytes.
void append_cstr(char* dst, usize cap, usize* len, const char* src) {
    while (*src != '\0' && *len + 1 < cap) {
        dst[(*len)++] = *src++;
    }
    dst[*len] = '\0';
}

// Dibuja el fondo y los actores que hay en GameState (M13).
//
// Hasta M13 esto no existia: @bg, @show, @hide y @move mantenian bg_id y actors[] pero
// NADIE los convertia en sprites, asi que una escena de novela visual solo pintaba el
// cuadro de dialogo. El registro de nombres del atlas (render/atlas.h) y las tablas de
// nombres del .vnc v5 son lo que hacia falta para poder resolver un actor_id a un sprite.
void draw_scene(const VnMode& vn) {
    const GameState& state = *vn.state;

    // Fondo, escalado a la resolucion virtual completa. Los placeholders son de 256x144,
    // asi que se ven deliberadamente toscos al estirarlos (ver sz_bake placeholders).
    const char* bg_name = symbols_name(SymKind::Bg, state.bg_id);
    if (bg_name[0] != '\0') {
        char  name[96];
        usize len = 0;
        name[0]   = '\0';
        append_cstr(name, sizeof(name), &len, "bg_");
        append_cstr(name, sizeof(name), &len, bg_name);

        AtlasSprite rect{};
        if (atlas_find(name, &rect)) {
            Sprite s{};
            s.tex   = vn.atlas_tex;
            s.src_x = static_cast<f32>(rect.x);
            s.src_y = static_cast<f32>(rect.y);
            s.src_w = static_cast<f32>(rect.w);
            s.src_h = static_cast<f32>(rect.h);
            s.dst_x = 0.0f;
            s.dst_y = 0.0f;
            s.dst_w = static_cast<f32>(k_virtual_width);
            s.dst_h = static_cast<f32>(k_virtual_height);
            s.layer = static_cast<u16>(RenderLayer::Background);
            render_draw_sprite(s);
        }
    }

    // Actores. x/y son normalizados 0..1 (SPEC.md #9.1) y marcan el punto de APOYO: el
    // sprite se centra horizontalmente ahi y se apoya con su base en esa altura, que es lo
    // que hace que cambiar de pose a otra de distinto alto no haga saltar al personaje.
    for (u32 i = 0; i < k_max_actor_slots; ++i) {
        const ActorSlot& a = state.actors[i];
        if (a.actor_id == 0 || a.alpha <= 0.0f) {
            continue;
        }
        char  name[96];
        usize len = 0;
        name[0]   = '\0';
        append_cstr(name, sizeof(name), &len, "actor_");
        append_cstr(name, sizeof(name), &len, symbols_name(SymKind::Actor, a.actor_id));
        append_cstr(name, sizeof(name), &len, "_");
        append_cstr(name, sizeof(name), &len, symbols_name(SymKind::Pose, a.pose_id));

        AtlasSprite rect{};
        if (!atlas_find(name, &rect)) {
            continue;  // sin sprite no se dibuja nada; nunca es fatal (SPEC.md #4)
        }

        // Una partida guardada antes de que existiera `scale` (o un GameState puesto a cero
        // a mano) trae scale = 0, que dibujaria un sprite de area nula e invisible sin una
        // sola pista de por que.
        f32 scale = a.scale > 0.0f ? a.scale : 1.0f;
        f32 w     = static_cast<f32>(rect.w) * scale;
        f32 h     = static_cast<f32>(rect.h) * scale;

        Sprite s{};
        s.tex   = vn.atlas_tex;
        s.src_x = static_cast<f32>(rect.x);
        s.src_y = static_cast<f32>(rect.y);
        s.src_w = static_cast<f32>(rect.w);
        s.src_h = static_cast<f32>(rect.h);
        s.dst_x = a.x * static_cast<f32>(k_virtual_width) - w * 0.5f;
        s.dst_y = a.y * static_cast<f32>(k_virtual_height) - h;
        s.dst_w = w;
        s.dst_h = h;
        // El alpha va en el canal alto del color, premultiplicado como pide render.h: el
        // fundido de @show/@hide ya lo mantiene vm.cpp en ActorSlot.alpha.
        u32 alpha_byte = static_cast<u32>(a.alpha * 255.0f) & 0xFFu;
        s.color        = (alpha_byte << 24) | (alpha_byte << 16) | (alpha_byte << 8) | alpha_byte;
        s.layer        = static_cast<u16>(RenderLayer::Actors);
        s.order        = static_cast<u16>(i);
        render_draw_sprite(s);
    }
}

}  // namespace

void VnMode::render() {
    if (script.cmd_count == 0) {
        return;
    }

    // Fondo y actores antes que nada: son las capas de abajo (Background/Actors) y el
    // cuadro de dialogo va encima. Va antes del early-return de "no es un Say" porque la
    // escena sigue ahi durante un @wait o una transicion, no solo mientras alguien habla.
    draw_scene(*this);

    // Transicion en curso (M12): se lee del comando actual y de vm.cmd_timer, sin ningun
    // campo nuevo en GameState (ver cmd_start en vm.cpp). Va antes del early-return de
    // abajo porque una transicion no es un Say y tiene que dibujarse igual.
    if (state->vm.pc < script.cmd_count) {
        const Cmd& cur = script.cmds[state->vm.pc];
        if (cur.kind == CmdKind::Transition && cur.transition.seconds > 0.0f) {
            f32 threshold = state->vm.cmd_timer / cur.transition.seconds;
            if (threshold > 1.0f) {
                threshold = 1.0f;
            }
            // Negro opaco premultiplicado: el shader lo multiplica por el alpha que
            // calcula, asi que aqui va el tinte a plena intensidad.
            render_draw_transition(to_gfx_mask(cur.transition.transition_kind), threshold,
                                 0xFF000000u);
        }
    }

    // Opciones del @choice (M15). Van antes del early-return de "no es un Say" porque un
    // Choice no es un Say y hasta ahora eso significaba que no se dibujaba nada en absoluto.
    if (choice_count > 0) {
        for (u32 i = 0; i < choice_count; ++i) {
            UiRect r         = vn_choice_rect(i, choice_count);
            bool   available = vm_choice_option_available(*state, script, static_cast<u8>(i));
            u32    color     = k_ui_button_idle;
            if (!available) {
                // Se DIBUJA apagada en vez de esconderse: que una opcion exista pero no
                // este disponible es informacion de juego (SPEC.md #9.1), y ocultarla haria
                // que el menu cambiara de tamano segun el estado de las variables.
                color = 0x60202020u;
            } else if (static_cast<i32>(i) == choice_selected) {
                color = k_ui_button_hover;
            }
            ui_draw_button(r, color);
            if (font.valid()) {
                text_draw(choice_layouts[i], r.x + 30.0f, r.y + 18.0f, choice_layouts[i].count);
            }
        }
        return;
    }

    if (!current_is_say(*this)) {
        return;
    }

    // Caja de dialogo: un rectangulo solido simple (sin atlas todavia, SPEC.md #7.1 capa
    // DialogueBox) mas el texto encima (capa DialogueText). Placeholder deliberado: el
    // arte real de UI llega con el pipeline de assets (fuera de alcance de M7).
    // Arte real (Kenney UI Pack, CC0) estirado en nueve trozos, no un rectangulo solido:
    // hasta M15 el cuadro de dialogo era literalmente render_white_texture() tintada.
    ui_draw_nine_slice(vn_dialogue_box_rect(), "button_rectangle_border", 0xE0303030u,
                        RenderLayer::DialogueBox);

    u32 visible = static_cast<u32>(visible_glyphs_f);
    text_draw(current_layout, 90.0f, 830.0f, visible);

    // Botonera de raton (M15). Las etiquetas son texto de interfaz fijo, asi que se
    // maquetan una sola vez y no se rehacen al cambiar de idioma (ver "Pendientes
    // observados" en docs/DECISIONS.md).
    if (font.valid() && !button_labels_built) {
        for (u32 i = 0; i < k_vn_button_count; ++i) {
            button_labels[i] = text_layout(font, vn_button_label(static_cast<VnButton>(i)),
                                            200.0f, layout_arena);
        }
        button_labels_built = true;
    }
    for (u32 i = 0; i < k_vn_button_count; ++i) {
        VnButton b = static_cast<VnButton>(i);
        UiRect   r = vn_button_rect(b);
        // Un modo activo (auto o saltar) se pinta encendido aunque el cursor no este
        // encima: sin eso no habria forma de saber si estan puestos.
        bool on = (b == VnButton::Auto && auto_mode) || (b == VnButton::Skip && skip_mode);
        u32  color = on ? k_ui_button_active
                        : (hovered_button == static_cast<i32>(i) ? k_ui_button_hover
                                                                  : k_ui_button_idle);
        ui_draw_button(r, color);
        if (button_labels_built) {
            text_draw(button_labels[i], r.x + 12.0f, r.y + 6.0f, button_labels[i].count);
        }
    }
}
