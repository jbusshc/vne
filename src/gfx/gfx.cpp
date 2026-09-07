#include "gfx/gfx.h"

// SOKOL_IMPL debe compilarse en exactamente una unidad de traduccion (ADR-0010 / vne-cpp
// convenciones de terceros): esta. El backend (SOKOL_D3D11 / SOKOL_GLCORE) se define por
// CMake solo para este archivo (ver CMakeLists.txt, set_source_files_properties).
#define SOKOL_IMPL
#include <sokol_gfx.h>
// La seccion de implementacion de sokol_gfx.h no tiene guarda de doble inclusion propia
// (solo la parte de declaraciones la tiene): si otro header de este archivo vuelve a
// incluir <sokol_gfx.h> con SOKOL_GFX_IMPL todavia definido, el cuerpo entero se compila
// una segunda vez y todo redefine. Se deshace el define en cuanto termina esta inclusion.
#undef SOKOL_IMPL
#undef SOKOL_GFX_IMPL

#include <cmath>

#include "base/arena.h"
#include "base/assert.h"
#include "base/log.h"
#include "base/radix_sort.h"
#include "gfx/gfx_backend.h"
#include "gfx/shaders.h"
#include "gfx/texture.h"
#include "gfx/texture_internal.h"

namespace {

constexpr u32 k_max_sprites_per_frame = 8192;

struct SpriteVertex {
    f32 x, y;
    f32 u, v;
    u32 color;
};

struct Corner {
    f32 x, y;
};

PlatformWindow* g_window = nullptr;

}  // namespace

u32 g_gfx_draw_call_count = 0;

namespace {

sg_shader     g_shader{};
sg_pipeline   g_pipeline{};
sg_buffer     g_vertex_buffer{};
sg_image      g_scene_color_image{};
sg_attachments g_scene_attachments{};
sg_sampler    g_scene_sampler{};

Sprite* g_sprite_queue = nullptr;
u32     g_sprite_count = 0;

void sprite_corners(const Sprite& s, Corner out[4]) {
    f32 hw = s.dst_w * 0.5f;
    f32 hh = s.dst_h * 0.5f;
    f32 cx = s.dst_x + hw;
    f32 cy = s.dst_y + hh;
    f32 c  = std::cos(s.rotation);
    f32 sn = std::sin(s.rotation);
    Corner local[4]{{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
    for (i32 i = 0; i < 4; ++i) {
        f32 lx  = local[i].x * c - local[i].y * sn;
        f32 ly  = local[i].x * sn + local[i].y * c;
        out[i]  = {cx + lx, cy + ly};
    }
}

void to_ndc(f32 vx, f32 vy, f32* out_x, f32* out_y) {
    *out_x = (vx / static_cast<f32>(k_virtual_width)) * 2.0f - 1.0f;
    *out_y = 1.0f - (vy / static_cast<f32>(k_virtual_height)) * 2.0f;
}

sg_pipeline_desc sprite_pipeline_desc() {
    sg_pipeline_desc desc{};
    desc.shader                       = g_shader;
    desc.layout.attrs[0].format       = SG_VERTEXFORMAT_FLOAT2;
    desc.layout.attrs[1].format       = SG_VERTEXFORMAT_FLOAT2;
    desc.layout.attrs[2].format       = SG_VERTEXFORMAT_UBYTE4N;
    desc.color_count                  = 1;
    desc.colors[0].pixel_format       = SG_PIXELFORMAT_RGBA8;
    // Color premultiplicado (SPEC.md #7.1): blend ONE / ONE_MINUS_SRC_ALPHA, no
    // SRC_ALPHA / ONE_MINUS_SRC_ALPHA (esa formula es para alpha directo).
    desc.colors[0].blend.enabled          = true;
    desc.colors[0].blend.src_factor_rgb   = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    desc.primitive_type                = SG_PRIMITIVETYPE_TRIANGLES;
    desc.cull_mode                     = SG_CULLMODE_NONE;
    desc.label                         = "sprite_pipeline";
    return desc;
}

}  // namespace

bool gfx_init(PlatformWindow* window) {
    g_window = window;

    if (!gfx_backend_init(window)) {
        return false;
    }

    sg_desc desc{};
    desc.environment = gfx_backend_environment();
    sg_setup(&desc);
    if (!sg_isvalid()) {
        log_error("sg_setup fallo");
        return false;
    }

    // texture_system_init crea la textura placeholder con sg_make_image: debe ir
    // despues de sg_setup, nunca antes.
    texture_system_init();

    sg_shader_desc shd_desc = gfx_sprite_shader_desc(sg_query_backend());
    g_shader                = sg_make_shader(&shd_desc);

    sg_pipeline_desc pip_desc = sprite_pipeline_desc();
    g_pipeline                = sg_make_pipeline(&pip_desc);

    sg_buffer_desc vb_desc{};
    vb_desc.size  = static_cast<usize>(k_max_sprites_per_frame) * 6 * sizeof(SpriteVertex);
    vb_desc.type  = SG_BUFFERTYPE_VERTEXBUFFER;
    vb_desc.usage = SG_USAGE_STREAM;
    vb_desc.label = "sprite_vertex_buffer";
    g_vertex_buffer = sg_make_buffer(&vb_desc);

    sg_image_desc scene_desc{};
    scene_desc.render_target = true;
    scene_desc.width         = k_virtual_width;
    scene_desc.height        = k_virtual_height;
    scene_desc.pixel_format  = SG_PIXELFORMAT_RGBA8;
    scene_desc.label         = "scene_color_target";
    g_scene_color_image      = sg_make_image(&scene_desc);

    sg_attachments_desc att_desc{};
    att_desc.colors[0].image = g_scene_color_image;
    att_desc.label           = "scene_attachments";
    g_scene_attachments      = sg_make_attachments(&att_desc);

    sg_sampler_desc smp_desc{};
    smp_desc.min_filter = SG_FILTER_NEAREST;
    smp_desc.mag_filter = SG_FILTER_NEAREST;
    smp_desc.wrap_u     = SG_WRAP_CLAMP_TO_EDGE;
    smp_desc.wrap_v     = SG_WRAP_CLAMP_TO_EDGE;
    smp_desc.label      = "scene_sampler";
    g_scene_sampler     = sg_make_sampler(&smp_desc);

    return true;
}

void gfx_shutdown() {
    sg_shutdown();
    gfx_backend_shutdown();
    texture_system_shutdown();
    g_window = nullptr;
}

void gfx_begin_frame() {
    g_sprite_queue     = arena_alloc_n<Sprite>(&g_arena_frame, k_max_sprites_per_frame);
    g_sprite_count     = 0;
    g_gfx_draw_call_count = 0;
}

void gfx_draw_sprite(const Sprite& s) {
    if (g_sprite_count >= k_max_sprites_per_frame) {
        log_error("gfx_draw_sprite: cola llena (%u), se descarta el sprite", k_max_sprites_per_frame);
        return;
    }
    g_sprite_queue[g_sprite_count] = s;
    g_sprite_count += 1;
}

void gfx_flush() {
    RadixSortEntry* entries =
        arena_alloc_n<RadixSortEntry>(&g_arena_frame, g_sprite_count > 0 ? g_sprite_count : 1);
    for (u32 i = 0; i < g_sprite_count; ++i) {
        const Sprite& s  = g_sprite_queue[i];
        entries[i].key   = (static_cast<u64>(s.layer) << 48) | (static_cast<u64>(s.order) << 32) |
                          static_cast<u64>(s.tex.index);
        entries[i].index = i;
    }
    radix_sort_u64(entries, g_sprite_count, &g_arena_frame);

    int vb_base_offset = 0;
    if (g_sprite_count > 0) {
        SpriteVertex* verts = arena_alloc_n<SpriteVertex>(&g_arena_frame, static_cast<usize>(g_sprite_count) * 6);
        for (u32 i = 0; i < g_sprite_count; ++i) {
            const Sprite& s = g_sprite_queue[entries[i].index];
            i32           tw = 1;
            i32           th = 1;
            texture_size(s.tex, &tw, &th);
            if (tw <= 0) tw = 1;
            if (th <= 0) th = 1;

            Corner c[4];
            sprite_corners(s, c);
            f32 u0 = s.src_x / static_cast<f32>(tw);
            f32 v0 = s.src_y / static_cast<f32>(th);
            f32 u1 = (s.src_x + s.src_w) / static_cast<f32>(tw);
            f32 v1 = (s.src_y + s.src_h) / static_cast<f32>(th);

            SpriteVertex quad[4];
            to_ndc(c[0].x, c[0].y, &quad[0].x, &quad[0].y);
            quad[0].u = u0;
            quad[0].v = v0;
            to_ndc(c[1].x, c[1].y, &quad[1].x, &quad[1].y);
            quad[1].u = u1;
            quad[1].v = v0;
            to_ndc(c[2].x, c[2].y, &quad[2].x, &quad[2].y);
            quad[2].u = u1;
            quad[2].v = v1;
            to_ndc(c[3].x, c[3].y, &quad[3].x, &quad[3].y);
            quad[3].u = u0;
            quad[3].v = v1;
            quad[0].color = quad[1].color = quad[2].color = quad[3].color = s.color;

            SpriteVertex* dst = &verts[static_cast<usize>(i) * 6];
            dst[0] = quad[0];
            dst[1] = quad[1];
            dst[2] = quad[2];
            dst[3] = quad[0];
            dst[4] = quad[2];
            dst[5] = quad[3];
        }

        sg_range vb_data{verts, static_cast<usize>(g_sprite_count) * 6 * sizeof(SpriteVertex)};
        vb_base_offset = sg_append_buffer(g_vertex_buffer, &vb_data);
    }

    sg_pass pass{};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {0.05f, 0.05f, 0.08f, 1.0f};
    pass.attachments                  = g_scene_attachments;
    sg_begin_pass(&pass);
    sg_apply_viewport(0, 0, k_virtual_width, k_virtual_height, true);

    if (g_sprite_count > 0) {
        sg_apply_pipeline(g_pipeline);
        u32 batch_start = 0;
        while (batch_start < g_sprite_count) {
            TextureHandle batch_tex = g_sprite_queue[entries[batch_start].index].tex;
            u32           batch_end = batch_start + 1;
            while (batch_end < g_sprite_count &&
                   g_sprite_queue[entries[batch_end].index].tex == batch_tex) {
                batch_end += 1;
            }

            sg_bindings bnd{};
            bnd.vertex_buffers[0]        = g_vertex_buffer;
            bnd.vertex_buffer_offsets[0] = vb_base_offset + static_cast<int>(batch_start) * 6 *
                                                                 static_cast<int>(sizeof(SpriteVertex));
            bnd.images[0]   = texture_gpu_image(batch_tex);
            bnd.samplers[0] = texture_gpu_sampler(batch_tex);
            sg_apply_bindings(&bnd);
            sg_draw(0, static_cast<int>(batch_end - batch_start) * 6, 1);
            g_gfx_draw_call_count += 1;

            batch_start = batch_end;
        }
    }

    sg_end_pass();
}

Recti gfx_letterbox_rect(i32 window_w, i32 window_h, i32 virtual_w, i32 virtual_h) {
    if (window_w <= 0 || window_h <= 0 || virtual_w <= 0 || virtual_h <= 0) {
        return {0, 0, 0, 0};
    }
    f32 window_aspect  = static_cast<f32>(window_w) / static_cast<f32>(window_h);
    f32 virtual_aspect = static_cast<f32>(virtual_w) / static_cast<f32>(virtual_h);

    Recti r{};
    if (window_aspect > virtual_aspect) {
        // Ventana mas ancha que el contenido: barras a los lados (pillarbox).
        r.h = window_h;
        r.w = static_cast<i32>(std::lround(window_h * virtual_aspect));
        r.x = (window_w - r.w) / 2;
        r.y = 0;
    } else {
        // Ventana mas alta/estrecha que el contenido: barras arriba y abajo (letterbox).
        r.w = window_w;
        r.h = static_cast<i32>(std::lround(window_w / virtual_aspect));
        r.x = 0;
        r.y = (window_h - r.h) / 2;
    }
    return r;
}

void gfx_present(i32 window_w, i32 window_h) {
    sg_swapchain sc = gfx_backend_begin_frame(window_w, window_h);
    Recti        vp = gfx_letterbox_rect(window_w, window_h, k_virtual_width, k_virtual_height);

    const SpriteVertex blit_verts[6] = {
        {-1.0f, 1.0f, 0.0f, 0.0f, 0xFFFFFFFFu}, {1.0f, 1.0f, 1.0f, 0.0f, 0xFFFFFFFFu},
        {1.0f, -1.0f, 1.0f, 1.0f, 0xFFFFFFFFu}, {-1.0f, 1.0f, 0.0f, 0.0f, 0xFFFFFFFFu},
        {1.0f, -1.0f, 1.0f, 1.0f, 0xFFFFFFFFu}, {-1.0f, -1.0f, 0.0f, 1.0f, 0xFFFFFFFFu},
    };
    sg_range blit_data{blit_verts, sizeof(blit_verts)};
    int      blit_offset = sg_append_buffer(g_vertex_buffer, &blit_data);

    sg_pass pass{};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 1.0f};
    pass.swapchain                    = sc;
    sg_begin_pass(&pass);
    sg_apply_viewport(vp.x, vp.y, vp.w, vp.h, true);
    sg_apply_pipeline(g_pipeline);

    sg_bindings bnd{};
    bnd.vertex_buffers[0]        = g_vertex_buffer;
    bnd.vertex_buffer_offsets[0] = blit_offset;
    bnd.images[0]                = g_scene_color_image;
    bnd.samplers[0]              = g_scene_sampler;
    sg_apply_bindings(&bnd);
    sg_draw(0, 6, 1);
    g_gfx_draw_call_count += 1;

    sg_end_pass();
    sg_commit();

    gfx_backend_present();
}

namespace {
TextureHandle g_white_texture{};
}  // namespace

TextureHandle gfx_white_texture() {
    if (!g_white_texture.valid()) {
        g_white_texture = texture_create_dynamic(1, 1);
        const u8 white_pixel[4] = {255, 255, 255, 255};
        texture_update_dynamic(g_white_texture, white_pixel);
    }
    return g_white_texture;
}

bool gfx_capture_thumbnail(u8* out_rgb, i32 out_w, i32 out_h) {
    return gfx_backend_capture_thumbnail(g_scene_color_image, out_rgb, out_w, out_h);
}
