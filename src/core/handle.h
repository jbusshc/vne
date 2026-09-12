#pragma once
#include "core/types.h"

// Handle opaco de 8 bytes para referenciar recursos vividos en un Pool. gen == 0 es el
// handle nulo. Nunca se resuelve a un puntero crudo fuera del ambito actual (ver Pool en
// pool.h). Los handles son POD y se serializan directos en el archivo de guardado.

template <typename Tag>
struct Handle {
    u32 index = 0;
    u32 gen   = 0;

    bool valid() const { return gen != 0; }
    bool operator==(const Handle&) const = default;
};

using TextureHandle = Handle<struct TextureTag>;
using FontHandle    = Handle<struct FontTag>;
using SoundHandle   = Handle<struct SoundTag>;
using ScriptHandle  = Handle<struct ScriptTag>;

static_assert(sizeof(TextureHandle) == 8);
static_assert(sizeof(FontHandle) == 8);
static_assert(sizeof(SoundHandle) == 8);
static_assert(sizeof(ScriptHandle) == 8);
