#include "game/locales.h"

#include <cstring>

u32 locale_index_from_id(const char* id) {
    if (id == nullptr) {
        return 0;
    }
    for (u32 i = 0; i < k_locale_count; ++i) {
        if (std::strcmp(k_locales[i].id, id) == 0) {
            return i;
        }
    }
    return 0;
}
