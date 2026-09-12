#pragma once
#include "core/types.h"

// CRC32 estandar (IEEE 802.3, el mismo que zip/png/ethernet), para el checksum del
// bloque de GameState en el formato .vnsave (SPEC.md #8.3).
u32 crc32(const void* data, usize size);
