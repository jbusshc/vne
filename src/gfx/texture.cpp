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

#include <cstdlib>

#include "assets/pak.h"
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

// Decodifica QOI y sube la imagen resultante a *slot (sobrescribe image/sampler/width/
// height). Compartido entre texture_load (sincrono, M1) y texture_finish_load_from_memory
// (M11, bytes que ya trajo el hilo de IO): la unica diferencia entre ambos es de donde
// salen los bytes QOI, no que se hace con ellos.
bool decode_qoi_into_slot(TextureSlot* slot, const void* qoi_bytes, usize qoi_size) {
    qoi_desc desc{};
    void*    pixels = qoi_decode(qoi_bytes, static_cast<int>(qoi_size), &desc, 4);
    if (pixels == nullptr) {
        return false;
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
    return true;
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

TextureLoadResult texture_load(const char* logical_name, TextureHandle* out) {
    *out = g_placeholder;

    // M11: ya no abre directamente por ruta de archivo -- se resuelve a traves del
    // backend activo (directorio suelto o .pak, ver assets/pak.h), igual que
    // text_load_font. Sincrono (a diferencia de assets_texture, ver texture_reserve_
    // placeholder/texture_finish_load_from_memory mas abajo): este es el camino que ya
    // usaba el stress test de M1 antes de que existiera el hilo de IO, y sigue siendo
    // valido para cualquier carga que el llamante prefiera bloqueante.
    const u8* bytes = nullptr;
    usize     size  = 0;
    bool      owned = false;
    if (!pak_resolve(logical_name, &bytes, &size, &owned)) {
        log_error("texture_load: no se encontro '%s'", logical_name);
        return TextureLoadResult::NotFound;
    }

    TextureSlot*  slot   = nullptr;
    TextureHandle handle = pool_acquire<struct TextureTag>(&g_pool, &slot);
    if (!handle.valid()) {
        pak_release(bytes, owned);
        log_error("texture_load: pool de texturas lleno (%u)", k_max_textures);
        return TextureLoadResult::OutOfSlots;
    }

    bool ok = decode_qoi_into_slot(slot, bytes, size);
    pak_release(bytes, owned);
    if (!ok) {
        log_error("texture_load: '%s' no es un QOI valido", logical_name);
        return TextureLoadResult::BadFormat;
    }

    *out = handle;
    return TextureLoadResult::Ok;
}

TextureHandle texture_reserve_placeholder() {
    TextureSlot*  placeholder_slot = pool_resolve<struct TextureTag>(&g_pool, g_placeholder);
    TextureSlot*  slot             = nullptr;
    TextureHandle handle           = pool_acquire<struct TextureTag>(&g_pool, &slot);
    if (!handle.valid()) {
        log_error("texture_reserve_placeholder: pool de texturas lleno (%u)", k_max_textures);
        return g_placeholder;
    }
    *slot = *placeholder_slot;  // misma sg_image/sg_sampler que el placeholder: no crea
                                // nada nuevo en la GPU, solo reserva el slot.
    return handle;
}

bool texture_finish_load_from_memory(TextureHandle handle, const u8* qoi_bytes,
                                      usize qoi_size) {
    TextureSlot* slot = pool_resolve<struct TextureTag>(&g_pool, handle);
    if (slot == nullptr) {
        log_error("texture_finish_load_from_memory: handle invalido o caducado");
        return false;
    }
    if (!decode_qoi_into_slot(slot, qoi_bytes, qoi_size)) {
        log_error("texture_finish_load_from_memory: bytes QOI invalidos, se queda con el "
                  "placeholder");
        return false;
    }
    return true;
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
