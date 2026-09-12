#pragma once
#include <sokol_gfx.h>

#include "core/types.h"

// Shader unico de sprites y de blit de letterbox (ADR-0010: escrito a mano en vez de
// invocar el binario sokol-shdc). Vertice: posicion ya en NDC (calculada en la CPU en
// render.cpp), UV, y color RGBA8 normalizado. Sin uniforms: no hace falta matriz porque la
// proyeccion ya esta resuelta por vertice antes de subir el buffer.

inline const char* k_sprite_vs_hlsl5 = R"(
struct VsIn {
    float2 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};
struct VsOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};
VsOut main(VsIn inp) {
    VsOut outp;
    outp.pos = float4(inp.pos, 0.0, 1.0);
    outp.uv = inp.uv;
    outp.color = inp.color;
    return outp;
}
)";

inline const char* k_sprite_fs_hlsl5 = R"(
Texture2D tex : register(t0);
SamplerState smp : register(s0);
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0, float4 color : COLOR0) : SV_Target {
    return tex.Sample(smp, uv) * color;
}
)";

inline const char* k_sprite_vs_glsl330 = R"(
#version 330
layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 color0;
out vec2 v_uv;
out vec4 v_color;
void main() {
    gl_Position = vec4(pos, 0.0, 1.0);
    v_uv = uv;
    v_color = color0;
}
)";

inline const char* k_sprite_fs_glsl330 = R"(
#version 330
in vec2 v_uv;
in vec4 v_color;
out vec4 frag_color;
uniform sampler2D tex_smp;
void main() {
    frag_color = texture(tex_smp, v_uv) * v_color;
}
)";

// --- Transiciones de pantalla completa (M12, SPEC.md #12: "la misma ruta de codigo para
// fade, wipe y disolucion, nunca un sistema por comando").
//
// Una sola formula para las tres:
//
//     alpha = saturate((threshold - mask) * sharpness)
//
// Lo que cambia entre ellas es DATO, no codigo: que mascara se enlaza y con que sharpness.
//   - fade:     mascara constante 0 y sharpness 1  -> alpha = threshold, un fundido lineal.
//   - wipe:     mascara de degradado horizontal    -> el borde barre la pantalla.
//   - dissolve: mascara de ruido                   -> los pixeles "saltan" dispersos.
//
// Sale premultiplicado (rgb * a, a) porque el pipeline mezcla con ONE/ONE_MINUS_SRC_ALPHA
// (SPEC.md #7.1), igual que los sprites.
//
// El vertice es el MISMO que el de sprites: posicion ya en NDC, uv y color. No hace falta
// un vertex shader aparte.

inline const char* k_transition_fs_hlsl5 = R"(
cbuffer params : register(b0) {
    float4 transition_params;  // x = threshold, y = sharpness
};
Texture2D tex : register(t0);
SamplerState smp : register(s0);
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0, float4 color : COLOR0) : SV_Target {
    float mask = tex.Sample(smp, uv).r;
    float a = saturate((transition_params.x - mask) * transition_params.y);
    return float4(color.rgb * a, a);
}
)";

inline const char* k_transition_fs_glsl330 = R"(
#version 330
in vec2 v_uv;
in vec4 v_color;
out vec4 frag_color;
uniform sampler2D tex_smp;
uniform vec4 transition_params;  // x = threshold, y = sharpness
void main() {
    float mask = texture(tex_smp, v_uv).r;
    float a = clamp((transition_params.x - mask) * transition_params.y, 0.0, 1.0);
    frag_color = vec4(v_color.rgb * a, a);
}
)";

// Un unico float4 en vez de dos float sueltos: evita cualquier diferencia de empaquetado
// de uniforms entre backends (SG_UNIFORMLAYOUT_*), que es justo el tipo de detalle que
// sokol-shdc resolveria y que aqui se escribe a mano (ADR-0010).
struct TransitionUniforms {
    f32 threshold = 0.0f;
    f32 sharpness = 1.0f;
    f32 _pad[2]   = {};
};
static_assert(sizeof(TransitionUniforms) == 16);

inline sg_shader_desc rhi_transition_shader_desc(sg_backend backend) {
    sg_shader_desc desc{};

    if (backend == SG_BACKEND_D3D11) {
        desc.vertex_func.source   = k_sprite_vs_hlsl5;  // mismo vertice que los sprites
        desc.fragment_func.source = k_transition_fs_hlsl5;
        desc.attrs[0]             = {nullptr, "POSITION", 0};
        desc.attrs[1]             = {nullptr, "TEXCOORD", 0};
        desc.attrs[2]             = {nullptr, "COLOR", 0};
    } else {
        desc.vertex_func.source   = k_sprite_vs_glsl330;
        desc.fragment_func.source = k_transition_fs_glsl330;
        desc.attrs[0]             = {"pos", nullptr, 0};
        desc.attrs[1]             = {"uv", nullptr, 0};
        desc.attrs[2]             = {"color0", nullptr, 0};
    }

    desc.images[0] = {
        SG_SHADERSTAGE_FRAGMENT, SG_IMAGETYPE_2D, SG_IMAGESAMPLETYPE_FLOAT, false, 0, 0, 0};
    desc.samplers[0]            = {SG_SHADERSTAGE_FRAGMENT, SG_SAMPLERTYPE_FILTERING, 0, 0, 0};
    desc.image_sampler_pairs[0] = {SG_SHADERSTAGE_FRAGMENT, 0, 0, "tex_smp"};

    desc.uniform_blocks[0].stage             = SG_SHADERSTAGE_FRAGMENT;
    desc.uniform_blocks[0].size              = sizeof(TransitionUniforms);
    desc.uniform_blocks[0].hlsl_register_b_n = 0;
    desc.uniform_blocks[0].layout            = SG_UNIFORMLAYOUT_STD140;
    desc.uniform_blocks[0].glsl_uniforms[0]  = {SG_UNIFORMTYPE_FLOAT4, 1, "transition_params"};

    desc.label = "transition_shader";
    return desc;
}

// Elige las fuentes segun el backend activo en runtime (sg_query_backend()). No hace
// falta un caso SG_BACKEND_METAL_*: ADR-0009 deja Metal sin implementar en M1.
inline sg_shader_desc rhi_sprite_shader_desc(sg_backend backend) {
    sg_shader_desc desc{};

    if (backend == SG_BACKEND_D3D11) {
        desc.vertex_func.source   = k_sprite_vs_hlsl5;
        desc.fragment_func.source = k_sprite_fs_hlsl5;
        desc.attrs[0]             = {nullptr, "POSITION", 0};
        desc.attrs[1]             = {nullptr, "TEXCOORD", 0};
        desc.attrs[2]             = {nullptr, "COLOR", 0};
    } else {
        desc.vertex_func.source   = k_sprite_vs_glsl330;
        desc.fragment_func.source = k_sprite_fs_glsl330;
        desc.attrs[0]             = {"pos", nullptr, 0};
        desc.attrs[1]             = {"uv", nullptr, 0};
        desc.attrs[2]             = {"color0", nullptr, 0};
    }

    desc.images[0] = {
        SG_SHADERSTAGE_FRAGMENT, SG_IMAGETYPE_2D, SG_IMAGESAMPLETYPE_FLOAT, false, 0, 0, 0};
    desc.samplers[0]           = {SG_SHADERSTAGE_FRAGMENT, SG_SAMPLERTYPE_FILTERING, 0, 0, 0};
    desc.image_sampler_pairs[0] = {SG_SHADERSTAGE_FRAGMENT, 0, 0, "tex_smp"};
    desc.label                  = "sprite_shader";
    return desc;
}
