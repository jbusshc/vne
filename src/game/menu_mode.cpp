#include "game/menu_mode.h"

#include <cstdio>

#include <SDL3/SDL.h>

#include "audio/audio.h"
#include "gfx/gfx.h"
#include "text/catalog.h"

namespace {
constexpr f32       k_volume_step = 0.1f;
const char* const k_bus_names[4] = {"Master", "Musica", "Efectos", "Voces"};
constexpr i32       k_row_count    = 5;  // 4 buses + idioma
}  // namespace

void MenuMode::update(const InputState& input, f32 dt) {
    (void)dt;
    if (input.key_pressed[SDL_SCANCODE_ESCAPE]) {
        wants_close = true;
    }
    if (input.key_pressed[SDL_SCANCODE_UP] && selected > 0) {
        selected -= 1;
    }
    if (input.key_pressed[SDL_SCANCODE_DOWN] && selected < k_row_count - 1) {
        selected += 1;
    }

    if (selected < 4) {
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
        return;
    }

    // Fila de idioma (M10, SPEC.md #10: "cambia de espanol a japones sin reiniciar").
    if (!input.key_pressed[SDL_SCANCODE_LEFT] && !input.key_pressed[SDL_SCANCODE_RIGHT]) {
        return;
    }
    locale_index =
        (locale_index + (input.key_pressed[SDL_SCANCODE_RIGHT] ? 1 : k_locale_count - 1)) %
        static_cast<i32>(k_locale_count);

    const char* vnl_path = locale_vnl_paths[locale_index];
    if (vnl_path == nullptr) {
        catalog_clear();
    } else {
        catalog_load(vnl_path, scratch_arena);
    }
    // Fuente CJK bajo demanda (M10): la rasterizacion de glifos CJK ya existe desde M2
    // (glyph_cache la cachea al vuelo); aqui solo hace falta que VnMode dibuje con la
    // fuente correcta. locale_index==1 ("japones") es el unico que la necesita por
    // ahora — si se anaden mas idiomas CJK, esto se convertiria en una tabla por idioma
    // en vez de un unico caso especial.
    if (dialogue_font_slot != nullptr) {
        *dialogue_font_slot = (locale_index == 1 && cjk_font.valid()) ? cjk_font : latin_font;
    }
}

void MenuMode::render() {
    Sprite panel{};
    panel.tex   = gfx_white_texture();
    panel.dst_x = 500.0f;
    panel.dst_y = 260.0f;
    panel.dst_w = 900.0f;
    panel.dst_h = 560.0f;
    panel.color = 0xF0202020u;
    panel.layer = static_cast<u16>(GfxLayer::UI);
    gfx_draw_sprite(panel);

    if (!font.valid() || state == nullptr) {
        return;
    }

    bool needs_rebuild = cached_selected != selected || cached_locale != locale_index;
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
        char locale_line[96];
        std::snprintf(locale_line, sizeof(locale_line), "%sIdioma: %s",
                      selected == 4 ? "> " : "  ", locale_names[locale_index]);
        cached_lines[4]  = text_layout(font, locale_line, 800.0f, scratch_arena);
        cached_selected  = selected;
        cached_locale    = locale_index;
    }

    f32 y = 310.0f;
    for (u32 i = 0; i < 5; ++i) {
        text_draw(cached_lines[i], 550.0f, y, cached_lines[i].count);
        y += 90.0f;
    }
}
