
#ifndef PART345_COMP_H
#define PART345_COMP_H

#include "nds_types.h"

u32 part345_decompress (u8* dest, u8* src);
int decomp_firmware(unsigned char* in_data);


// level
//   0 = store
//   1 = compress
//   2 = compress + optimise
struct _ARGS_VPK_COMPRESS {
	char filelog[256];
	u8 level;
	u8 method;
	u32 lz_move_max;
};

struct _ARGS_VPK_DECOMPRESS {
	char filelog[256];
};

typedef struct _ARGS_VPK_COMPRESS ARGS_VPK_COMPRESS;
typedef struct _ARGS_VPK_DECOMPRESS ARGS_VPK_DECOMPRESS;

u32 decompress_part345(u8 *dst, u8 *src);
u32 compress_part345(u8 *dst, u8 *src, u32 size);

u32 getCompressedPart345Size(u8 *src);

#endif
