#include "vm/script_load.h"

#include <cstdio>
#include <cstring>

#include "base/log.h"

namespace {
// Deben coincidir exactamente con src/script/compiler.cpp (write_vnc). Duplicados a
// proposito: script/ es una herramienta offline que vm/ (runtime) no debe enlazar.
constexpr u32 k_vnc_magic   = 0x53434E56u;  // 'VNCS'
constexpr u32 k_vnc_version = 1u;
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

    constexpr long k_header_size = 5 * static_cast<long>(sizeof(u32));
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

    u32 header[5];
    std::memcpy(header, bytes, sizeof(header));
    u32 magic            = header[0];
    u32 version          = header[1];
    u32 cmd_count        = header[2];
    u32 string_pool_size = header[3];
    u32 label_count      = header[4];
    (void)label_count;  // no hace falta en runtime: los saltos ya son pc directos

    if (magic != k_vnc_magic) {
        log_error("script_load: '%s' no es un .vnc valido (magic incorrecto)", path);
        return ScriptLoadResult::BadFormat;
    }
    if (version != k_vnc_version) {
        log_error("script_load: '%s' tiene version %u, se esperaba %u", path, version,
                  k_vnc_version);
        return ScriptLoadResult::BadFormat;
    }

    usize expected_size = static_cast<usize>(k_header_size) +
                          static_cast<usize>(cmd_count) * sizeof(Cmd) +
                          static_cast<usize>(string_pool_size);
    if (expected_size > static_cast<usize>(size)) {
        log_error("script_load: '%s' esta truncado", path);
        return ScriptLoadResult::BadFormat;
    }

    out->cmds             = reinterpret_cast<const Cmd*>(bytes + k_header_size);
    out->cmd_count        = cmd_count;
    out->string_pool      = reinterpret_cast<const char*>(bytes + k_header_size +
                                                       cmd_count * sizeof(Cmd));
    out->string_pool_size = string_pool_size;
    return ScriptLoadResult::Ok;
}
