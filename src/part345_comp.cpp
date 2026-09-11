#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "part345_comp.h"
#include "bitstream.h"
#include "tree.h"

#ifndef PART345_LAZY_GAIN
#define PART345_LAZY_GAIN 1u
#endif

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


/* Equal children select the left child; an equal parent stays in place.
 * Removing only the first minimum and replacing the second with the parent
 * preserves the heap ordering observed in all 60 official P345 trees. */
static void huffman_sift_down(PNODE *heap, u32 count, u32 position) {
    PNODE value = heap[position];
    u32 child;
    while ((child = position * 2) <= count) {
        if (child < count && heap[child + 1]->weight < heap[child]->weight)
            child++;
        if (value->weight <= heap[child]->weight) break;
        heap[position] = heap[child];
        position = child;
    }
    heap[position] = value;
}

static PNODE build_tree_from_freq_table(u32 *freq, u32 items) {
    PNODE heap[0x801];
    u32 count = 0, i;
    for (i = 0; i < items; i++)
        if (freq[i]) heap[++count] = node_create(NULL, NULL, i, freq[i]);
    for (i = count / 2; i; i--) huffman_sift_down(heap, count, i);
    while (count > 1) {
        PNODE first = heap[1], second;
        heap[1] = heap[count--];
        huffman_sift_down(heap, count, 1);
        second = heap[1];
        heap[1] = node_create(first, second, 0, first->weight + second->weight);
        huffman_sift_down(heap, count, 1);
    }
    return count ? heap[1] : NULL;
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

static u32 make_tree(u32 sel) {
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
	make_tree(0);
	
	// init 2nd stream (contains lz distance values)
	bitstream_clear(&datadec[1].bitstream);
	offset = (src[8] << 24) | (src[9] << 16) | (src[10] << 8) | (src[11] << 0);
	datadec[1].bitstream.ptr = src + offset;
	datadec[1].unk1 = 0x800;
	datadec[1].value = 0;
	datadec[1].bitlen = 11;
	datadec[1].table[0] = (u32*)malloc(0x4000);
	datadec[1].table[1] = (u32*)malloc(0x4000);
	make_tree(1);
	
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
	while(max != 0 && *mem1 == *mem2) {
		mem1++;
		mem2++;
		max--;
		ret++;
	}
	return ret;
}

static void lz_search(u8 *src, u32 pos, u32 srcmax, u32 *back, u32 *length) {
	u32 i, len, max_length;
	
	// init
	*back = 0;
	*length = 0;
	
	max_length = srcmax - pos;
	if(max_length > 0x102) max_length = 0x102;
	for(i = 0; i < 0x800; i++) {
		if(i < pos) {
			len = lz_memcmp(src + pos, src + pos - i - 1, max_length);
			if((len > 2) && (len >= *length)) {
				*length = len;
				*back = i + 1; 
			}
		}
	}
}

static void lz_search_lazy(u8 *src, u32 pos, u32 srcmax, u32 *back, u32 *length, bool *commit_match) {
	u32 next_back, next_length;
	lz_search(src, pos, srcmax, back, length);
	if(*length > 0x102) *length = 0x102;
	if(*commit_match) {
		*commit_match = false;
		return;
	}
	if(*back == 0 || pos + 1 >= srcmax) return;
	lz_search(src, pos + 1, srcmax, &next_back, &next_length);
	if(next_length > 0x102) next_length = 0x102;
	if(next_back != 0 && next_length > *length + PART345_LAZY_GAIN) {
		*back = 0;
		*length = 0;
		*commit_match = true;
	}
}

static void ensure_decodable_tree(u32 *freq, u32 items) {
	u32 i, used, first;
	used = 0;
	first = items;
	for(i = 0; i < items; i++) {
		if(freq[i] != 0) {
			if(first == items) first = i;
			used++;
		}
	}
	if(used == 0) {
		freq[0] = 1;
		freq[1] = 1;
	}
	else if(used == 1) {
		freq[first == 0 ? 1 : 0] = 1;
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

/* The first stream's word alignment is part of the P345 format: its padded
   end is stored as the second stream's offset.  The final stream is
   different.  It only needs its last partially written byte to be flushed;
   physical word/component alignment belongs to the firmware packer. */
static void bitstream_finish_byte(BITSTREAM *stream) {
	while(stream->bit != 0) bitstream_write(stream, 1, 0);
}

static void bitstream_pad_to_word(BITSTREAM *stream) {
	bitstream_finish_byte(stream);
	while((stream->pos % 4) != 0) bitstream_write(stream, 8, 0);
}

u32 compress_part345(u8 *dst, u8 *src, u32 size) {
	u32 i, back, length;
	bool commit_match;
	u32 *freq[2];
	PNODE tree[2];
	PDPV_TABLE dpv[2];
	BITSTREAM bs[2], bsdst;
	u32 bitlen[2], effective_size, stored_size, stream_capacity;

	if(dst == NULL || src == NULL || size == 0 || size > 0xFFFFFFu) return 0;
	stream_capacity = size * 2u + 0x10000u;
	
	bitlen[0] = 9;
	bitlen[1] = 11;
	
	// init bitstream
	for(i = 0; i < 2; i++) {
		bitstream_clear(&bs[i]);
		bs[i].ptr = (u8*)malloc(stream_capacity);
		if(bs[i].ptr == NULL) {
			while(i != 0) free(bs[--i].ptr);
			return 0;
		}
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
	commit_match = false;
	while(i < size) {
		lz_search_lazy(src, i, size, &back, &length, &commit_match);
		if(back != 0) {
			if(length > 0x102) length = 0x102;
			*(freq[1] + back - 1) += 1;
			*(freq[0] + length - 3 + 0x100) += 1;
			i += length;
		}
		else {
			*(freq[0] + src[i]) += 1;
			i++;
		}
	}
	for(i = 0; i < 2; i++) ensure_decodable_tree(freq[i], (1u << bitlen[i]));
	
	// tree
	for(i = 0; i < 2; i++) {
		tree[i] = build_tree_from_freq_table(freq[i], (1 << bitlen[i]));
		tree_to_dpv(tree[i], dpv[i], (1 << bitlen[i]), false);
		vpk_tree_save(tree[i], &bs[i], bitlen[i]);
		free_tree(tree[i]);
	}
	
	// lz compress (part 2)
	i = 0;
	commit_match = false;
	while(i < size) {
		lz_search_lazy(src, i, size, &back, &length, &commit_match);
		if(back != 0) {
			if(length > 0x102) length = 0x102;
			bitstream_write(&bs[1], (dpv[1] + back - 1)->depth, (dpv[1] + back - 1)->path);
			bitstream_write(&bs[0], (dpv[0] + length - 3 + 0x100)->depth, (dpv[0] + length - 3 + 0x100)->path);
			i += length;
		}
		else {
			bitstream_write(&bs[0], (dpv[0] + src[i])->depth, (dpv[0] + src[i])->path);
			i++;
		}
	}
	
	/* The first stream ends at a format-defined word boundary.  For the second
	   stream, retain only its final partially occupied byte.  `stored_size`
	   remains word-aligned in the on-stream header so a firmware image writer
	   can materialize the required trailing zeroes separately. */
	bitstream_pad_to_word(&bs[0]);
	bitstream_finish_byte(&bs[1]);
	effective_size = 12 + bs[0].pos + bs[1].pos;
	stored_size = 12 + bs[0].pos + ((bs[1].pos + 3u) & ~3u);
	
	// combine data
	memset(dst, 0, effective_size);
	bitstream_clear(&bsdst);
	bsdst.ptr = dst;
	bitstream_write(&bsdst, 8 * 3, swap32(stored_size * 4) >> 8);
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
	
	return effective_size;
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
    make_tree(0);

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
    make_tree(1);

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

    // cleanup
    free(datadec[0].table[0]);
    free(datadec[0].table[1]);
    free(datadec[1].table[0]);
    free(datadec[1].table[1]);

    return effective_size;
}
