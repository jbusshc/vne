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
void (*g_editor_render_hook)() = nullptr;

namespace {

sg_shader     g_shader{};
sg_pipeline   g_pipeline{};
sg_shader     g_transition_shader{};
sg_pipeline   g_transition_pipeline{};
sg_buffer     g_vertex_buffer{};
sg_image      g_scene_color_image{};
sg_attachments g_scene_attachments{};
sg_sampler    g_scene_sampler{};

Sprite* g_sprite_queue = nullptr;
u32     g_sprite_count = 0;

// Transicion pendiente de este frame (M12). Inmediata como la cola de sprites: se limpia
// en gfx_begin_frame y hay que volver a pedirla cada frame.
bool              g_transition_active   = false;
GfxTransitionMask g_transition_mask     = GfxTransitionMask::Fade;
f32               g_transition_threshold = 0.0f;
u32               g_transition_color     = 0xFF000000u;

// Mascaras generadas proceduralmente la primera vez que se usan (no son assets: no hay
// nada que autorar ni que empaquetar, y evita depender de arte de terceros para una
// funcion del motor). Ver gfx/shaders.h para que significa cada una en la formula.
TextureHandle g_transition_mask_textures[3]{};

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

// Genera la mascara de una transicion (M12). Los tres casos escriben un valor 0..255 en
// los cuatro canales; el shader solo lee .r, pero replicarlo deja la textura legible si
// alguna vez se inspecciona a ojo.
//
// Los tamanos son pequenos a proposito y se muestrean con NEAREST, igual que todo lo
// demas: con el sharpness que usa cada transicion, el borde es mas ancho que un texel
// estirado a 1920x1080, asi que no se ve bandeado. En dissolve el "bloque" grande es
// justamente el aspecto buscado (SPEC.md #13.2 fija estetica PS2).
TextureHandle make_transition_mask(GfxTransitionMask kind) {
    i32 w = 1, h = 1;
    switch (kind) {
        case GfxTransitionMask::Fade:     w = 1;   h = 1;  break;
        case GfxTransitionMask::Wipe:     w = 512; h = 1;  break;
        case GfxTransitionMask::Dissolve: w = 64;  h = 64; break;
    }

    TextureHandle tex = texture_create_dynamic(w, h);
    u8*           pixels = arena_alloc_n<u8>(&g_arena_frame, static_cast<usize>(w) * h * 4);
    if (pixels == nullptr) {
        log_error("make_transition_mask: sin espacio en la arena de frame");
        return tex;
    }

    // Generador propio y determinista (no <random>, que asigna y no da la misma secuencia
    // entre implementaciones): un LCG basta de sobra para una mascara de ruido.
    u32 rng = 0x9E3779B9u;
    for (i32 y = 0; y < h; ++y) {
        for (i32 x = 0; x < w; ++x) {
            u8 value = 0;
            switch (kind) {
                case GfxTransitionMask::Fade:
                    value = 0;  // constante: alpha = threshold * sharpness, fundido lineal
                    break;
                case GfxTransitionMask::Wipe:
                    value = static_cast<u8>((x * 255) / (w - 1));  // degradado izq -> der
                    break;
                case GfxTransitionMask::Dissolve:
                    rng   = rng * 1664525u + 1013904223u;
                    value = static_cast<u8>((rng >> 24) & 0xFFu);
                    break;
            }
            u8* p = &pixels[(static_cast<usize>(y) * w + x) * 4];
            p[0] = p[1] = p[2] = value;
            p[3]                = 255;
        }
    }
    texture_update_dynamic(tex, pixels);
    return tex;
}

// Las tres se crean en gfx_init, NO perezosamente en el primer uso: crearlas dentro de
// gfx_flush significaria llamar a sg_make_image/sg_update_image con una render pass
// abierta, que sokol_gfx no permite (el juego moria nada mas arrancar la primera vez que
// se escribio asi).
TextureHandle transition_mask_texture(GfxTransitionMask kind) {
    return g_transition_mask_textures[static_cast<u32>(kind)];
}

// Cuanto "endurece" el borde cada transicion. fade usa 1 a proposito: con su mascara
// constante 0, alpha = saturate(threshold), que es exactamente un fundido lineal. Las
// otras dos quieren un borde marcado que barra o disperse.
f32 transition_sharpness(GfxTransitionMask kind) {
    switch (kind) {
        case GfxTransitionMask::Fade:     return 1.0f;
        case GfxTransitionMask::Wipe:     return 24.0f;
        case GfxTransitionMask::Dissolve: return 24.0f;
    }
    return 1.0f;
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

    // Mismo formato de vertice y mismo blend que los sprites: lo unico que cambia es el
    // shader (mascara + umbral, ver gfx/shaders.h), asi que se parte del mismo desc.
    sg_shader_desc trans_shd_desc = gfx_transition_shader_desc(sg_query_backend());
    g_transition_shader           = sg_make_shader(&trans_shd_desc);
    sg_pipeline_desc trans_pip_desc = sprite_pipeline_desc();
    trans_pip_desc.shader           = g_transition_shader;
    trans_pip_desc.label            = "transition_pipeline";
    g_transition_pipeline           = sg_make_pipeline(&trans_pip_desc);

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

    // Aqui y no en el primer uso: make_transition_mask llama a sg_make_image y
    // sg_update_image, y sokol_gfx no admite ninguna de las dos con una pasada abierta
    // (que es donde estaria si se crearan perezosamente desde gfx_flush).
    for (u32 i = 0; i < 3; ++i) {
        g_transition_mask_textures[i] = make_transition_mask(static_cast<GfxTransitionMask>(i));
    }

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
    g_transition_active   = false;
}

void gfx_draw_transition(GfxTransitionMask mask, f32 threshold, u32 color) {
    g_transition_active    = true;
    g_transition_mask      = mask;
    g_transition_threshold = threshold;
    g_transition_color     = color;
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

    // El quad de la transicion se sube ANTES de abrir la pasada, igual que los sprites y
    // que el blit de letterbox de gfx_present: sg_append_buffer no se llama dentro de una
    // pasada en este motor.
    int transition_offset = 0;
    if (g_transition_active) {
        const SpriteVertex quad[6] = {
            {-1.0f, 1.0f, 0.0f, 0.0f, g_transition_color},
            {1.0f, 1.0f, 1.0f, 0.0f, g_transition_color},
            {1.0f, -1.0f, 1.0f, 1.0f, g_transition_color},
            {-1.0f, 1.0f, 0.0f, 0.0f, g_transition_color},
            {1.0f, -1.0f, 1.0f, 1.0f, g_transition_color},
            {-1.0f, -1.0f, 0.0f, 1.0f, g_transition_color},
        };
        sg_range quad_data{quad, sizeof(quad)};
        transition_offset = sg_append_buffer(g_vertex_buffer, &quad_data);
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

    // Encima de todo lo demas (capa Transition = 7, la mas alta): una sola draw call
    // extra, la misma para fade/wipe/dissolve -- solo cambian la mascara enlazada y el
    // sharpness (SPEC.md #12, criterio "sin que draw_calls suba mas de 1").
    if (g_transition_active) {
        TextureHandle mask_tex = transition_mask_texture(g_transition_mask);
        sg_apply_pipeline(g_transition_pipeline);

        sg_bindings bnd{};
        bnd.vertex_buffers[0]        = g_vertex_buffer;
        bnd.vertex_buffer_offsets[0] = transition_offset;
        bnd.images[0]                = texture_gpu_image(mask_tex);
        bnd.samplers[0]              = texture_gpu_sampler(mask_tex);
        sg_apply_bindings(&bnd);

        GfxTransitionUniforms uniforms{};
        uniforms.threshold = g_transition_threshold;
        uniforms.sharpness = transition_sharpness(g_transition_mask);
        sg_range uniform_data{&uniforms, sizeof(uniforms)};
        sg_apply_uniforms(0, &uniform_data);

        sg_draw(0, 6, 1);
        g_gfx_draw_call_count += 1;
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

    // SPEC.md #6.5: "editor_render() -> solo si VN_EDITOR", dentro de la misma pasada al
    // swapchain, justo despues del blit de letterbox y antes de terminarla (para que el
    // editor se dibuje encima de la escena ya compuesta, no debajo).
    if (g_editor_render_hook != nullptr) {
        g_editor_render_hook();
    }

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
