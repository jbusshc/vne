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

#include "gfx/gfx.h"
#include "gfx/texture.h"

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
    for (u32 i = 0; i < k_save_slot_count; ++i) {
        load_slot_thumbnail(this, i);
    }
    labels_built = false;
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
    if (!input.key_pressed[SDL_SCANCODE_RETURN] && !input.key_pressed[SDL_SCANCODE_SPACE]) {
        return;
    }

    const char* path = save_slot_path(static_cast<u32>(selected));
    if (is_save) {
        bool captured = gfx_capture_thumbnail(g_thumbnail_scratch, k_thumbnail_width,
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
    panel.tex   = gfx_white_texture();
    panel.dst_x = 0.0f;
    panel.dst_y = 0.0f;
    panel.dst_w = static_cast<f32>(k_virtual_width);
    panel.dst_h = static_cast<f32>(k_virtual_height);
    panel.color = 0xE6000000u;
    panel.layer = static_cast<u16>(GfxLayer::UI);
    gfx_draw_sprite(panel);

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
        if (slot_has_data[i] && slot_thumbnail[i].valid()) {
            Sprite thumb{};
            thumb.tex   = slot_thumbnail[i];
            thumb.dst_x = 100.0f;
            thumb.dst_y = y;
            thumb.dst_w = static_cast<f32>(k_thumbnail_width) * 0.5f;
            thumb.dst_h = static_cast<f32>(k_thumbnail_height) * 0.5f;
            thumb.layer = static_cast<u16>(GfxLayer::UI);
            gfx_draw_sprite(thumb);
        }
        if (font.valid()) {
            text_draw(slot_label[i], 350.0f, y, slot_label[i].count);
        }
        y += static_cast<f32>(k_thumbnail_height) * 0.5f + 30.0f;
    }
}
