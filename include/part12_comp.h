
#ifndef PART12_COMP_H
#define PART12_COMP_H

#include "nds_types.h"

#ifdef __cplusplus
extern "C" {
#endif

u32 decompressLZ77(u8 *dst, const u8 *src);
u32 getCompressedLZ77Size(const u8 *src);
u32 compressLZ77(u8 *dst, const u8 *src, u32 size);

#ifdef __cplusplus
}
#endif

#endif
