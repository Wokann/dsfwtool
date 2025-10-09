
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


u32 decompressLZ77(u8 *dst,u8 *src) {
    u32 header = src[0] | (src[1]<<8) | (src[2]<<16) | (src[3]<<24);
	int compressionType = (header & 0xF0) >> 4;
	if (compressionType != COMPRESSION_TYPE_LZ77) {
            printf("Cannot decompress type %d\n", compressionType);
            return 0;
	}
    int decompSize = header >> 8;
    if (decompSize <= 0) return 0;
	if(!dst)  return decompSize;

    u8 *tempBuf = (u8 *)malloc(decompSize);
    if (!tempBuf) return -1;

    size_t inPos = 4;
    size_t outPos = 0;
    u8 flags = 0;
    int flagBits = 0;

    u16 halfWord = 0;
    int halfFlag = 0;
    size_t halfAddr = 0;

    while (outPos < (size_t)decompSize) {
        if (flagBits == 0) {
            flags = src[inPos++];
            flagBits = 8;
        }

        if ((flags & 0x80) == 0) {
            u8 val = src[inPos++];
            tempBuf[outPos++] = val;

            if (!halfFlag) {
                halfWord = val;
                halfAddr = outPos - 1;
                halfFlag = 1;
            } else {
                halfWord |= (val << 8);
                *(u16 *)(dst + halfAddr) = halfWord;
                halfFlag = 0;
            }

        } else {
            u8 b1 = src[inPos++];
            u8 b2 = src[inPos++];
            int length = (b1 >> 4) + 3;
            int displacement = ((b1 & 0xF) << 8) | b2;

            for (int k = 0; k < length && outPos < (size_t)decompSize; k++) {
                int srcIdx = (int)outPos - (displacement + 1);
                u8 val = (srcIdx >= 0) ? tempBuf[srcIdx] : 0;
                tempBuf[outPos++] = val;

                if (!halfFlag) {
                    halfWord = val;
                    halfAddr = outPos - 1;
                    halfFlag = 1;
                } else {
                    halfWord |= (val << 8);
                    *(u16 *)(dst + halfAddr) = halfWord;
                    halfFlag = 0;
                }
            }
        }

        flags <<= 1;
        flagBits--;
    }

    if (halfFlag) {
        *(u16 *)(dst + halfAddr) = halfWord;
    }

    free(tempBuf);
    return decompSize;
}

u32 getCompressedLZ77Size(u8 *src) {
    u32 header = src[0] | (src[1]<<8) | (src[2]<<16) | (src[3]<<24);
	int compressionType = (header & 0xF0) >> 4;
	if (compressionType != COMPRESSION_TYPE_LZ77) {
            printf("Cannot decompress type %d\n", compressionType);
            return 0;
	}
    int decompSize = header >> 8;
    if (decompSize <= 0) return 0;
    if (!src) return -1;

    size_t inPos = 4;
    size_t outPos = 0;
    u8 flags = 0;
    int flagBits = 0;

    while (outPos < (size_t)decompSize) {
        if (flagBits == 0) {
            flags = src[inPos++];
            flagBits = 8;
        }
        if ((flags & 0x80) == 0) {
            inPos++;
            outPos++;
        } else {
            u8 b1 = src[inPos++];
            u8 b2 = src[inPos++];
            int length = (b1 >> 4) + 3;
            outPos += length;
        }

        flags <<= 1;
        flagBits--;
    }

    printf("Effective compressed size: %08X bytes\n", inPos);
    return (int)inPos;
}


#define LZ_HEADER 0x10
#define WINDOW_SIZE 4096
#define MAX_MATCH 18

typedef struct {
    s16 next[WINDOW_SIZE];
    s16 head[256];
    s16 tail[256];
    u16 pos;
    u16 len;
} HashWindow;

static void hashInit(HashWindow *win) {
    memset(win->head, -1, sizeof(win->head));
    memset(win->tail, -1, sizeof(win->tail));
    win->pos = 0;
    win->len = 0;
}

static void hashSlide(HashWindow *win, const u8 *ptr) {
    u8 val = *ptr;
    u16 idx = win->len < WINDOW_SIZE ? win->len : win->pos;

    if (win->len == WINDOW_SIZE) {
        u8 outVal = *(ptr - WINDOW_SIZE);
        s16 h = win->head[outVal];
        win->head[outVal] = win->next[h];
        if (win->head[outVal] == -1) win->tail[outVal] = -1;
        win->pos = (win->pos + 1) % WINDOW_SIZE;
    }

    if (win->tail[val] == -1) win->head[val] = idx;
    else win->next[win->tail[val]] = idx;
    win->tail[val] = idx;
    win->next[idx] = -1;

    if (win->len < WINDOW_SIZE) win->len++;
}

static void hashSlideN(HashWindow *win, const u8 *ptr, int n) {
    for (int i = 0; i < n; i++) hashSlide(win, ptr + i);
}

static u8 hashFind(HashWindow *win, const u8 *ptr, int remain, u16 *offset) {
    if (remain < 3) return 0;

    s16 idx = win->head[*ptr];
    u16 bestOffset = 0;
    u8 bestLen = 2;

    while (idx != -1) {
        const u8 *search = idx < win->pos ? ptr - win->pos + idx : ptr - win->len - win->pos + idx;
        if (search[1] != ptr[1] || search[2] != ptr[2]) { idx = win->next[idx]; continue; }
        if (ptr - search < 2) break;

        u8 len = 3;
        const u8 *p1 = search + 3, *p2 = ptr + 3;
        while (len < MAX_MATCH && p2 < ptr + remain && *p1 == *p2) { p1++; p2++; len++; }

        if (len > bestLen) { bestLen = len; bestOffset = (u16)(ptr - search); if (bestLen == MAX_MATCH) break; }
        idx = win->next[idx];
    }

    if (bestLen < 3) return 0;

    *offset = bestOffset;
    return bestLen;
}

u32 compressLZ77(u8 *dst, u8 *src, u32 size) {
    if (!src || !dst || size <= 0) return -1;

    HashWindow win;
    hashInit(&win);

    u32 outPos = 0;
    dst[outPos++] = LZ_HEADER;
    dst[outPos++] = (u8)(size & 0xFF);
    dst[outPos++] = (u8)((size >> 8) & 0xFF);
    dst[outPos++] = (u8)((size >> 16) & 0xFF);

    while (size > 0) {
        u8 flags = 0;
        u32 flagPos = outPos++;
        
        for (int bit = 0; bit < 8; bit++) {
            flags <<= 1;
            if (size <= 0) continue;

            u16 offset;
            u8 matchLen = hashFind(&win, src, size, &offset);
            if (matchLen) {
                flags |= 1;
                dst[outPos++] = (u8)((matchLen - 3) << 4 | ((offset - 1) >> 8));
                dst[outPos++] = (u8)((offset - 1) & 0xFF);
                hashSlideN(&win, src, matchLen);
                src += matchLen;
                size -= matchLen;
            } else {
                dst[outPos++] = *src;
                hashSlide(&win, src);
                src++;
                size--;
            }
        }
        dst[flagPos] = flags;
    }

    return outPos;
}

