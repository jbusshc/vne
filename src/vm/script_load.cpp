#include "vm/script_load.h"

#include <cstring>

#include "vfs/pak.h"
#include "core/heap_guard.h"
#include "core/log.h"

namespace {
// Deben coincidir exactamente con src/script/compiler.cpp (write_vnc). Duplicados a
// proposito: script/ es una herramienta offline que vm/ (runtime) no debe enlazar.
constexpr u32 k_vnc_magic   = 0x53434E56u;  // 'VNCS'
constexpr u32 k_vnc_version = 6u;           // M14: fuera las tablas de nombres por guion
}  // namespace

ScriptLoadResult script_load(const char* logical_name, Arena* arena, CompiledScript* out) {
    *out = CompiledScript{};

    // M11: ya no abre directamente por ruta de archivo -- se resuelve a traves del
    // backend activo (directorio suelto o .pak, ver vfs/pak.h). pak_resolve_into_arena
    // copia a `arena` en backend suelto (para que el buffer viva tanto como la escena,
    // ver el comentario en pak.h) o devuelve el puntero residente sin copiar en backend
    // empaquetado.
    //
    // Leer el archivo pasa por SDL, que asigna (13 veces, medido en M15 reproduciendo la
    // sesion de raton que pisa un trigger). Cargar el guion de un trigger ocurre DENTRO del
    // frame desde M9 y nadie lo habia visto: la sesion grabada de teclado no llegaba a pisar
    // ninguno. Misma excepcion acotada que ADR-0035, y en el mismo sitio que la de
    // catalog_load —dentro y no en el llamante— para que valga igual para los cuatro
    // llamantes de script_load, dos de los cuales corren en el frame.
    const u8* bytes = nullptr;
    usize     size  = 0;
    heap_guard_suspend();
    bool found = pak_resolve_into_arena(logical_name, arena, &bytes, &size);
    heap_guard_resume();
    if (!found) {
        log_error("script_load: no se encontro '%s'", logical_name);
        return ScriptLoadResult::NotFound;
    }

    constexpr usize k_header_size = 6 * sizeof(u32);
    if (size < k_header_size) {
        log_error("script_load: '%s' es demasiado pequeno para ser un .vnc", logical_name);
        return ScriptLoadResult::BadFormat;
    }

    u32 header[6];
    std::memcpy(header, bytes, sizeof(header));
    u32 magic               = header[0];
    u32 version             = header[1];
    u32 cmd_count           = header[2];
    u32 string_pool_size    = header[3];
    u32 label_count         = header[4];
    u32 choice_option_count = header[5];

    if (magic != k_vnc_magic) {
        log_error("script_load: '%s' no es un .vnc valido (magic incorrecto)", logical_name);
        return ScriptLoadResult::BadFormat;
    }
    if (version != k_vnc_version) {
        log_error("script_load: '%s' tiene version %u, se esperaba %u", logical_name, version,
                  k_vnc_version);
        return ScriptLoadResult::BadFormat;
    }

    usize offset = k_header_size;
    usize expected_size = offset + static_cast<usize>(cmd_count) * sizeof(Cmd) +
                          static_cast<usize>(string_pool_size) +
                          static_cast<usize>(label_count) * sizeof(ScriptLabel) +
                          static_cast<usize>(choice_option_count) * sizeof(ChoiceOption);
    if (expected_size > size) {
        log_error("script_load: '%s' esta truncado", logical_name);
        return ScriptLoadResult::BadFormat;
    }

    out->cmds      = reinterpret_cast<const Cmd*>(bytes + offset);
    out->cmd_count = cmd_count;
    offset += static_cast<usize>(cmd_count) * sizeof(Cmd);

    out->string_pool      = reinterpret_cast<const char*>(bytes + offset);
    out->string_pool_size = string_pool_size;
    offset += string_pool_size;

    // ScriptLabel{name_hash,pc} tiene el mismo layout binario que CompiledLabel
    // (script/compiler.h): ambos son dos u32 seguidos, sin relleno. Duplicado a
    // proposito por la misma razon que el resto de este archivo.
    out->labels      = reinterpret_cast<const ScriptLabel*>(bytes + offset);
    out->label_count = label_count;
    offset += static_cast<usize>(label_count) * sizeof(ScriptLabel);

    out->choice_options       = reinterpret_cast<const ChoiceOption*>(bytes + offset);
    out->choice_option_count = choice_option_count;

    return ScriptLoadResult::Ok;
}
