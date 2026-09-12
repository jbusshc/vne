#include "game/save_load_mode.h"

#include <cstdio>
#include <cstring>

#include <SDL3/SDL.h>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4267)
#endif
#include <qoi.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "core/heap_guard.h"
#include "render/render.h"
#include "render/texture.h"

const char* save_slot_path(u32 slot_index) {
    static char paths[k_save_slot_count][64];
    std::snprintf(paths[slot_index], sizeof(paths[slot_index]),
                  "assets_baked/save_slot_%u.vnsave", slot_index);
    return paths[slot_index];
}

namespace {

// Cota generosa para un QOI de 384x216: en la practica pesa unos pocos KB (ver
// k_thumbnail_max_bytes en vm/save.h).
u8 g_thumbnail_scratch[k_thumbnail_max_bytes];

void load_slot_thumbnail(SaveLoadMode* m, u32 i) {
    u32 size = 0;
    if (load_save_thumbnail(save_slot_path(i), g_thumbnail_scratch, k_thumbnail_max_bytes,
                             &size) != LoadResult::Ok ||
        size == 0) {
        m->slot_has_data[i] = false;
        return;
    }

    qoi_desc desc{};
    void*    pixels = qoi_decode(g_thumbnail_scratch, static_cast<int>(size), &desc, 4);
    if (pixels == nullptr) {
        m->slot_has_data[i] = false;
        return;
    }

    if (!m->slot_thumbnail[i].valid()) {
        m->slot_thumbnail[i] =
            texture_create_dynamic(static_cast<i32>(desc.width), static_cast<i32>(desc.height));
    }
    texture_update_dynamic(m->slot_thumbnail[i], static_cast<const u8*>(pixels));
    std::free(pixels);
    m->slot_has_data[i] = true;
}

}  // namespace

void SaveLoadMode::on_enter() {
    // Misma excepcion que al guardar (ver update): abrir el panel decodifica la miniatura de
    // cada hueco con qoi, que asigna. Es una operacion puntual al abrir un menu, no algo que
    // pase en cada frame.
    heap_guard_suspend();
    for (u32 i = 0; i < k_save_slot_count; ++i) {
        load_slot_thumbnail(this, i);
    }
    heap_guard_resume();
    labels_built = false;
}

UiRect save_slot_rect(u32 slot_index) {
    // Misma geometria que render(): miniatura de 192x108 a la izquierda y etiqueta a la
    // derecha, con el paso vertical de la miniatura mas 30 de aire.
    f32 step = static_cast<f32>(k_thumbnail_height) * 0.5f + 30.0f;
    return UiRect{90.0f, 114.0f + static_cast<f32>(slot_index) * step, 900.0f, step - 8.0f};
}

UiRect save_close_rect() {
    return UiRect{static_cast<f32>(k_virtual_width) - 260.0f, 40.0f, 200.0f, 60.0f};
}

void SaveLoadMode::update(const InputState& input, f32 dt) {
    (void)dt;
    if (input.key_pressed[SDL_SCANCODE_ESCAPE]) {
        wants_close = true;
        return;
    }
    if (input.key_pressed[SDL_SCANCODE_UP] && selected > 0) {
        selected -= 1;
    }
    if (input.key_pressed[SDL_SCANCODE_DOWN] &&
        selected < static_cast<i32>(k_save_slot_count) - 1) {
        selected += 1;
    }

    // Raton (M15).
    hovered_close = ui_hover(save_close_rect(), input);
    if (hovered_close && input.mouse_pressed[0]) {
        wants_close = true;
        return;
    }
    hovered_slot = -1;
    for (u32 i = 0; i < k_save_slot_count; ++i) {
        if (ui_hover(save_slot_rect(i), input)) {
            hovered_slot = static_cast<i32>(i);
        }
    }
    // Un clic en un hueco lo elige Y ejecuta la accion, igual que INTRO sobre el hueco
    // elegido. Un solo gesto y no dos: quien abre este panel ya decidio si viene a guardar o
    // a cargar (is_save), asi que pedir una confirmacion aparte solo anadiria un clic.
    bool clicked_slot = input.mouse_pressed[0] && hovered_slot >= 0;
    if (clicked_slot) {
        selected = hovered_slot;
    }

    if (!clicked_slot && !input.key_pressed[SDL_SCANCODE_RETURN] &&
        !input.key_pressed[SDL_SCANCODE_SPACE]) {
        return;
    }

    const char* path = save_slot_path(static_cast<u32>(selected));

    // Guardar y cargar salen del presupuesto de cero heap por frame (SPEC.md #4), misma
    // familia que ADR-0035: codigo de terceros que asigna al codificar/decodificar un asset
    // durante una operacion pesada, puntual y pedida por el jugador. Medido en M15 al
    // reproducir la sesion grabada: qoi_encode de la miniatura hace 2 asignaciones.
    //
    // Nadie lo habia visto hasta ahora porque ningun test habia pulsado F5 DENTRO del bucle
    // de frame — es exactamente el hueco que la grabacion de input existe para cerrar.
    // Un tiron al guardar es normal en cualquier juego; uno por frame no lo seria, y por eso
    // la excepcion se acota aqui y no mas arriba.
    heap_guard_suspend();
    struct GuardResume {
        ~GuardResume() { heap_guard_resume(); }
    } guard_resume;

    if (is_save) {
        bool captured = render_capture_thumbnail(g_thumbnail_scratch, k_thumbnail_width,
                                               k_thumbnail_height);
        if (captured) {
            qoi_desc desc{};
            desc.width      = static_cast<unsigned int>(k_thumbnail_width);
            desc.height     = static_cast<unsigned int>(k_thumbnail_height);
            desc.channels   = 3;
            desc.colorspace = QOI_SRGB;
            int   encoded_size = 0;
            void* encoded = qoi_encode(g_thumbnail_scratch, &desc, &encoded_size);
            if (encoded != nullptr && static_cast<u32>(encoded_size) <= k_thumbnail_max_bytes) {
                save_game(path, *state, *backlog, static_cast<const u8*>(encoded),
                          static_cast<u32>(encoded_size));
            } else {
                save_game(path, *state, *backlog);
            }
            std::free(encoded);
        } else {
            // Backend sin soporte de captura (GL, ADR-0009): se guarda igualmente, solo
            // que sin miniatura.
            save_game(path, *state, *backlog);
        }
        load_slot_thumbnail(this, static_cast<u32>(selected));
        labels_built = false;
    } else if (slot_has_data[selected]) {
        if (load_game(path, state, backlog) == LoadResult::Ok) {
            vm_resync_after_state_change(state);
            wants_close = true;
        }
    }
}

void SaveLoadMode::render() {
    Sprite panel{};
    panel.tex   = render_white_texture();
    panel.dst_x = 0.0f;
    panel.dst_y = 0.0f;
    panel.dst_w = static_cast<f32>(k_virtual_width);
    panel.dst_h = static_cast<f32>(k_virtual_height);
    panel.color = 0xE6000000u;
    panel.layer = static_cast<u16>(RenderLayer::UI);
    render_draw_sprite(panel);

    if (font.valid() && !labels_built) {
        for (u32 i = 0; i < k_save_slot_count; ++i) {
            char line[64];
            std::snprintf(line, sizeof(line), "%s Slot %u: %s",
                          static_cast<i32>(i) == selected ? ">" : " ", i + 1,
                          slot_has_data[i] ? "guardado" : "vacio");
            slot_label[i] = text_layout(font, line, 600.0f, scratch_arena);
        }
        labels_built = true;
    }

    f32 y = 120.0f;
    for (u32 i = 0; i < k_save_slot_count; ++i) {
        UiRect row = save_slot_rect(i);
        if (static_cast<i32>(i) == selected) {
            ui_draw_button(row, k_ui_row_selected);
        } else if (static_cast<i32>(i) == hovered_slot) {
            ui_draw_button(row, k_ui_button_idle);
        }
        if (slot_has_data[i] && slot_thumbnail[i].valid()) {
            Sprite thumb{};
            thumb.tex   = slot_thumbnail[i];
            thumb.dst_x = 100.0f;
            thumb.dst_y = y;
            thumb.dst_w = static_cast<f32>(k_thumbnail_width) * 0.5f;
            thumb.dst_h = static_cast<f32>(k_thumbnail_height) * 0.5f;
            thumb.layer = static_cast<u16>(RenderLayer::UI);
            thumb.order = 1;  // por encima del fondo de la fila
            render_draw_sprite(thumb);
        }
        if (font.valid()) {
            text_draw(slot_label[i], 350.0f, y, slot_label[i].count);
        }
        y += static_cast<f32>(k_thumbnail_height) * 0.5f + 30.0f;
    }

    if (!font.valid()) {
        return;
    }
    if (!close_label_built) {
        close_label       = text_layout(font, "Cerrar", 200.0f, scratch_arena);
        close_label_built = true;
    }
    UiRect close = save_close_rect();
    ui_draw_button(close, hovered_close ? k_ui_button_hover : k_ui_button_idle);
    text_draw(close_label, close.x + 16.0f, close.y + 8.0f, close_label.count);
}
