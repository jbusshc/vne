#include "game/backlog_mode.h"

#include <SDL3/SDL.h>

#include "gfx/gfx.h"

namespace {

void rebuild_cache_if_needed(BacklogMode* m) {
    if (m->cached_scroll == m->scroll || !m->font.valid()) {
        return;
    }
    BacklogEntry ordered[k_backlog_capacity];
    backlog_get_ordered(g_backlog, ordered);

    m->cached_count = 0;
    for (u32 i = 0; i < k_backlog_visible_lines && static_cast<u32>(m->scroll) + i < g_backlog.count;
         ++i) {
        const BacklogEntry& entry = ordered[static_cast<u32>(m->scroll) + i];
        const char*          text  = m->script != nullptr ? script_string(*m->script, entry.text_id)
                                                            : "";
        m->cached_layouts[m->cached_count] = text_layout(m->font, text, 1700.0f, m->scratch_arena);
        m->cached_count += 1;
    }
    m->cached_scroll = m->scroll;
}

}  // namespace

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
}

void BacklogMode::render() {
    Sprite panel{};
    panel.tex   = gfx_white_texture();
    panel.dst_x = 0.0f;
    panel.dst_y = 0.0f;
    panel.dst_w = static_cast<f32>(k_virtual_width);
    panel.dst_h = static_cast<f32>(k_virtual_height);
    panel.color = 0xE6000000u;
    panel.layer = static_cast<u16>(GfxLayer::UI);
    gfx_draw_sprite(panel);

    rebuild_cache_if_needed(this);

    f32 y = 100.0f;
    for (u32 i = 0; i < cached_count; ++i) {
        text_draw(cached_layouts[i], 100.0f, y, cached_layouts[i].count);
        y += 100.0f;
    }
}
