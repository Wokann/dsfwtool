
#ifndef PART345_COMP_H
#define PART345_COMP_H

#include "nds_types.h"

#ifdef __cplusplus
extern "C" {
#endif

u32 decompress_part345(u8 *dst, u8 *src);
u32 compress_part345(u8 *dst, u8 *src, u32 size);
u32 getCompressedPart345Size(u8 *src);

#ifdef __cplusplus
}
#endif

#endif
