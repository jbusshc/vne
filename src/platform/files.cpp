#include "platform/files.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>

bool file_exists(const char* path) {
    return SDL_GetPathInfo(path, nullptr);
}

i64 file_mtime_ns(const char* path) {
    SDL_PathInfo info{};
    if (!SDL_GetPathInfo(path, &info)) {
        return 0;
    }
    return static_cast<i64>(info.modify_time);
}

bool file_read_all(const char* path, Arena* arena, u8** out_data, usize* out_size) {
    *out_data = nullptr;
    *out_size = 0;

    usize size    = 0;
    void* sdl_buf = SDL_LoadFile(path, &size);
    if (sdl_buf == nullptr) {
        return false;
    }

    u8* dest = arena_alloc_n<u8>(arena, size);
    if (dest == nullptr) {
        SDL_free(sdl_buf);
        return false;
    }
    std::memcpy(dest, sdl_buf, size);
    SDL_free(sdl_buf);

    *out_data = dest;
    *out_size = size;
    return true;
}

namespace {

struct DirListCtx {
    const char*      extension;
    FileListCallback callback;
    void*            userdata;
};

SDL_EnumerationResult dir_list_callback(void* userdata, const char* dirname, const char* fname) {
    const DirListCtx* ctx = static_cast<const DirListCtx*>(userdata);

    if (fname[0] == '.') {
        return SDL_ENUM_CONTINUE;  // oculto, y de paso descarta "." y ".."
    }

    char full_path[1024];
    std::snprintf(full_path, sizeof(full_path), "%s/%s", dirname, fname);
    SDL_PathInfo info{};
    if (SDL_GetPathInfo(full_path, &info) && info.type != SDL_PATHTYPE_FILE) {
        return SDL_ENUM_CONTINUE;  // subdirectorios fuera (SPEC.md #7.4 no los pide)
    }

    usize name_len = std::strlen(fname);
    if (ctx->extension != nullptr && ctx->extension[0] != '\0') {
        usize ext_len = std::strlen(ctx->extension);
        if (name_len < ext_len ||
            std::strcmp(fname + (name_len - ext_len), ctx->extension) != 0) {
            return SDL_ENUM_CONTINUE;
        }
    }

    char name_no_ext[256];
    std::snprintf(name_no_ext, sizeof(name_no_ext), "%s", fname);
    char* dot = std::strrchr(name_no_ext, '.');
    if (dot != nullptr) {
        *dot = '\0';
    }
    ctx->callback(ctx->userdata, name_no_ext, fname);
    return SDL_ENUM_CONTINUE;
}

}  // namespace

void dir_list_by_extension(const char* dir, const char* extension, FileListCallback callback,
                            void* userdata) {
    DirListCtx ctx{extension, callback, userdata};
    SDL_EnumerateDirectory(dir, dir_list_callback, &ctx);
}
