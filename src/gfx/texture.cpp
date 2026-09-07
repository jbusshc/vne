#include "gfx/texture.h"

// qoi.h es codigo de terceros: sus conversiones implicitas int/uchar no son un problema
// de nuestro codigo, pero MSVC las marca bajo /W4. Se silencia solo alrededor de este
// include, igual que con doctest en tests/CMakeLists.txt.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4267)
#endif
#define QOI_IMPLEMENTATION
#define QOI_NO_STDIO
#include <qoi.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <cstdio>
#include <cstdlib>

#include "base/arena.h"
#include "base/log.h"
#include "base/pool.h"
#include "gfx/texture_internal.h"

namespace {

constexpr u32 k_max_textures = 256;

struct TextureSlot {
    sg_image   image;
    sg_sampler sampler;
    i32        width  = 0;
    i32        height = 0;
};

Pool<TextureSlot, k_max_textures> g_pool;
TextureHandle                     g_placeholder;

// 2x2 magenta bien visible: ver la textura equivocada en pantalla debe ser inconfundible
// (SPEC.md #7.4).
sg_image make_placeholder_image() {
    static const u32   k_pixels[4] = {0xFFFF00FFu, 0xFFFF00FFu, 0xFFFF00FFu, 0xFFFF00FFu};
    sg_image_desc desc{};
    desc.width                 = 2;
    desc.height                = 2;
    desc.pixel_format          = SG_PIXELFORMAT_RGBA8;
    desc.data.subimage[0][0].ptr  = k_pixels;
    desc.data.subimage[0][0].size = sizeof(k_pixels);
    desc.label                 = "texture_placeholder";
    return sg_make_image(&desc);
}

sg_sampler make_default_sampler() {
    sg_sampler_desc desc{};
    desc.min_filter = SG_FILTER_NEAREST;
    desc.mag_filter = SG_FILTER_NEAREST;
    desc.wrap_u     = SG_WRAP_CLAMP_TO_EDGE;
    desc.wrap_v     = SG_WRAP_CLAMP_TO_EDGE;
    desc.label      = "texture_sampler";
    return sg_make_sampler(&desc);
}

}  // namespace

void texture_system_init() {
    pool_init(&g_pool);

    TextureSlot* slot = nullptr;
    g_placeholder      = pool_acquire<struct TextureTag>(&g_pool, &slot);
    slot->image        = make_placeholder_image();
    slot->sampler       = make_default_sampler();
    slot->width         = 2;
    slot->height        = 2;
}

void texture_system_shutdown() {
    // Los objetos sg_image/sg_sampler se liberan todos juntos en sg_shutdown(); no hace
    // falta iterar el pool aqui.
    g_pool = Pool<TextureSlot, k_max_textures>{};
}

TextureLoadResult texture_load(const char* path, TextureHandle* out) {
    *out = g_placeholder;

    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        log_error("texture_load: no se encontro '%s'", path);
        return TextureLoadResult::NotFound;
    }
    std::fseek(file, 0, SEEK_END);
    long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(file);
        log_error("texture_load: '%s' esta vacio", path);
        return TextureLoadResult::BadFormat;
    }

    u8* bytes = arena_alloc_n<u8>(&g_arena_frame, static_cast<usize>(size));
    usize read = std::fread(bytes, 1, static_cast<usize>(size), file);
    std::fclose(file);
    if (read != static_cast<usize>(size)) {
        log_error("texture_load: lectura incompleta de '%s'", path);
        return TextureLoadResult::BadFormat;
    }

    qoi_desc desc{};
    void*    pixels = qoi_decode(bytes, static_cast<int>(size), &desc, 4);
    if (pixels == nullptr) {
        log_error("texture_load: '%s' no es un QOI valido", path);
        return TextureLoadResult::BadFormat;
    }

    TextureSlot* slot   = nullptr;
    TextureHandle handle = pool_acquire<struct TextureTag>(&g_pool, &slot);
    if (!handle.valid()) {
        std::free(pixels);
        log_error("texture_load: pool de texturas lleno (%u)", k_max_textures);
        return TextureLoadResult::OutOfSlots;
    }

    sg_image_desc img_desc{};
    img_desc.width                    = static_cast<int>(desc.width);
    img_desc.height                   = static_cast<int>(desc.height);
    img_desc.pixel_format             = SG_PIXELFORMAT_RGBA8;
    img_desc.data.subimage[0][0].ptr  = pixels;
    img_desc.data.subimage[0][0].size = static_cast<usize>(desc.width) * desc.height * 4;

    slot->image   = sg_make_image(&img_desc);
    slot->sampler = make_default_sampler();
    slot->width   = static_cast<i32>(desc.width);
    slot->height  = static_cast<i32>(desc.height);
    std::free(pixels);

    *out = handle;
    return TextureLoadResult::Ok;
}

TextureHandle texture_create_dynamic(i32 width, i32 height) {
    TextureSlot*  slot   = nullptr;
    TextureHandle handle = pool_acquire<struct TextureTag>(&g_pool, &slot);
    if (!handle.valid()) {
        log_error("texture_create_dynamic: pool de texturas lleno (%u)", k_max_textures);
        return g_placeholder;
    }

    sg_image_desc img_desc{};
    img_desc.width        = width;
    img_desc.height       = height;
    img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    img_desc.usage        = SG_USAGE_DYNAMIC;
    img_desc.label        = "texture_dynamic";

    slot->image   = sg_make_image(&img_desc);
    slot->sampler = make_default_sampler();
    slot->width   = width;
    slot->height  = height;
    return handle;
}

void texture_update_dynamic(TextureHandle h, const u8* pixels_rgba) {
    TextureSlot* slot = pool_resolve<struct TextureTag>(&g_pool, h);
    if (slot == nullptr) {
        log_error("texture_update_dynamic: handle invalido");
        return;
    }
    sg_image_data data{};
    data.subimage[0][0].ptr  = pixels_rgba;
    data.subimage[0][0].size = static_cast<usize>(slot->width) * slot->height * 4;
    sg_update_image(slot->image, &data);
}

void texture_size(TextureHandle h, i32* out_w, i32* out_h) {
    TextureSlot* slot = pool_resolve<struct TextureTag>(&g_pool, h);
    *out_w            = slot != nullptr ? slot->width : 0;
    *out_h            = slot != nullptr ? slot->height : 0;
}

sg_image texture_gpu_image(TextureHandle h) {
    TextureSlot* slot = pool_resolve<struct TextureTag>(&g_pool, h);
    return slot != nullptr ? slot->image : pool_resolve<struct TextureTag>(&g_pool, g_placeholder)->image;
}

sg_sampler texture_gpu_sampler(TextureHandle h) {
    TextureSlot* slot = pool_resolve<struct TextureTag>(&g_pool, h);
    return slot != nullptr ? slot->sampler
                           : pool_resolve<struct TextureTag>(&g_pool, g_placeholder)->sampler;
}
