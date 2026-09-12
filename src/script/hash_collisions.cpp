#include "script/hash_collisions.h"

bool HashCollisionCheck::add(const std::string& name, u32 id, std::string* out_previous) {
    auto it = by_id_.find(id);
    if (it == by_id_.end()) {
        by_id_.emplace(id, name);
        return true;
    }
    if (it->second == name) {
        return true;  // el mismo nombre otra vez no es una colision
    }
    *out_previous = it->second;
    return false;
}
