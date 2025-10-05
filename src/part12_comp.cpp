
#include <stdio.h>
#include <malloc.h>
#include <string.h>

#include "nds_types.h"
#include "get_data.h"
#include "get_encrypted_data.h"
#include "get_normal_data.h"
#include "lz77.h"
#include "part12_comp.h"

int decrypt_decompress (u8* src, u8* *dest) {
	GET_DATA get_data = get_encrypted_data;

	get_data.set_address (src);

	int compression_type = (get_data.get_u8() & 0xF0) >> 4;
	int decompressed_size = get_data.get_u8();
	decompressed_size |= get_data.get_u8() << 8;
	decompressed_size |= get_data.get_u8() << 16;

	*dest = (u8*) malloc (decompressed_size);

	switch (compression_type) {
		case COMPRESSION_TYPE_LZ77:
			Decompress_LZ77 (get_data, *dest, decompressed_size);
			break;
		default:
			printf ("CANNOT DECOMPRESS TYPE %d\n", compression_type);
			decompressed_size = 0;
			break;
	}

	return decompressed_size;
}

int decompress (u8* src, u8* *dest) {
	GET_DATA get_data = get_normal_data;

	get_data.set_address (src);

	int compression_type = (get_data.get_u8() & 0xF0) >> 4;
	int decompressed_size = get_data.get_u8();
	decompressed_size |= get_data.get_u8() << 8;
	decompressed_size |= get_data.get_u8() << 16;

	*dest = (u8*) malloc (decompressed_size);

	switch (compression_type) {
		case COMPRESSION_TYPE_LZ77:
			Decompress_LZ77 (get_data, *dest, decompressed_size);
			break;
		default:
			printf ("CANNOT DECOMPRESS TYPE %d\n", compression_type);
			decompressed_size = 0;
			break;
	}

	return decompressed_size;
}