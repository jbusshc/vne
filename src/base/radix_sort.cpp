#include "base/radix_sort.h"

#include <cstring>

void radix_sort_u64(RadixSortEntry* entries, u32 count, Arena* arena) {
    if (count == 0) {
        return;
    }
    RadixSortEntry* temp = arena_alloc_n<RadixSortEntry>(arena, count);
    u32             counts[256];
    for (u32 pass = 0; pass < 8; ++pass) {
        std::memset(counts, 0, sizeof(counts));
        u32 shift = pass * 8;
        for (u32 i = 0; i < count; ++i) {
            u8 bucket = static_cast<u8>((entries[i].key >> shift) & 0xFFu);
            counts[bucket] += 1;
        }
        u32 sum = 0;
        for (u32 b = 0; b < 256; ++b) {
            u32 c     = counts[b];
            counts[b] = sum;
            sum += c;
        }
        for (u32 i = 0; i < count; ++i) {
            u8 bucket            = static_cast<u8>((entries[i].key >> shift) & 0xFFu);
            temp[counts[bucket]] = entries[i];
            counts[bucket] += 1;
        }
        std::memcpy(entries, temp, count * sizeof(RadixSortEntry));
    }
}
