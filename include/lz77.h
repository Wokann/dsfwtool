
#ifndef LZ77_H
#define LZ77_H

#include "nds_types.h"
#include "get_data.h"

#define COMPRESSION_TYPE_LZ77 1

void Decompress_LZ77(GET_DATA get_data, u8* dest, int dest_size);

#endif
