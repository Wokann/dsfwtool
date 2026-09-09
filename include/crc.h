
#ifndef CRC_H
#define CRC_H

#include "nds_types.h"

#ifdef __cplusplus
extern "C" {
#endif

u16 swiCRC(u16 crc, const u8 *data, u32 size);

#ifdef __cplusplus
}
#endif

#endif
