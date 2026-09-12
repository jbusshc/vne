#include "game/backlog_mode.h"

#include <SDL3/SDL.h>

#include "render/render.h"
#include "text/catalog.h"

namespace {

void rebuild_cache_if_needed(BacklogMode* m) {
    u32 gen = catalog_generation();
    if ((m->cached_scroll == m->scroll && m->cached_locale_gen == gen) || !m->font.valid()) {
        return;
    }
    BacklogEntry ordered[k_backlog_capacity];
    backlog_get_ordered(g_backlog, ordered);

    m->cached_count = 0;
    for (u32 i = 0; i < k_backlog_visible_lines && static_cast<u32>(m->scroll) + i < g_backlog.count;
         ++i) {
        const BacklogEntry& entry = ordered[static_cast<u32>(m->scroll) + i];
        // Por key_hash contra el catalogo activo (M14): asi el backlog cambia de idioma junto
        // con el dialogo, que es un criterio de SPEC.md #12. El texto del guion queda solo
        // como red de seguridad para una entrada de un .vnsave viejo (sin key_hash) — y
        // solo vale si el guion cargado es el mismo que la escribio, que es justamente la
        // razon por la que el key_hash tenia que existir.
        const char* fallback =
            m->script != nullptr ? script_string(*m->script, entry.text_id) : "";
        const char* text = entry.key_hash != 0 ? catalog_resolve(entry.key_hash, fallback)
                                                : fallback;
        m->cached_layouts[m->cached_count] = text_layout(m->font, text, 1700.0f, m->scratch_arena);
        m->cached_count += 1;
    }
    m->cached_scroll      = m->scroll;
    m->cached_locale_gen = gen;
}

}  // namespace

UiRect backlog_close_rect() {
    return UiRect{static_cast<f32>(k_virtual_width) - 260.0f, 40.0f, 200.0f, 60.0f};
}

void BacklogMode::update(const InputState& input, f32 dt) {
    (void)dt;
    if (input.key_pressed[SDL_SCANCODE_ESCAPE] || input.key_pressed[SDL_SCANCODE_B]) {
        wants_close = true;
    }
    i32 max_scroll = g_backlog.count > k_backlog_visible_lines
                          ? static_cast<i32>(g_backlog.count - k_backlog_visible_lines)
                          : 0;
    if (input.key_pressed[SDL_SCANCODE_UP] && scroll > 0) {
        scroll -= 1;
    }
    if (input.key_pressed[SDL_SCANCODE_DOWN] && scroll < max_scroll) {
        scroll += 1;
    }

    // Raton (M15). La rueda es LA forma natural de recorrer un historial, y el boton de
    // cerrar es lo que hace que se pueda salir sin tocar el teclado.
    hovered_close = ui_hover(backlog_close_rect(), input);
    if (hovered_close && input.mouse_pressed[0]) {
        wants_close = true;
        return;
    }
    if (input.mouse_wheel_y != 0.0f) {
        // Rueda ARRIBA sube por el historial (hacia lo mas antiguo), que es al contrario que
        // el indice: scroll cuenta desde la entrada mas vieja.
        scroll -= static_cast<i32>(input.mouse_wheel_y);
        if (scroll < 0) {
            scroll = 0;
        }
        if (scroll > max_scroll) {
            scroll = max_scroll;
        }
    }
}

void BacklogMode::render() {
    Sprite panel{};
    panel.tex   = render_white_texture();
    panel.dst_x = 0.0f;
    panel.dst_y = 0.0f;
    panel.dst_w = static_cast<f32>(k_virtual_width);
    panel.dst_h = static_cast<f32>(k_virtual_height);
    panel.color = 0xE6000000u;
    panel.layer = static_cast<u16>(RenderLayer::UI);
    render_draw_sprite(panel);

    rebuild_cache_if_needed(this);

    f32 y = 150.0f;
    for (u32 i = 0; i < cached_count; ++i) {
        text_draw(cached_layouts[i], 100.0f, y, cached_layouts[i].count);
        y += 100.0f;
    }

    if (!font.valid()) {
        return;
    }
    if (!close_label_built) {
        close_label       = text_layout(font, "Cerrar", 200.0f, scratch_arena);
        close_label_built = true;
    }
    UiRect close = backlog_close_rect();
    ui_draw_button(close, hovered_close ? k_ui_button_hover : k_ui_button_idle);
    text_draw(close_label, close.x + 16.0f, close.y + 8.0f, close_label.count);
}
