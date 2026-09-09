
#ifndef BITSTREAM_H
#define BITSTREAM_H

#include "nds_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    u8 *ptr;
    u8 bit;
    u8 byte;
    u32 pos;
} BITSTREAM;

void bitstream_clear(BITSTREAM *bs);
void bitstream_write(BITSTREAM *bs, u32 size, u32 data);
u32 bitstream_read(BITSTREAM *bs, u32 size);
u32 bitstream_peek(BITSTREAM *bs, u32 size);

#ifdef __cplusplus
}
#endif

#endif
