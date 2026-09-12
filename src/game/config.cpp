#include "game/config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "base/log.h"
#include "game/locales.h"

Config g_config;

namespace {

constexpr const char* k_path = "config.ini";

// Parser minimo y TOTAL: nunca falla, nunca reporta un error de sintaxis. Lo que no entiende
// lo ignora y deja el valor por defecto. Es lo correcto para un archivo que el jugador edita
// a mano: una coma de mas no puede dejarlo sin poder jugar.
//
// No se usa una libreria de INI (seria una dependencia nueva, SPEC.md #3) ni <regex>: son
// pares clave=valor en secciones, y eso es un bucle sobre lineas.
void trim(char* s) {
    usize len = std::strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' ' ||
                        s[len - 1] == '\t')) {
        s[--len] = '\0';
    }
    usize start = 0;
    while (s[start] == ' ' || s[start] == '\t') {
        start += 1;
    }
    if (start > 0) {
        std::memmove(s, s + start, len - start + 1);
    }
}

f32 parse_volume(const char* value) {
    f32 v = static_cast<f32>(std::atof(value));
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return v;
}

}  // namespace

void config_load() {
    std::FILE* f = std::fopen(k_path, "rb");
    if (f == nullptr) {
        // Primera ejecucion: no es un error, se queda todo por defecto.
        return;
    }

    char line[256];
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';' || line[0] == '[') {
            continue;  // vacia, comentario o cabecera de seccion
        }
        char* eq = std::strchr(line, '=');
        if (eq == nullptr) {
            continue;
        }
        *eq         = '\0';
        char* key   = line;
        char* value = eq + 1;
        trim(key);
        trim(value);

        if (std::strcmp(key, "locale") == 0) {
            // Por ID, no por indice (ver config.h y game/locales.h).
            g_config.locale_index = locale_index_from_id(value);
        } else if (std::strcmp(key, "fullscreen") == 0) {
            g_config.fullscreen = std::strcmp(value, "1") == 0 ||
                                   std::strcmp(value, "true") == 0;
        } else if (std::strcmp(key, "master") == 0) {
            g_config.bus_volume[0] = parse_volume(value);
        } else if (std::strcmp(key, "music") == 0) {
            g_config.bus_volume[1] = parse_volume(value);
        } else if (std::strcmp(key, "sfx") == 0) {
            g_config.bus_volume[2] = parse_volume(value);
        } else if (std::strcmp(key, "voice") == 0) {
            g_config.bus_volume[3] = parse_volume(value);
        }
    }
    std::fclose(f);
}

void config_save() {
    std::FILE* f = std::fopen(k_path, "wb");
    if (f == nullptr) {
        log_error("config_save: no se pudo escribir '%s'", k_path);
        return;
    }
    u32 locale = g_config.locale_index < k_locale_count ? g_config.locale_index : 0;
    std::fprintf(f,
                 "# Preferencias de vne. Se puede editar a mano; lo que no se entienda se\n"
                 "# ignora y se queda el valor por defecto.\n"
                 "[locale]\n"
                 "locale=%s\n"
                 "\n"
                 "[video]\n"
                 "fullscreen=%d\n"
                 "\n"
                 "[audio]\n"
                 "master=%.2f\n"
                 "music=%.2f\n"
                 "sfx=%.2f\n"
                 "voice=%.2f\n",
                 k_locales[locale].id, g_config.fullscreen ? 1 : 0,
                 static_cast<double>(g_config.bus_volume[0]),
                 static_cast<double>(g_config.bus_volume[1]),
                 static_cast<double>(g_config.bus_volume[2]),
                 static_cast<double>(g_config.bus_volume[3]));
    std::fclose(f);
}
