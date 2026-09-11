#include "script/asset_validate.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "base/types.h"

namespace {

constexpr u32 k_atlas_bin_magic   = 0x54414E56u;  // 'VNAT'
constexpr u32 k_atlas_bin_version = 3;

// Mismo layout que AtlasEntry en tools/bake/main.cpp y en src/gfx/atlas.cpp. Duplicado a
// proposito, igual que el resto de formatos de este proyecto: quien escribe, quien lee en
// runtime y quien valida offline son tres binarios distintos.
struct AtlasEntry {
    u64 name_hash;
    u32 name_offset;
    u16 x, y, w, h;
    u8  _pad[4];
};
static_assert(sizeof(AtlasEntry) == 24);

}  // namespace

std::string asset_sprite_name_for_actor(const std::string& actor, const std::string& pose) {
    return "actor_" + actor + "_" + pose;
}

std::string asset_sprite_name_for_background(const std::string& bg) {
    return "bg_" + bg;
}

bool AssetRegistry::contains(const std::string& name) const {
    return std::find(sprite_names.begin(), sprite_names.end(), name) != sprite_names.end();
}

bool AssetRegistry::load_from_atlas_bin(const char* path, AssetRegistry* out) {
    out->sprite_names.clear();

    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }
    std::fseek(file, 0, SEEK_END);
    long file_size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (file_size <= 0) {
        std::fclose(file);
        return false;
    }
    std::vector<u8> bytes(static_cast<usize>(file_size));
    usize           read = std::fread(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    if (read != bytes.size()) {
        return false;
    }

    constexpr usize k_header_size = sizeof(u32) * 6;
    if (bytes.size() < k_header_size) {
        return false;
    }
    u32 header[6];
    std::memcpy(header, bytes.data(), k_header_size);
    if (header[0] != k_atlas_bin_magic || header[1] != k_atlas_bin_version) {
        return false;
    }

    u32   count          = header[4];
    u32   name_pool_size = header[5];
    usize needed = k_header_size + static_cast<usize>(count) * sizeof(AtlasEntry) + name_pool_size;
    if (bytes.size() < needed) {
        return false;
    }

    const char* pool = reinterpret_cast<const char*>(bytes.data() + k_header_size +
                                                      static_cast<usize>(count) * sizeof(AtlasEntry));
    out->sprite_names.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        AtlasEntry entry{};
        std::memcpy(&entry, bytes.data() + k_header_size + static_cast<usize>(i) * sizeof(AtlasEntry),
                    sizeof(AtlasEntry));
        if (entry.name_offset < name_pool_size) {
            out->sprite_names.emplace_back(pool + entry.name_offset);
        }
    }
    return true;
}

void validate_asset_names(const std::vector<ParsedInstr>& instructions,
                           const std::string& file_name, const AssetRegistry& registry,
                           std::vector<ParseError>* out_errors) {
    for (const ParsedInstr& instr : instructions) {
        if (instr.kind == InstrKind::Show) {
            std::string sprite = asset_sprite_name_for_actor(instr.actor, instr.pose);
            if (!registry.contains(sprite)) {
                out_errors->push_back(ParseError{
                    file_name, instr.line,
                    "actor o pose desconocidos: '" + instr.actor + "' '" + instr.pose +
                        "' (no hay ningun sprite '" + sprite + "' en el atlas)"});
            }
        } else if (instr.kind == InstrKind::Bg) {
            std::string sprite = asset_sprite_name_for_background(instr.bg);
            if (!registry.contains(sprite)) {
                out_errors->push_back(
                    ParseError{file_name, instr.line,
                               "fondo desconocido: '" + instr.bg + "' (no hay ningun sprite '" +
                                   sprite + "' en el atlas)"});
            }
        }
    }
}
