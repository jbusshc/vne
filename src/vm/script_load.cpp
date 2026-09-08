#include "vm/script_load.h"

#include <cstdio>
#include <cstring>

#include "base/log.h"

namespace {
// Deben coincidir exactamente con src/script/compiler.cpp (write_vnc). Duplicados a
// proposito: script/ es una herramienta offline que vm/ (runtime) no debe enlazar.
constexpr u32 k_vnc_magic   = 0x53434E56u;  // 'VNCS'
constexpr u32 k_vnc_version = 3u;           // M10: Cmd::say y ChoiceOption ganaron key_hash
}  // namespace

ScriptLoadResult script_load(const char* path, Arena* arena, CompiledScript* out) {
    *out = CompiledScript{};

    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        log_error("script_load: no se encontro '%s'", path);
        return ScriptLoadResult::NotFound;
    }
    std::fseek(file, 0, SEEK_END);
    long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);

    constexpr long k_header_size = 6 * static_cast<long>(sizeof(u32));
    if (size < k_header_size) {
        std::fclose(file);
        log_error("script_load: '%s' es demasiado pequeno para ser un .vnc", path);
        return ScriptLoadResult::BadFormat;
    }

    u8* bytes = arena_alloc_n<u8>(arena, static_cast<usize>(size));
    if (bytes == nullptr) {
        std::fclose(file);
        log_error("script_load: sin espacio en la arena para '%s' (%ld bytes)", path, size);
        return ScriptLoadResult::BadFormat;
    }
    usize read = std::fread(bytes, 1, static_cast<usize>(size), file);
    std::fclose(file);
    if (read != static_cast<usize>(size)) {
        log_error("script_load: lectura incompleta de '%s'", path);
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
        log_error("script_load: '%s' no es un .vnc valido (magic incorrecto)", path);
        return ScriptLoadResult::BadFormat;
    }
    if (version != k_vnc_version) {
        log_error("script_load: '%s' tiene version %u, se esperaba %u", path, version,
                  k_vnc_version);
        return ScriptLoadResult::BadFormat;
    }

    usize offset = static_cast<usize>(k_header_size);
    usize expected_size = offset + static_cast<usize>(cmd_count) * sizeof(Cmd) +
                          static_cast<usize>(string_pool_size) +
                          static_cast<usize>(label_count) * sizeof(ScriptLabel) +
                          static_cast<usize>(choice_option_count) * sizeof(ChoiceOption);
    if (expected_size > static_cast<usize>(size)) {
        log_error("script_load: '%s' esta truncado", path);
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
