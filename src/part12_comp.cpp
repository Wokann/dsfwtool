
#include <string.h>

#include "nds_types.h"
#include "part12_comp.h"

#define LZ_HEADER 0x10u
#define WINDOW_SIZE 4096
#define MAX_MATCH 18

u32 decompressLZ77(u8 *dst, const u8 *src) {
    u32 header;
    u32 decompSize;
    if (src == NULL) return 0;
    header = (u32)src[0] | ((u32)src[1] << 8) | ((u32)src[2] << 16) | ((u32)src[3] << 24);
	if (((header & 0xF0u) >> 4) != (LZ_HEADER >> 4)) return 0;
    decompSize = header >> 8;
    if (decompSize == 0) return 0;
	if(!dst) return decompSize;

    size_t inPos = 4;
    size_t outPos = 0;
    u8 flags = 0;
    int flagBits = 0;

    while (outPos < decompSize) {
        if (flagBits == 0) {
            flags = src[inPos++];
            flagBits = 8;
        }

        if ((flags & 0x80) == 0) {
            u8 val = src[inPos++];
            dst[outPos++] = val;

        } else {
            u8 b1 = src[inPos++];
            u8 b2 = src[inPos++];
            int length = (b1 >> 4) + 3;
            int displacement = ((b1 & 0xF) << 8) | b2;

            for (int k = 0; k < length && outPos < decompSize; k++) {
                int srcIdx = (int)outPos - (displacement + 1);
                u8 val = (srcIdx >= 0) ? dst[srcIdx] : 0;
                dst[outPos++] = val;
            }
        }

        flags <<= 1;
        flagBits--;
    }

    return decompSize;
}

u32 getCompressedLZ77Size(const u8 *src) {
    u32 header;
    u32 decompSize;
    if (src == NULL) return 0;
    header = (u32)src[0] | ((u32)src[1] << 8) | ((u32)src[2] << 16) | ((u32)src[3] << 24);
	if (((header & 0xF0u) >> 4) != (LZ_HEADER >> 4)) return 0;
    decompSize = header >> 8;
    if (decompSize == 0) return 0;

    size_t inPos = 4;
    size_t outPos = 0;
    u8 flags = 0;
    int flagBits = 0;

    while (outPos < decompSize) {
        if (flagBits == 0) {
            flags = src[inPos++];
            flagBits = 8;
        }
        if ((flags & 0x80) == 0) {
            inPos++;
            outPos++;
        } else {
            u8 b1 = src[inPos++];
            inPos++;
            int length = (b1 >> 4) + 3;
            outPos += length;
        }

        flags <<= 1;
        flagBits--;
    }

    return (u32)inPos;
}


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

        // Firmware streams prefer the most recent occurrence when several
        // back-references encode the same longest match.  The candidate list
        // is visited from oldest to newest, so equal lengths must replace the
        // previous candidate.
        if (len >= bestLen) { bestLen = len; bestOffset = (u16)(ptr - search); }
        idx = win->next[idx];
    }

    if (bestLen < 3) return 0;

    *offset = bestOffset;
    return bestLen;
}

u32 compressLZ77(u8 *dst, const u8 *src, u32 size) {
    if (!src || !dst || size == 0 || size > 0xFFFFFFu) return 0;

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
