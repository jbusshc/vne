#include "game/menu_mode.h"

#include <cstdio>

#include <SDL3/SDL.h>

#include "audio/audio.h"
#include "render/render.h"
#include "game/config.h"
#include "game/locales.h"
#include "text/catalog.h"

namespace {
constexpr f32       k_volume_step = 0.1f;
const char* const k_bus_names[4] = {"Master", "Musica", "Efectos", "Voces"};
constexpr i32       k_row_count    = k_menu_row_count;

// Paso al que se redondea un volumen puesto con el raton. Sin redondeo, arrastrar daria
// 0.43871 y la fila mostraria "43%" de un valor que no se puede reproducir con el teclado;
// con 0.05 las dos formas de moverlo llegan a los mismos sitios.
constexpr f32 k_volume_snap = 0.05f;

f32 clamp01(f32 v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}
}  // namespace

UiRect menu_row_rect(i32 row) {
    return UiRect{540.0f, 300.0f + static_cast<f32>(row) * 90.0f, 820.0f, 74.0f};
}

UiRect menu_slider_rect(i32 bus) {
    UiRect row = menu_row_rect(bus);
    // Carril a la derecha de la etiqueta, centrado en la fila.
    return UiRect{row.x + 440.0f, row.y + row.h * 0.5f - 14.0f, 340.0f, 28.0f};
}

UiRect menu_close_rect() {
    return UiRect{540.0f, 300.0f + static_cast<f32>(k_menu_row_count) * 90.0f, 200.0f, 60.0f};
}

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

    // ---- Raton (M15) ----
    hovered_row = -1;
    for (i32 row = 0; row < k_row_count; ++row) {
        if (ui_hover(menu_row_rect(row), input)) {
            hovered_row = row;
        }
    }
    hovered_close = ui_hover(menu_close_rect(), input);
    if (hovered_close && input.mouse_pressed[0]) {
        wants_close = true;
        return;
    }
    // Pinchar una fila la selecciona, para que el teclado siga desde donde esta el raton en
    // vez de tener dos cursores distintos.
    if (input.mouse_pressed[0] && hovered_row >= 0) {
        selected = hovered_row;
    }

    if (state != nullptr) {
        if (dragging_bus < 0 && input.mouse_pressed[0]) {
            for (i32 bus = 0; bus < 4; ++bus) {
                if (ui_hover(menu_slider_rect(bus), input)) {
                    dragging_bus = bus;
                    break;
                }
            }
        }
        if (dragging_bus >= 0) {
            if (input.mouse_down[0]) {
                // La posicion del cursor a lo largo del carril ES el valor: arrastrar y
                // pinchar en un punto concreto son la misma operacion, sin casos especiales.
                UiRect track = menu_slider_rect(dragging_bus);
                f32    t     = clamp01((input.mouse_x - track.x) / track.w);
                f32    snapped =
                    clamp01(static_cast<f32>(static_cast<i32>(t / k_volume_snap + 0.5f)) *
                            k_volume_snap);
                state->bus_volume[dragging_bus]  = snapped;
                g_config.bus_volume[dragging_bus] = snapped;
                audio_set_bus_volume(static_cast<Bus>(dragging_bus), snapped);
            } else {
                // Soltar: aqui y solo aqui se escribe config.ini (ver dragging_bus en la
                // cabecera).
                dragging_bus = -1;
                if (persist_config) {
                    config_save();
                }
            }
            return;
        }
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
            f32 v                       = clamp01(state->bus_volume[selected] + delta);
            state->bus_volume[selected] = v;
            audio_set_bus_volume(static_cast<Bus>(selected), v);
            // Igual que el idioma: se persiste al cambiar (M14).
            g_config.bus_volume[selected] = v;
            if (persist_config) {
                config_save();
            }
        }
        return;
    }

    // Fila de idioma (M10, SPEC.md #10: "cambia de espanol a japones sin reiniciar"). Con
    // el raton se cicla pinchandola, que es lo que hace una fila de un solo ajuste.
    bool next_locale =
        input.key_pressed[SDL_SCANCODE_RIGHT] ||
        (input.mouse_pressed[0] && hovered_row == 4);
    bool prev_locale = input.key_pressed[SDL_SCANCODE_LEFT];
    if (!next_locale && !prev_locale) {
        return;
    }
    locale_index = (locale_index + (next_locale ? 1 : k_locale_count - 1)) %
                   static_cast<i32>(k_locale_count);

    apply_locale();

    // Se persiste AL CAMBIAR, no al salir: si el juego se cierra de forma anormal, la
    // preferencia ya esta guardada (M14, criterio de SPEC.md #12).
    g_config.locale_index = static_cast<u32>(locale_index);
    if (persist_config) {
        config_save();
    }
}

void MenuMode::apply_locale() {
    const LocaleDesc& locale = k_locales[locale_index];

    if (locale.vnl == nullptr) {
        catalog_clear();  // idioma base: el texto del guion tal cual
    } else {
        catalog_load(locale.vnl, catalog_arena != nullptr ? catalog_arena : scratch_arena);
    }

    // Que fuente usar sale de la TABLA (M14), no de comparar el indice con 1. El caso
    // especial de M10 asumia que cualquier idioma que no fuera espanol era japones: con un
    // tercer idioma latino habria cargado la fuente CJK en silencio.
    if (bold_font_slot != nullptr) {
        *bold_font_slot =
            (locale.needs_cjk && cjk_bold_font.valid()) ? cjk_bold_font : latin_bold_font;
    }
    if (dialogue_font_slot != nullptr) {
        *dialogue_font_slot = (locale.needs_cjk && cjk_font.valid()) ? cjk_font : latin_font;
    }
}

void MenuMode::render() {
    Sprite panel{};
    panel.tex   = render_white_texture();
    panel.dst_x = 500.0f;
    panel.dst_y = 260.0f;
    panel.dst_w = 900.0f;
    panel.dst_h = 560.0f;
    panel.color = 0xF0202020u;
    panel.layer = static_cast<u16>(RenderLayer::UI);
    render_draw_sprite(panel);

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
                      selected == 4 ? "> " : "  ", k_locales[locale_index].display);
        cached_lines[4]  = text_layout(font, locale_line, 800.0f, scratch_arena);
        cached_selected  = selected;
        cached_locale    = locale_index;
    }

    for (i32 i = 0; i < k_menu_row_count; ++i) {
        UiRect row = menu_row_rect(i);
        if (i == selected) {
            ui_draw_button(row, k_ui_row_selected);
        } else if (i == hovered_row) {
            ui_draw_button(row, k_ui_button_idle);
        }
        text_draw(cached_lines[i], row.x + 10.0f, row.y + 8.0f, cached_lines[i].count);

        if (i < 4) {
            // Carril y relleno. El relleno es el valor, asi que lo que se ve y lo que se
            // arrastra son literalmente el mismo rectangulo (ver menu_slider_rect).
            ui_draw_slider(menu_slider_rect(i), state->bus_volume[i]);
        }
    }

    if (!close_label_built) {
        close_label       = text_layout(font, "Cerrar", 200.0f, scratch_arena);
        close_label_built = true;
    }
    UiRect close = menu_close_rect();
    ui_draw_button(close, hovered_close ? k_ui_button_hover : k_ui_button_idle);
    text_draw(close_label, close.x + 16.0f, close.y + 8.0f, close_label.count);
}
