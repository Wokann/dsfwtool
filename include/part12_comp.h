
#ifndef PART12_COMP_H
#define PART12_COMP_H

#include "nds_types.h"

int decrypt_decompress (u8* src, u8* *dest);
int decompress (u8* src, u8* *dest);

u32 decompressLZ77(u8 *dst,u8 *src);
u32 getCompressedLZ77Size(u8 *src);
u32 compressLZ77(u8 *dst, u8 *src, u32 size);

#endif
