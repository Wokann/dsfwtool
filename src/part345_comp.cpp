#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "part345_comp.h"
#include "bitstream.h"
#include "tree.h"

u32 ror_u32 (u32 val, int bits) {
	return (val >> bits) | (val << (32 - bits));
}

u32 get_u32 (u8* data, int offset) {
	return (data[offset+0] << 0) | (data[1] << 8) | (data[offset+2] << 16) | (data[offset+3] << 24);
}

// Big endian version
u32 get_u32_be (u8* data, int offset) {
	return (data[offset+0] << 24) | (data[offset+1] << 16) | (data[offset+2] << 8) | (data[offset+3] << 0);
}

void put_u32 (u8* data, int offset, u32 val) {
	data [offset + 0] = (u8) (val >>  0);
	data [offset + 1] = (u8) (val >>  8);
	data [offset + 2] = (u8) (val >> 16);
	data [offset + 3] = (u8) (val >> 24);
}

void put_u16 (u8* data, int offset, u16 val) {
	data [offset + 0] = (u8) (val >>  0);
	data [offset + 1] = (u8) (val >>  8);
}


u32 byteswap_u32 (u32 val) {
	u32 temp;
	temp = (val ^ ror_u32 (val, 16)) & 0xff00ffff;
	val = ror_u32 (val, 8);
	val = val ^ (temp >> 8);
	return val;
}
	
typedef struct {
	u32 shift;	
	u32 dat1;	
	u8* addr;	
	u32 data;	
	u32 dat4;	
} DATA_BITS;

DATA_BITS data_bits_0;		// 0x023def20
DATA_BITS data_bits_1;		// 0x023def24

u16 data_table_0[0x100];	// 0x023def60 size 0x200 bytes
u16 data_table_1[0x800];	// 0x023e0160 size 0x1000 bytes
u16 data_table_2[0x800];	// 0x023df160 size 0x1000 bytes

u16 data_table_3[0x2000];	// 0x023e1160 size 0x4000 bytes
u16 data_table_4[0x2000];	// 0x023e5160 size 0x4000 bytes


u8 get_bit (DATA_BITS* data_bits) {
	u8 bit;
	bit = data_bits->data >> 31;
	if (data_bits->shift == 31) {
		data_bits->data = get_u32_be (data_bits->addr, 0);
		data_bits->addr += 4;
		data_bits->shift = 0;
	} else {
		data_bits->data <<= 1;
		data_bits->shift += 1;
	}
	return bit;
}

u32 get_bits (DATA_BITS* data_bits, u32 num_bits) {
	u32 retval = data_bits->data >> (32 - num_bits);
	u32 temp;
	if ((data_bits->shift + num_bits) == 32) {
		data_bits->data = get_u32_be (data_bits->addr, 0);
		data_bits->addr += 4;
		data_bits->shift = 0;
	} else if ((data_bits->shift + num_bits) < 32) {
		data_bits->data <<= num_bits;
		data_bits->shift += num_bits;
	} else {	// 32 < (data_bits->shift + num_bits) < 64
		temp = get_u32_be (data_bits->addr, 0);
		data_bits->addr += 4;
		retval |= temp >> (64 - (data_bits->shift + num_bits));
		data_bits->shift += (num_bits - 32);
		data_bits->data = temp << data_bits->shift;
	}
	return retval;
}

//fw_decompress
u32 part345_decompress (u8* dest, u8* src) {
	u32 size, decompressed_size;
	u32 offset;
	u32 bits;
	u16 data;
	u32 temp_data;
	bool loop;
	u16* temp_table;
	
	size = get_u32_be (src, 4) & 0x00ffffff;
	decompressed_size = size;

	if (dest == NULL) {
		return decompressed_size;
	}
	
	offset = get_u32_be (src, 8);
	
	data_bits_0.shift = 0;
	data_bits_0.dat1 = 0x200;
	data_bits_0.addr = src + 12;
	data_bits_0.data = 0;

	data_bits_1.shift = 0;
	data_bits_1.dat1 = 0x800;
	data_bits_1.addr = src + offset;
	data_bits_1.data = 0;
	
	
	get_bits (&data_bits_0, 0x20);
	get_bits (&data_bits_1, 0x20);
	
	// Build the first tree
	temp_table = data_table_0;
	data = data_bits_0.dat1;
 	loop = true;
	while (loop) {
		while ((bits = get_bit (&data_bits_0)) != 0) {
			*(temp_table++) = (data | 0x8000);
			*(temp_table++) = (data | 0x4000);
			data += 1;
		}
		
		bits = get_bits (&data_bits_0, 9);

		do {
			temp_data = *(--temp_table);
			if ((temp_data & 0x8000) == 0) {
				data_table_1[temp_data & 0x3FFF] = (u16)bits;	
				break;
			} 
			data_table_2[temp_data & 0x3FFF] = (u16)bits;	
			bits = temp_data & 0x3FFF;
			if (temp_table == data_table_0) {
				loop = false;
				break;
			}
		} while (temp_table != data_table_0);
	}
	data_bits_0.dat4 = bits;	
	
	// Build the second tree
	temp_table = data_table_0;
	data = data_bits_1.dat1;
	loop = true;
	while (loop) {
		while ((bits = get_bit (&data_bits_1)) != 0) {
			*(temp_table++) = (data | 0x8000);
			*(temp_table++) = (data | 0x4000);
			data += 1;
		}
		
		bits = get_bits (&data_bits_1, 11);
	
		do {
			temp_data = *(--temp_table);		
			if ((temp_data & 0x8000) == 0) {
				data_table_3[temp_data & 0x3FFF] = bits;	
				break;
			} 
			data_table_4[temp_data & 0x3FFF] = bits;		
			bits = temp_data & 0x3FFF;
			if (temp_table == data_table_0) {
				loop = false;
				break;
			}
		} while (temp_table != data_table_0);
	}
	data_bits_1.dat4 = bits;

	// Decompress the data
	while (size > 0) {
		data = data_bits_0.dat4;
	
		while (data >= 0x200) {
			bits = get_bit (&data_bits_0);
			if (bits == 0) {
				data = data_table_1[data];	
			} else {
				data = data_table_2[data];	
			}
		}
		
		if (data < 0x100) {
			*(dest++) = (u8)data;
			size--;
		} else {
			temp_data = data_bits_1.dat4; 
			
			while ( temp_data >= 0x800) {
				bits = get_bit(&data_bits_1); 
				if (bits == 0) {
					temp_data = data_table_3[temp_data];
				} else {
					temp_data = data_table_4[temp_data];
				}
			}
			
			u32 copy_length = data - 253;
			u8* copy_source = (dest - temp_data) - 1;
			size -= copy_length;
			
			while (copy_length != 0) {
				*(dest++) = *(copy_source++);
				copy_length--;
			}
		}
	}
	
	return decompressed_size;
}


typedef struct _DPV_TABLE {
	u32 depth;
	u32 path;
	u32 value;
} DPV_TABLE, *PDPV_TABLE;

static void vpk_tree_save(PNODE node, BITSTREAM *bs, u32 bitlen);

static void vpk_tree_save(PNODE node, BITSTREAM *bs, u32 bitlen) {
	if(node) {
		if((node->left) || (node->right)) {
			bitstream_write(bs, 1, 1);
			if(node->left) vpk_tree_save(node->left, bs, bitlen);
			if(node->right) vpk_tree_save(node->right, bs, bitlen);
		}
		else {
			bitstream_write(bs, 1, 0);
			bitstream_write(bs, bitlen, node->value);
		}
	}
}

static PNODE build_tree_from_freq_table(u32 *freq, u32 items) {
	u32 i, weight, index, bits;
	PNODE nodes[0x800+0x1000], tree, node1, node2, node;
	
	(void)bits;
	
	// init
	memset(&nodes[0], 0, sizeof(nodes));
	tree = NULL;
	bits = 0;
	
	// create nodes
	for(i = 0; i < items; i++) {
		weight = freq[i];
		if(weight != 0) nodes[i] = node_create(NULL, NULL, i, weight);
	}
	
	// build tree
	index = items;
	while(1) {
		// find 2 nodes with the smallest weight
		node1 = NULL;
		node2 = NULL;
		for(i = 0; i < index; i++) {
			node = nodes[i];
			if((!node) || (node->weight == 0)) continue;
			if(!node1) {
				node1 = node;
			}
			else {
				if(!node2) {
					node2 = node;
				}
				else {
					if((node->weight < node1->weight) || (node->weight < node2->weight)) {
						if(node1->weight > node2->weight) node1 = node; else node2 = node;  
					}
				}
			}
		}
		
		if((node1) && (node2)) {
			// create parent node
			nodes[index++] = node_create(node1, node2, 0, node1->weight + node2->weight);
			node1->weight = 0;
			node2->weight = 0;
		}
		else {
			// get root node
			if(node1 != NULL) tree = node1; else tree = node2;
			break;
		}
	}
	
	return tree;
}

static void tree_get_depth_and_path_for_value( PNODE node, u32 value, u32 depth, u32 path, int *depthx, int *pathx) {
	if(node) {
		if((node->left) || (node->right)) {
			if(node->left) tree_get_depth_and_path_for_value( node->left, value, depth+1, (path<<1)+0, depthx, pathx);
			if(node->right) tree_get_depth_and_path_for_value( node->right, value, depth+1, (path<<1)+1, depthx, pathx);
		}
		else {
			if(node->value == value) {
				*depthx = depth;
				*pathx = path;
			}
		}
	}
}

static void tree_to_dpv(PNODE node, DPV_TABLE* dpv, u32 items, bool ereader) {
	u32 last;
	int depth, path, i;
	
	last = 0;
	memset(dpv, 0, sizeof(DPV_TABLE) * items);
	for(i = items - 1; i >= 0; i--) {
		if((ereader) && (i == 0)) continue;
		depth = -1;
		path = -1;
		tree_get_depth_and_path_for_value(node, i, 0, 0, &depth, &path);
		if(depth != -1) {
			dpv[i].depth = depth;
			dpv[i].path  = path;
			dpv[i].value = i;
			last = i;
		}
		else {
			if(ereader) {
				dpv[i].depth = dpv[last].depth;
				dpv[i].path  = dpv[last].path;
				dpv[i].value = dpv[last].value;
			}
		}
	}
}

typedef struct _DATA_DEC {
	u16 unk1;
	u32 value;
	u32 unk2;
	u32 *table[2];
	u8  bitlen;
	BITSTREAM bitstream;
} DATA_DEC, *PDATA_DEC;

DATA_DEC datadec[2];

static u32 stream_read_bits(u32 bits, u32 sel) {
	return bitstream_read(&datadec[sel].bitstream, bits);
}

static u32 make_tree(u8 *src, u32 sel) {
	u32 r0, r2, pos;
	u16 temp[256], r8, r9;
	
	r8 = r9 = datadec[sel].unk1;
	pos = 0;
	
loc_23285B4:
	r0 = stream_read_bits(1, sel);
	if(r0 != 0) {
		temp[pos++] = r9 | 0x8000;
		temp[pos++] = r9 | 0x4000;
		r9++;
		r8++;
		goto loc_23285B4;
	}
	
	r0 = stream_read_bits( datadec[sel].bitlen, sel);
	do {
		pos--;
		r2 = temp[pos];
		if(r2 & 0x8000) {
			r2 = r2 & 0x3FFF;
			*(datadec[sel].table[1] + r2) = r0;
			r0 = r2;
		}
		else {
			r2 = r2 & 0x3FFF;
			*(datadec[sel].table[0] + r2) = r0;
			r9 = r8;
			goto loc_23285B4;
		}
	} while(pos != 0);
	
	datadec[sel].unk2 = r0;
	
	return r0;
}

u32 decompress_part345(u8 *dst, u8 *src) {
	u32 r0, pos, x1, x2, sizedec, posdst, len, offset;
	
	// decompressed size
	sizedec = (src[5] << 16) | (src[6] << 8) | (src[7] << 0);
	if(!dst) return sizedec;
	
	// init 1st stream (contains lz single bytes or lz lengths)
	bitstream_clear(&datadec[0].bitstream);
	offset = 12;
	datadec[0].bitstream.ptr = src + offset;
	datadec[0].unk1 = 0x200;
	datadec[0].value = 0;
	datadec[0].bitlen = 9;
	datadec[0].table[0] = (u32*)malloc(0x1000);
	datadec[0].table[1] = (u32*)malloc(0x1000);
	make_tree(src, 0);
	
	// init 2nd stream (contains lz distance values)
	bitstream_clear(&datadec[1].bitstream);
	offset = (src[8] << 24) | (src[9] << 16) | (src[10] << 8) | (src[11] << 0);
	datadec[1].bitstream.ptr = src + offset;
	datadec[1].unk1 = 0x800;
	datadec[1].value = 0;
	datadec[1].bitlen = 11;
	datadec[1].table[0] = (u32*)malloc(0x4000);
	datadec[1].table[1] = (u32*)malloc(0x4000);
	make_tree(src, 1);
	
	r0 = sizedec;
	posdst = 0;
	while(posdst < sizedec) {
		x1 = datadec[0].unk2;
		while(x1 >= 0x200) {
			r0 = stream_read_bits(1, 0);
			x1 = *(datadec[0].table[r0] + x1);
		}
		if(x1 < 0x100) {
			dst[posdst++] = (u8)x1;
		}
		else {
			x2 = datadec[1].unk2;
			while(x2 >= 0x800) {
				r0 = stream_read_bits(1, 1);
				x2 = *(datadec[1].table[r0] + x2);
			}
			
			pos = posdst - x2 - 1;
			len = x1 - 0x100 + 3;
			while(len--) {
				dst[posdst++] = dst[pos++];
			}
		}
	}
	
	// cleanup
	free(datadec[0].table[0]);
	free(datadec[0].table[1]);
	free(datadec[1].table[0]);
	free(datadec[1].table[1]);
	
	return posdst;
}

static u32 lz_memcmp(u8 *mem1, u8 *mem2, u32 max) {
	u32 ret;
	ret = 0;
	while((*mem1++ == *mem2++) && (max-- > 0)) ret++;
	return ret;
}

static void lz_search(u8 *src, u32 pos, u32 srcmax, u32 *back, u32 *length) {
	u32 i, len;
	
	// init
	*back = 0;
	*length = 0;
	
	for(i = 0; i < 0x800; i++) {
		if(i < pos) {
			len = lz_memcmp(src + pos, src + pos - i - 1, srcmax - pos);
			if((len > 2) && (len > *length)) {
				*length = len;
				*back = i + 1; 
			}
		}
	}
}

static u32 swap32(u32 value) {
	u8 src[4], dst[4];
	memcpy(src, &value, 4);
	dst[0] = src[3];
	dst[1] = src[2];
	dst[2] = src[1];
	dst[3] = src[0];
	return dst[0] | dst[1] << 8 | dst[2] << 16 | dst[3] << 24;
}

u32 compress_part345(u8 *dst, u8 *src, u32 size) {
	u32 i, back, length;
	u32 *freq[2];
	PNODE tree[2];
	PDPV_TABLE dpv[2];
	BITSTREAM bs[2], bsdst;
	u32 bitlen[2], ret;
	
	bitlen[0] = 9;
	bitlen[1] = 11;
	
	// init bitstream
	for(i = 0; i < 2; i++) {
		bitstream_clear(&bs[i]);
		bs[i].ptr = (u8*)malloc(256 * 1024);
	}
	
	// alloc freq table
	for(i = 0; i < 2; i++) {
		freq[i] = (u32*)malloc((1 << bitlen[i]) * 4);
		memset(freq[i], 0, (1 << bitlen[i]) * 4);
	}
	
	// alloc depth-to-value table
	for(i = 0; i < 2; i++) {
		dpv[i] = (PDPV_TABLE)malloc(sizeof(DPV_TABLE) * (1 << bitlen[i]));
	}
	
	// lz compress (part 1)
	i = 0;
	while(i < size) {
		lz_search(src, i, size, &back, &length);
		if(back != 0) {
			if(length > 0x102) length = 0x102;
			*(freq[1] + back - 1) += 1;
			*(freq[0] + length - 3 + 0x100) += 1;
			//printf("(b) lz - pos1=%08X pos2=%08X len=%08X back=%08X\n", i - back, i, length, back);
			i += length;
		}
		else {
			*(freq[0] + src[i]) += 1;
			//printf("(b) lz - %02X\n", *(data + i));
			i++;
		}
	}
	
	// tree
	for(i = 0; i < 2; i++) {
		//printf("freq1\n"); for(i = 0; i < 0x200; i++) printf("%04X = %d\n", i, freq1[i]);
		tree[i] = build_tree_from_freq_table(freq[i], (1 << bitlen[i]));
		//printf("tree 1\n"); tree_fprint(stdout, tree[0]); printf("\r\n");
		tree_to_dpv(tree[i], dpv[i], (1 << bitlen[i]), false);
		//printf("dpv1\n"); for (i=0;i<(u32)(1 << bitlen[i]);i++) printf("%08X - depth=%08X path=%08X value=%08X\n", i, dpv[i]->depth, dpv[i]->path, dpv[i]->value);
		vpk_tree_save(tree[i], &bs[i], bitlen[i]);
		free_tree(tree[i]);
	}
	
	// lz compress (part 2)
	i = 0;
	while(i < size) {
		lz_search(src, i, size, &back, &length);
		if(back != 0) {
			if(length > 0x102) length = 0x102;
			bitstream_write(&bs[1], (dpv[1] + back - 1)->depth, (dpv[1] + back - 1)->path);
			bitstream_write(&bs[0], (dpv[0] + length - 3 + 0x100)->depth, (dpv[0] + length - 3 + 0x100)->path);
			//printf("(b) lz - pos1=%08X pos2=%08X len=%08X back=%08X\n", i - back, i, length, back);
			i += length;
		}
		else {
			bitstream_write(&bs[0], (dpv[0] + src[i])->depth, (dpv[0] + src[i])->path);
			//printf("(b) lz - %02X\n", *(data + i));
			i++;
		}
	}
	
	// finish bitstream (multiple of 4 byte)
	for(i = 0; i < 2; i++) {
		while(((bs[i].pos % 4) != 0) || (bs[i].bit != 0)) bitstream_write(&bs[i], 1, 0);
	}
	
	// combine data
	ret = 12 + bs[0].pos + bs[1].pos;
	memset(dst, 0, ret);
	bitstream_clear(&bsdst);
	bsdst.ptr = dst;
	bitstream_write(&bsdst, 8 * 3, swap32(ret * 4) >> 8);
	bitstream_write(&bsdst, 8 * 1, 0x80);
	bitstream_write(&bsdst, 8 * 1, 0x80);
	bitstream_write(&bsdst, 8 * 3, size);
	bitstream_write(&bsdst, 8 * 4, 12 + bs[0].pos);
	memcpy(dst + 12, bs[0].ptr, bs[0].pos);
	memcpy(dst + 12 + bs[0].pos, bs[1].ptr, bs[1].pos);
	// free depth-to-value table
	for(i = 0; i < 2; i++) free(dpv[i]);
	// free freq table
	for(i = 0; i < 2; i++) free(freq[i]);
	// free bitstream data
	for(i = 0; i < 2; i++) free(bs[i].ptr);
	
	return ret;
}

u32 getCompressedPart345Size(u8 *src) {
    u32 r0, x1, x2, sizedec, posdst, offset;
    
    // decompressed size
    sizedec = (src[5] << 16) | (src[6] << 8) | (src[7] << 0);

    // init 1st stream (contains lz single bytes or lz lengths)
    bitstream_clear(&datadec[0].bitstream);
    offset = 12;
    datadec[0].bitstream.ptr = src + offset;
    datadec[0].unk1 = 0x200;
    datadec[0].value = 0;
    datadec[0].bitlen = 9;
    datadec[0].table[0] = (u32*)malloc(0x1000);
    datadec[0].table[1] = (u32*)malloc(0x1000);
    make_tree(src, 0);

    // init 2nd stream (contains lz distance values)
    bitstream_clear(&datadec[1].bitstream);
    offset = (src[8] << 24) | (src[9] << 16) | (src[10] << 8) | (src[11] << 0);
    datadec[1].bitstream.ptr = src + offset;
    datadec[1].bitstream.pos = 0; // 初始化已读字节数
    datadec[1].unk1 = 0x800;
    datadec[1].value = 0;
    datadec[1].bitlen = 11;
    datadec[1].table[0] = (u32*)malloc(0x4000);
    datadec[1].table[1] = (u32*)malloc(0x4000);
    make_tree(src, 1);

    posdst = 0;
    while(posdst < sizedec) {
        x1 = datadec[0].unk2;
        while(x1 >= 0x200) {
            r0 = stream_read_bits(1, 0);
            x1 = *(datadec[0].table[r0] + x1);
        }

        if (x1 < 0x100) {
            posdst++;
        } else {
            x2 = datadec[1].unk2;
            while(x2 >= 0x800) {
                r0 = stream_read_bits(1, 1);
                x2 = *(datadec[1].table[r0] + x2);
            }
            posdst += x1 - 0x100 + 3;
        }
    }

    u32 effective_size = offset + datadec[1].bitstream.pos;
    printf("Effective compressed size: %08X bytes\n", effective_size);

    // cleanup
    free(datadec[0].table[0]);
    free(datadec[0].table[1]);
    free(datadec[1].table[0]);
    free(datadec[1].table[1]);

    return effective_size;
}
