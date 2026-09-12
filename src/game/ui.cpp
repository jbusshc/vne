#include "game/ui.h"

#include "render/atlas.h"

namespace {

TextureHandle g_atlas;

// Nombres logicos del arte de UI en el atlas (Kenney UI Pack, CC0, en assets_src/png/ desde
// M3). Estan aqui y no repartidos por los modos para que cambiar el aspecto de todos los
// botones sea una linea.
constexpr const char* k_sprite_button = "button_rectangle_depth_flat";
constexpr const char* k_sprite_panel  = "button_rectangle_border";
constexpr const char* k_sprite_track  = "slide_horizontal_grey";

}  // namespace

void ui_set_atlas(TextureHandle atlas) {
    g_atlas = atlas;
}

void ui_draw_rect(const UiRect& r, u32 color, RenderLayer layer, u16 order) {
    Sprite s{};
    s.tex   = render_white_texture();
    s.dst_x = r.x;
    s.dst_y = r.y;
    s.dst_w = r.w;
    s.dst_h = r.h;
    s.color = color;
    s.layer = static_cast<u16>(layer);
    s.order = order;
    render_draw_sprite(s);
}

void ui_draw_nine_slice(const UiRect& r, const char* atlas_name, u32 tint, RenderLayer layer,
                         u16 order, f32 corner) {
    AtlasSprite src{};
    if (!g_atlas.valid() || !atlas_find(atlas_name, &src) || src.w == 0 || src.h == 0) {
        // Sin atlas o sin ese sprite: rectangulo solido, como toda la UI hasta M15.
        ui_draw_rect(r, tint, layer, order);
        return;
    }

    // La esquina no puede pasar de la mitad del sprite ni de la mitad del destino, o los
    // trozos se solaparian y el centro tendria ancho negativo.
    f32 src_corner = corner;
    if (src_corner > static_cast<f32>(src.w) * 0.5f) src_corner = static_cast<f32>(src.w) * 0.5f;
    if (src_corner > static_cast<f32>(src.h) * 0.5f) src_corner = static_cast<f32>(src.h) * 0.5f;
    f32 dst_corner = src_corner;
    if (dst_corner > r.w * 0.5f) dst_corner = r.w * 0.5f;
    if (dst_corner > r.h * 0.5f) dst_corner = r.h * 0.5f;

    // Cortes en el sprite y en el destino: tres columnas y tres filas cada uno.
    const f32 sx[4] = {static_cast<f32>(src.x), static_cast<f32>(src.x) + src_corner,
                       static_cast<f32>(src.x + src.w) - src_corner,
                       static_cast<f32>(src.x + src.w)};
    const f32 sy[4] = {static_cast<f32>(src.y), static_cast<f32>(src.y) + src_corner,
                       static_cast<f32>(src.y + src.h) - src_corner,
                       static_cast<f32>(src.y + src.h)};
    const f32 dx[4] = {r.x, r.x + dst_corner, r.x + r.w - dst_corner, r.x + r.w};
    const f32 dy[4] = {r.y, r.y + dst_corner, r.y + r.h - dst_corner, r.y + r.h};

    for (u32 row = 0; row < 3; ++row) {
        for (u32 col = 0; col < 3; ++col) {
            f32 w = dx[col + 1] - dx[col];
            f32 h = dy[row + 1] - dy[row];
            if (w <= 0.0f || h <= 0.0f) {
                continue;  // el destino es mas pequeno que las esquinas por ese eje
            }
            Sprite s{};
            s.tex   = g_atlas;
            s.src_x = sx[col];
            s.src_y = sy[row];
            s.src_w = sx[col + 1] - sx[col];
            s.src_h = sy[row + 1] - sy[row];
            s.dst_x = dx[col];
            s.dst_y = dy[row];
            s.dst_w = w;
            s.dst_h = h;
            s.color = tint;
            s.layer = static_cast<u16>(layer);
            s.order = order;
            render_draw_sprite(s);
        }
    }
}

void ui_draw_button(const UiRect& r, u32 tint, RenderLayer layer, u16 order) {
    ui_draw_nine_slice(r, k_sprite_button, tint, layer, order);
}

void ui_draw_panel(const UiRect& r, u32 tint, RenderLayer layer, u16 order) {
    ui_draw_nine_slice(r, k_sprite_panel, tint, layer, order);
}

void ui_draw_slider(const UiRect& track, f32 value) {
    // El carril es arte de verdad (slide_horizontal_grey, 96x16); el relleno es un color
    // plano a proposito, porque un relleno proporcional ES una barra de color.
    ui_draw_nine_slice(track, k_sprite_track, 0xFFFFFFFFu, RenderLayer::UI, 0, 6.0f);
    if (value <= 0.0f) {
        return;
    }
    UiRect fill = track;
    fill.x += 4.0f;
    fill.y += 6.0f;
    fill.h -= 12.0f;
    fill.w = (track.w - 8.0f) * (value < 1.0f ? value : 1.0f);
    if (fill.h > 0.0f && fill.w > 0.0f) {
        ui_draw_rect(fill, 0xFF80A0C0u, RenderLayer::UI, 1);
    }
}
