#include "assets/hot_reload.h"

#if defined(VN_DEBUG)

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "assets/assets.h"
#include "assets/pak.h"
#include "base/heap_guard.h"
#include "base/log.h"
#include "platform/files.h"
#include "text/font.h"
#include "text/glyph_cache.h"

namespace {

constexpr f32 k_check_interval_s = 0.5f;  // SPEC.md #7.4
constexpr u32 k_max_watched_fonts = 8;

struct WatchedFont {
    FontHandle handle;
    char       logical_name[128] = {};
    char       source_path[512]  = {};
    u32        px_size            = 0;
    i64        last_mtime         = 0;
};

WatchedFont g_fonts[k_max_watched_fonts];
u32         g_font_count = 0;

// El atlas no se vigila por archivo sino por directorio: cualquier .png que cambie obliga
// a re-empaquetar el atlas entero (es un shelf packer sobre todo assets_src/png/, ADR-0025),
// asi que basta con quedarse con el mtime mas reciente de la carpeta.
char g_png_dir[512] = {};
i64  g_png_newest_mtime = 0;

f32 g_timer = 0.0f;

void png_newest_callback(void* userdata, const char* /*name_no_ext*/, const char* full_name) {
    i64* newest = static_cast<i64*>(userdata);
    char path[1024];
    std::snprintf(path, sizeof(path), "%s/%s", g_png_dir, full_name);
    i64 mtime = file_mtime_ns(path);
    if (mtime > *newest) {
        *newest = mtime;
    }
}

i64 png_dir_newest_mtime() {
    i64 newest = 0;
    dir_list_by_extension(g_png_dir, ".png", png_newest_callback, &newest);
    return newest;
}

void rebake_atlas_and_reload() {
    // vne_bake sin argumentos re-empaqueta el atlas desde assets_src/png/ relativo a su
    // propio CWD, que es el mismo directorio de build donde corre el juego.
    // system() asigna heap: misma excepcion acotada y ya aceptada que la recarga de
    // scripts del editor (ADR-0042), y por el mismo motivo -- esto no existe en Ship.
    heap_guard_suspend();
#if defined(_WIN32)
    int result = std::system(".\\vne_bake.exe");
#else
    int result = std::system("./vne_bake");
#endif
    heap_guard_resume();

    if (result != 0) {
        log_error("hot_reload: vne_bake fallo re-empaquetando el atlas");
        return;
    }
    assets_reload_texture("atlas_00.qoi");
}

}  // namespace

void hot_reload_init() {
    g_font_count = 0;
    g_timer      = 0.0f;
    // Ruta real, no nombre logico: esto vigila el ORIGEN (los .png de autoria), no el
    // asset horneado que consume el juego.
    std::snprintf(g_png_dir, sizeof(g_png_dir), "%s", "assets_src/png");
    g_png_newest_mtime = png_dir_newest_mtime();
}

void hot_reload_watch_font(FontHandle handle, const char* logical_name, u32 px_size) {
    if (g_font_count >= k_max_watched_fonts) {
        log_error("hot_reload: ya se vigilan %u fuentes, '%s' se queda fuera",
                  k_max_watched_fonts, logical_name);
        return;
    }
    WatchedFont& w = g_fonts[g_font_count];
    w.handle       = handle;
    w.px_size      = px_size;
    std::snprintf(w.logical_name, sizeof(w.logical_name), "%s", logical_name);
    // Las fuentes no se hornean todavia (M13): el archivo que se vigila es el mismo que
    // consume el juego, asi que su ruta real sale del propio backend suelto.
    if (!pak_resolve_loose_path(logical_name, w.source_path, sizeof(w.source_path))) {
        log_error("hot_reload: '%s' no se puede vigilar con un .pak montado", logical_name);
        return;
    }
    w.last_mtime = file_mtime_ns(w.source_path);
    g_font_count += 1;
}

void hot_reload_update(f32 dt) {
    g_timer += dt;
    if (g_timer < k_check_interval_s) {
        return;
    }
    g_timer = 0.0f;

    for (u32 i = 0; i < g_font_count; ++i) {
        WatchedFont& w     = g_fonts[i];
        i64          mtime = file_mtime_ns(w.source_path);
        if (mtime == 0 || mtime == w.last_mtime) {
            continue;
        }
        w.last_mtime = mtime;
        if (text_reload_font(w.handle, w.logical_name, w.px_size)) {
            // Los glifos ya rasterizados son de la version anterior (la clave del cache no
            // incluye la generacion, ver glyph_cache.h): hay que descartarlos o se
            // seguirian dibujando los viejos.
            glyph_cache_invalidate_font(w.handle);
            log_info("hot_reload: '%s' recargada en caliente", w.logical_name);
        }
    }

    i64 png_mtime = png_dir_newest_mtime();
    if (png_mtime != 0 && png_mtime != g_png_newest_mtime) {
        g_png_newest_mtime = png_mtime;
        log_info("hot_reload: cambio en assets_src/png/, re-empaquetando el atlas");
        rebake_atlas_and_reload();
    }
}

#else  // !VN_DEBUG

// En Ship no hay nada que vigilar: el .pak es inmutable (SPEC.md #7.4, "hot reload en
// Debug y Dev"). Definiciones vacias para no llenar los llamantes de #if.
void hot_reload_init() {}
void hot_reload_watch_font(FontHandle, const char*, u32) {}
void hot_reload_update(f32) {}

#endif  // VN_DEBUG
