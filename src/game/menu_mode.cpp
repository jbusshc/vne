#include "game/menu_mode.h"

#include <cstdio>

#include <SDL3/SDL.h>

#include "audio/audio.h"
#include "gfx/gfx.h"

namespace {
constexpr f32       k_volume_step = 0.1f;
const char* const k_bus_names[4] = {"Master", "Musica", "Efectos", "Voces"};
}  // namespace

void MenuMode::update(const InputState& input, f32 dt) {
    (void)dt;
    if (input.key_pressed[SDL_SCANCODE_ESCAPE]) {
        wants_close = true;
    }
    if (input.key_pressed[SDL_SCANCODE_UP] && selected > 0) {
        selected -= 1;
    }
    if (input.key_pressed[SDL_SCANCODE_DOWN] && selected < 3) {
        selected += 1;
    }
    f32 delta = 0.0f;
    if (input.key_pressed[SDL_SCANCODE_LEFT]) {
        delta = -k_volume_step;
    }
    if (input.key_pressed[SDL_SCANCODE_RIGHT]) {
        delta = k_volume_step;
    }
    if (delta != 0.0f && state != nullptr) {
        f32 v = state->bus_volume[selected] + delta;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        state->bus_volume[selected] = v;
        audio_set_bus_volume(static_cast<Bus>(selected), v);
    }
}

void MenuMode::render() {
    Sprite panel{};
    panel.tex   = gfx_white_texture();
    panel.dst_x = 500.0f;
    panel.dst_y = 300.0f;
    panel.dst_w = 900.0f;
    panel.dst_h = 500.0f;
    panel.color = 0xF0202020u;
    panel.layer = static_cast<u16>(GfxLayer::UI);
    gfx_draw_sprite(panel);

    if (!font.valid() || state == nullptr) {
        return;
    }

    bool needs_rebuild = cached_selected != selected;
    for (u32 i = 0; i < 4 && !needs_rebuild; ++i) {
        if (cached_volumes[i] != state->bus_volume[i]) {
            needs_rebuild = true;
        }
    }
    if (needs_rebuild) {
        for (u32 i = 0; i < 4; ++i) {
            char line[64];
            std::snprintf(line, sizeof(line), "%s%s: %d%%", i == static_cast<u32>(selected) ? "> " : "  ",
                          k_bus_names[i], static_cast<i32>(state->bus_volume[i] * 100.0f));
            cached_lines[i]   = text_layout(font, line, 800.0f, scratch_arena);
            cached_volumes[i] = state->bus_volume[i];
        }
        cached_selected = selected;
    }

    f32 y = 350.0f;
    for (u32 i = 0; i < 4; ++i) {
        text_draw(cached_lines[i], 550.0f, y, cached_lines[i].count);
        y += 90.0f;
    }
}
