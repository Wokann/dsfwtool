#define _CRT_SECURE_NO_WARNINGS 1
#define _CRT_NONSTDC_NO_WARNINGS 1

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define mkdir_dir(path) _mkdir(path)
#define access _access
#else
#include <unistd.h>
#define mkdir_dir(path) mkdir(path, 0755)
#endif

#include "nds_types.h"
#include "firmware.h"
#include "crc.h"
#include "encryption.h"
#include "part12_comp.h"
#include "part345_comp.h"

/*
 * dsfwtool is written as C99.  The project retains the historical .cpp
 * filenames, but the build compiles them as C.  The existing codec and
 * encryption modules are shared directly; this file provides a unified
 * command-line front end and firmware layout handling.
 */

#define DSFWTOOL_VERSION "1.0"
#define HEADER_BYTES FW_HEADER_DATA_SIZE
#define CAPACITY_UNIT FW_CAPACITY_UNIT
#define FLASHME_TRAILER FW_FLASHME_TRAILER_SIZE
#define PATH_BUFFER_SIZE 2048
#define MAX_HEADER_EDITS 32
#define MAX_RANGES 10
#define MAX_RELOCATED_COMPONENTS 7
#define WIFI_ACCESS_POINT_BYTES 0x400u
#define USER_SETTINGS_BYTES 0x200u
#define SETTINGS_TAIL_BYTES (WIFI_ACCESS_POINT_BYTES + USER_SETTINGS_BYTES)

typedef struct {
    u8 *data;
    size_t size;
} Blob;

typedef struct {
    u32 offsets[5];
    u32 spans[5];
    u32 effective_sizes[5];
    u32 alignments[5];

    int has_flashme;
    u32 flashme_header_offset;
    u32 flashme_offsets[2];
    u32 flashme_spans[2];
    u32 flashme_effective_sizes[2];
    u32 flashme_alignments[2];
} FirmwareLayout;

typedef struct {
    u16 offset;
    u8 width;
    u32 value;
} RawHeaderEdit;

typedef struct {
    int has_identifier;
    u8 identifier[4];
    int has_timestamp;
    u8 timestamp[5];
    RawHeaderEdit raw[MAX_HEADER_EDITS];
    int raw_count;
} HeaderEdits;

typedef struct {
    u32 start;
    u32 end;
    const char *name;
} FirmwareRange;

static const char *const primary_part_names[5] = {
    "arm9_boot_code.bin",
    "arm7_boot_code.bin",
    "arm9_gui_code.bin",
    "arm7_wifi_code.bin",
    "data_gfx.bin"
};

static const char *const primary_part_labels[5] = {
    "P1 ARM9 boot",
    "P2 ARM7 boot",
    "P3 ARM9 GUI",
    "P4 ARM7 Wi-Fi",
    "P5 graphics/data"
};

static const char *const flashme_part_names[2] = {
    "arm9_boot_code_flashme.bin",
    "arm7_boot_code_flashme.bin"
};

static void print_error(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    fprintf(stderr, "dsfwtool: ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static void blob_init(Blob *blob)
{
    blob->data = NULL;
    blob->size = 0;
}

static void blob_free(Blob *blob)
{
    free(blob->data);
    blob->data = NULL;
    blob->size = 0;
}

static int blob_alloc(Blob *blob, size_t size)
{
    size_t allocation_size = size == 0 ? 1 : size;
    blob_free(blob);
    blob->data = (u8 *)malloc(allocation_size);
    if (blob->data == NULL) {
        print_error("out of memory while allocating %lu bytes", (unsigned long)allocation_size);
        return -1;
    }
    blob->size = size;
    return 0;
}

static int read_file(const char *path, Blob *blob)
{
    FILE *file;
    long length;

    blob_init(blob);
    file = fopen(path, "rb");
    if (file == NULL) {
        print_error("cannot open '%s'", path);
        return -1;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        print_error("cannot seek '%s'", path);
        fclose(file);
        return -1;
    }
    length = ftell(file);
    if (length < 0) {
        print_error("cannot determine the size of '%s'", path);
        fclose(file);
        return -1;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        print_error("cannot seek '%s'", path);
        fclose(file);
        return -1;
    }
    if (blob_alloc(blob, (size_t)length) != 0) {
        fclose(file);
        return -1;
    }
    if (length != 0 && fread(blob->data, 1, (size_t)length, file) != (size_t)length) {
        print_error("cannot read '%s'", path);
        fclose(file);
        blob_free(blob);
        return -1;
    }
    fclose(file);
    return 0;
}

static int mkdir_recursive(const char *path)
{
    char temporary[PATH_BUFFER_SIZE];
    char *cursor;
    size_t length;

    if (path == NULL || path[0] == '\0') return -1;
    if (snprintf(temporary, sizeof(temporary), "%s", path) >= (int)sizeof(temporary)) {
        print_error("path is too long: '%s'", path);
        return -1;
    }
    length = strlen(temporary);
    while (length != 0 && (temporary[length - 1] == '/' || temporary[length - 1] == '\\')) {
        temporary[--length] = '\0';
    }
    if (length == 0) return 0;

    for (cursor = temporary + 1; *cursor; cursor++) {
        char separator;
        struct stat status;
        if (*cursor != '/' && *cursor != '\\') continue;
        separator = *cursor;
        *cursor = '\0';
        if (temporary[0] != '\0' && stat(temporary, &status) != 0) {
            if (mkdir_dir(temporary) != 0 && errno != EEXIST) {
                print_error("cannot create directory '%s'", temporary);
                *cursor = separator;
                return -1;
            }
        }
        *cursor = separator;
    }
    {
        struct stat status;
        if (stat(temporary, &status) != 0) {
            if (mkdir_dir(temporary) != 0 && errno != EEXIST) {
                print_error("cannot create directory '%s'", temporary);
                return -1;
            }
        }
    }
    return 0;
}

static int ensure_parent_directory(const char *path)
{
    char parent[PATH_BUFFER_SIZE];
    char *slash;
    char *backslash;
    char *separator;

    if (snprintf(parent, sizeof(parent), "%s", path) >= (int)sizeof(parent)) {
        print_error("path is too long: '%s'", path);
        return -1;
    }
    slash = strrchr(parent, '/');
    backslash = strrchr(parent, '\\');
    separator = slash;
    if (separator == NULL || (backslash != NULL && backslash > separator)) separator = backslash;
    if (separator == NULL) return 0;
    *separator = '\0';
    if (parent[0] == '\0') return 0;
    return mkdir_recursive(parent);
}

static int write_file(const char *path, const u8 *data, size_t size)
{
    FILE *file;
    if (ensure_parent_directory(path) != 0) return -1;
    file = fopen(path, "wb");
    if (file == NULL) {
        print_error("cannot create '%s'", path);
        return -1;
    }
    if (size != 0 && fwrite(data, 1, size, file) != size) {
        print_error("cannot write '%s'", path);
        fclose(file);
        return -1;
    }
    if (fclose(file) != 0) {
        print_error("cannot close '%s'", path);
        return -1;
    }
    return 0;
}

static int round_up_u32(u32 value, u32 alignment, u32 *output)
{
    u32 remainder;
    if (alignment == 0) return -1;
    remainder = value % alignment;
    if (remainder == 0) {
        *output = value;
        return 0;
    }
    if (value > UINT_MAX - (alignment - remainder)) return -1;
    *output = value + alignment - remainder;
    return 0;
}

static u32 read_le32(const u8 *data)
{
    return (u32)data[0] | ((u32)data[1] << 8) | ((u32)data[2] << 16) | ((u32)data[3] << 24);
}

static u16 read_le16(const u8 *data)
{
    return (u16)((u16)data[0] | ((u16)data[1] << 8));
}

static void write_le16(u8 *data, u16 value)
{
    data[0] = (u8)(value & 0xFF);
    data[1] = (u8)((value >> 8) & 0xFF);
}

static int parse_u32(const char *text, u32 *value)
{
    char *end;
    unsigned long parsed;
    errno = 0;
    parsed = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || parsed > UINT_MAX) {
        print_error("invalid numeric value '%s'", text);
        return -1;
    }
    *value = (u32)parsed;
    return 0;
}

static int parse_size(const char *text, u32 *value, int *is_auto)
{
    char *end;
    unsigned long parsed;
    unsigned long long multiplier = 1;
    unsigned long long result;

    *is_auto = 0;
    if (strcmp(text, "auto") == 0) {
        *is_auto = 1;
        *value = 0;
        return 0;
    }
    errno = 0;
    parsed = strtoul(text, &end, 0);
    if (errno != 0 || end == text) {
        print_error("invalid size '%s'", text);
        return -1;
    }
    if (*end != '\0') {
        if ((end[0] == 'k' || end[0] == 'K') &&
            (end[1] == '\0' || ((end[1] == 'b' || end[1] == 'B') && end[2] == '\0'))) {
            multiplier = 1024;
        } else if ((end[0] == 'm' || end[0] == 'M') &&
                   (end[1] == '\0' || ((end[1] == 'b' || end[1] == 'B') && end[2] == '\0'))) {
            multiplier = 1024 * 1024;
        } else {
            print_error("invalid size suffix in '%s' (use bytes, K, M, or auto)", text);
            return -1;
        }
    }
    result = (unsigned long long)parsed * multiplier;
    if (result == 0 || result > UINT_MAX) {
        print_error("size '%s' is out of range", text);
        return -1;
    }
    *value = (u32)result;
    return 0;
}

static u32 primary_part_offset(const FW_HEADER *header, int index)
{
    switch (index) {
        case 0: return (u32)header->part1_romaddr * (1u << (2 + (header->shift_amounts & 7)));
        case 1: return (u32)header->part2_romaddr * (1u << (2 + ((header->shift_amounts >> 6) & 7)));
        case 2: return (u32)header->part3_romaddr * 8u;
        case 3: return (u32)header->part4_romaddr * 8u;
        case 4: return (u32)header->part5_romaddr * 8u;
        default: return 0;
    }
}

static u32 primary_part_alignment(const FW_HEADER *header, int index)
{
    switch (index) {
        case 0: return 1u << (2 + (header->shift_amounts & 7));
        case 1: return 1u << (2 + ((header->shift_amounts >> 6) & 7));
        default: return 8;
    }
}

static u32 flashme_part_offset(const FW_HEADER *header, int index)
{
    if (index == 0) return (u32)header->part1_romaddr * (1u << (2 + (header->shift_amounts & 7)));
    return (u32)header->part2_romaddr * (1u << (2 + ((header->shift_amounts >> 6) & 7)));
}

static u32 flashme_part_alignment(const FW_HEADER *header, int index)
{
    if (index == 0) return 1u << (2 + (header->shift_amounts & 7));
    return 1u << (2 + ((header->shift_amounts >> 6) & 7));
}

static void sort_indices_by_offset(const u32 *offsets, int count, int *indices)
{
    int i;
    int j;
    for (i = 0; i < count; i++) indices[i] = i;
    for (i = 0; i < count; i++) {
        for (j = i + 1; j < count; j++) {
            if (offsets[indices[j]] < offsets[indices[i]]) {
                int temporary = indices[i];
                indices[i] = indices[j];
                indices[j] = temporary;
            }
        }
    }
}

/* A FlashMe patch derives both headers from an official header, whose ROM
   locations describe the source image rather than the new combined layout.
   Unless a component has an explicit --offset, use deterministic ordering
   hints and let the relocation pass assign its final aligned location. */
static int flashme_has_automatic_component_offsets(const int *has_override,
                                                   int count)
{
    int i;
    for (i = 0; i < count; i++) {
        if (has_override == NULL || !has_override[i]) return 1;
    }
    return 0;
}

static void seed_automatic_layout_offsets(u32 *offsets, const int *has_override,
                                          const int *physical_order, int count)
{
    int rank;
    for (rank = 0; rank < count; rank++) {
        int index = physical_order[rank];
        if (has_override == NULL || !has_override[index]) {
            /* These are ordering hints only.  The relocation pass below applies
               each component's actual alignment and packed size. */
            offsets[index] = HEADER_BYTES + (u32)rank;
        }
    }
}

static int calculate_spans(const u32 *offsets, int count, u32 image_limit, u32 *spans)
{
    int indices[5];
    int i;
    sort_indices_by_offset(offsets, count, indices);
    for (i = 0; i < count; i++) {
        int index = indices[i];
        u32 next = i + 1 == count ? image_limit : offsets[indices[i + 1]];
        if (offsets[index] < HEADER_BYTES || offsets[index] >= image_limit || next <= offsets[index]) {
            print_error("invalid or overlapping component offsets in firmware header");
            return -1;
        }
        spans[index] = next - offsets[index];
    }
    return 0;
}

static int p12_stream_size(const u8 *compressed, u32 available, u32 *effective)
{
    u32 decompressed_size;
    u32 input_position = 4;
    u32 output_position = 0;
    u8 flags = 0;
    int flag_bits = 0;

    if (available < 4 || compressed[0] != 0x10) return -1;
    decompressed_size = (u32)compressed[1] | ((u32)compressed[2] << 8) |
                        ((u32)compressed[3] << 16);
    if (decompressed_size == 0) return -1;
    while (output_position < decompressed_size) {
        if (flag_bits == 0) {
            if (input_position >= available) return -1;
            flags = compressed[input_position++];
            flag_bits = 8;
        }
        if ((flags & 0x80) == 0) {
            if (input_position >= available) return -1;
            input_position++;
            output_position++;
        } else {
            u32 length;
            if (input_position > available - 2) return -1;
            length = ((u32)(compressed[input_position] >> 4) + 3u);
            input_position += 2;
            if (length > decompressed_size - output_position) output_position = decompressed_size;
            else output_position += length;
        }
        flags <<= 1;
        flag_bits--;
    }
    *effective = input_position;
    return 0;
}

static int effective_p12_size(const u8 *encrypted, u32 span, u32 idcode, u32 *effective)
{
    Blob decrypted;
    int decrypted_size;

    if (span < 4) return -1;
    blob_init(&decrypted);
    if (blob_alloc(&decrypted, span) != 0) return -1;
    init_keycode(idcode, 2, 0x0C);
    decrypted_size = decrypt_buffer(encrypted, decrypted.data, (int)span);
    if (decrypted_size < 4 || decrypted.data[0] != 0x10) {
        blob_free(&decrypted);
        return -1;
    }
    if (p12_stream_size(decrypted.data, (u32)decrypted.size, effective) != 0) {
        blob_free(&decrypted);
        return -1;
    }
    blob_free(&decrypted);
    if (*effective == 0 || *effective > span) return -1;
    return 0;
}

static int effective_flashme_p12_size(const u8 *compressed, u32 span, u32 *effective)
{
    return p12_stream_size(compressed, span, effective) == 0 && *effective <= span ? 0 : -1;
}

static int effective_p345_size(const u8 *compressed, u32 span, u32 *effective)
{
    u32 distance_stream_offset;
    if (span < 12 || compressed[4] != 0x80) return -1;
    distance_stream_offset = ((u32)compressed[8] << 24) | ((u32)compressed[9] << 16) |
                             ((u32)compressed[10] << 8) | compressed[11];
    if (distance_stream_offset < 12 || distance_stream_offset >= span) return -1;
    *effective = getCompressedPart345Size((u8 *)compressed);
    if (*effective == 0 || *effective > span) return -1;
    return 0;
}

static int calculate_primary_layout(const Blob *image, FirmwareLayout *layout, int with_effective_sizes)
{
    const FW_HEADER *header;
    u32 idcode;
    int i;

    if (image->size < HEADER_BYTES || image->size > UINT_MAX) {
        print_error("firmware image is too small or too large");
        return -1;
    }
    memset(layout, 0, sizeof(*layout));
    header = (const FW_HEADER *)image->data;
    idcode = read_le32(header->fw_identifier);
    for (i = 0; i < 5; i++) {
        layout->offsets[i] = primary_part_offset(header, i);
        layout->alignments[i] = primary_part_alignment(header, i);
    }
    if (calculate_spans(layout->offsets, 5, (u32)image->size, layout->spans) != 0) return -1;
    if (!with_effective_sizes) return 0;

    for (i = 0; i < 2; i++) {
        if (effective_p12_size(image->data + layout->offsets[i], layout->spans[i], idcode,
                               &layout->effective_sizes[i]) != 0) {
            print_error("%s is not a valid encrypted P1/P2 LZ77 component", primary_part_names[i]);
            return -1;
        }
    }
    for (i = 2; i < 5; i++) {
        if (effective_p345_size(image->data + layout->offsets[i], layout->spans[i],
                                &layout->effective_sizes[i]) != 0) {
            print_error("%s is not a valid P3/P4/P5 compressed component", primary_part_names[i]);
            return -1;
        }
    }
    return 0;
}

static int try_flashme_layout(const Blob *image, u32 header_offset, FirmwareLayout *layout)
{
    const FW_HEADER *header;
    u32 offsets[2];
    u32 spans[2];
    int i;

    if ((unsigned long long)header_offset + HEADER_BYTES > image->size) return 0;
    header = (const FW_HEADER *)(image->data + header_offset);
    for (i = 0; i < 2; i++) {
        offsets[i] = flashme_part_offset(header, i);
        if (offsets[i] < HEADER_BYTES || offsets[i] >= image->size) return 0;
    }
    if (calculate_spans(offsets, 2, (u32)image->size, spans) != 0) return 0;
    for (i = 0; i < 2; i++) {
        u32 effective;
        if (effective_flashme_p12_size(image->data + offsets[i], spans[i], &effective) != 0) return 0;
        layout->flashme_offsets[i] = offsets[i];
        layout->flashme_spans[i] = spans[i];
        layout->flashme_effective_sizes[i] = effective;
        layout->flashme_alignments[i] = flashme_part_alignment(header, i);
    }
    layout->has_flashme = 1;
    layout->flashme_header_offset = header_offset;
    return 1;
}

static void add_flashme_candidate(u32 *candidates, int *count, u32 candidate, size_t image_size)
{
    int i;
    if ((unsigned long long)candidate + HEADER_BYTES > image_size) return;
    for (i = 0; i < *count; i++) {
        if (candidates[i] == candidate) return;
    }
    candidates[(*count)++] = candidate;
}

static void detect_flashme_layout(const Blob *image, FirmwareLayout *layout)
{
    u32 candidates[5];
    int count = 0;
    int i;

    if (image->size < HEADER_BYTES) return;
    if (image->size >= FLASHME_TRAILER) {
        add_flashme_candidate(candidates, &count, (u32)image->size - FLASHME_TRAILER, image->size);
    }
    add_flashme_candidate(candidates, &count, 0x3F680u, image->size);
    add_flashme_candidate(candidates, &count, 0x7F680u, image->size);
    /* FlashMe v1--v4 place the secondary header after the Wi-Fi area,
       at the end of the writable 0x3FE00-byte region. */
    add_flashme_candidate(candidates, &count, 0x3FC80u, image->size);
    add_flashme_candidate(candidates, &count, 0x7FC80u, image->size);

    for (i = 0; i < count; i++) {
        FirmwareLayout candidate = *layout;
        if (try_flashme_layout(image, candidates[i], &candidate)) {
            *layout = candidate;
            return;
        }
    }
}

static int parse_firmware_layout(const Blob *image, FirmwareLayout *layout, int with_effective_sizes)
{
    if (calculate_primary_layout(image, layout, with_effective_sizes) != 0) return -1;
    if (with_effective_sizes) detect_flashme_layout(image, layout);
    return 0;
}

static void fprint_ascii_identifier(FILE *file, const u8 identifier[4])
{
    int i;
    for (i = 0; i < 4; i++) {
        unsigned char value = identifier[i];
        fputc(isprint(value) ? value : '.', file);
    }
}

static void fprint_header_fields(FILE *file, const FW_HEADER *header, const char *label)
{
    const u8 *bytes = (const u8 *)header;
    u16 config_checksum;
    u16 config_length;
    fprintf(file, "%s\n", label);
    fprintf(file, "  identifier: '");
    fprint_ascii_identifier(file, header->fw_identifier);
    fprintf(file, "'  (%02X %02X %02X %02X)\n",
           header->fw_identifier[0], header->fw_identifier[1],
           header->fw_identifier[2], header->fw_identifier[3]);
    fprintf(file, "  console type: 0x%02X\n", header->console_type);
    fprintf(file, "  timestamp: 20%02X-%02X-%02X %02X:%02X\n",
           header->fw_timestamp[4], header->fw_timestamp[3], header->fw_timestamp[2],
           header->fw_timestamp[1], header->fw_timestamp[0]);
    fprintf(file, "  shift amounts: 0x%04X\n", header->shift_amounts);
    fprintf(file, "  user settings offset: 0x%04X\n", header->user_settings_offset);
    fprintf(file, "  CRC16: P1/P2=0x%04X  P3/P4=0x%04X  P5=0x%04X\n",
           header->part12_crc16, header->part34_crc16, header->part5_crc16);
    config_checksum = read_le16(bytes + 0x2A);
    config_length = read_le16(bytes + 0x2C);
    if (config_length == 0 || config_length == 0xFFFFu) {
        fprintf(file, "  Wi-Fi config CRC: 0x%04X  length=0x%04X (not declared)\n",
                config_checksum, config_length);
    } else if ((u32)config_length <= HEADER_BYTES - 0x2Cu) {
        u16 calculated = swiCRC(0, bytes + 0x2C, config_length);
        fprintf(file, "  Wi-Fi config CRC: 0x%04X  length=0x%04X (%s)\n",
                config_checksum, config_length, calculated == config_checksum ? "valid" : "mismatch");
    } else {
        fprintf(file, "  Wi-Fi config CRC: 0x%04X  length=0x%04X (out of range)\n",
                config_checksum, config_length);
    }
}

static void fprint_layout(FILE *file, const char *path, const Blob *image, const FirmwareLayout *layout)
{
    const FW_HEADER *header = (const FW_HEADER *)image->data;
    int i;

    fprintf(file, "Firmware: %s\n", path);
    fprintf(file, "Image size: 0x%08lX (%lu bytes, %.2f KiB)\n",
           (unsigned long)image->size, (unsigned long)image->size, (double)image->size / 1024.0);
    fprint_header_fields(file, header, "Primary header:");
    fprintf(file, "Primary component layout:\n");
    for (i = 0; i < 5; i++) {
        fprintf(file, "  P%d %-16s offset=0x%06X span=0x%06X effective=0x%06X align=0x%X\n",
               i + 1, primary_part_names[i], layout->offsets[i], layout->spans[i],
               layout->effective_sizes[i], layout->alignments[i]);
    }
    if (!layout->has_flashme) {
        fprintf(file, "FlashMe layout: not detected\n");
        return;
    }
    fprintf(file, "FlashMe layout: detected (secondary header at 0x%06X)\n", layout->flashme_header_offset);
    fprint_header_fields(file, (const FW_HEADER *)(image->data + layout->flashme_header_offset),
                         "FlashMe header:");
    for (i = 0; i < 2; i++) {
        fprintf(file, "  F%d %-16s offset=0x%06X span=0x%06X effective=0x%06X align=0x%X\n",
               i + 1, flashme_part_names[i], layout->flashme_offsets[i], layout->flashme_spans[i],
               layout->flashme_effective_sizes[i], layout->flashme_alignments[i]);
    }
}

static void print_layout(const char *path, const Blob *image, const FirmwareLayout *layout)
{
    fprint_layout(stdout, path, image, layout);
}

static int add_raw_header_edit(HeaderEdits *edits, u16 offset, u8 width, u32 value)
{
    if ((u32)offset + width > HEADER_BYTES || (width != 1 && width != 2 && width != 4)) {
        print_error("header edit at offset 0x%X is outside the exported header", offset);
        return -1;
    }
    if (edits->raw_count >= MAX_HEADER_EDITS) {
        print_error("too many header edits");
        return -1;
    }
    edits->raw[edits->raw_count].offset = offset;
    edits->raw[edits->raw_count].width = width;
    edits->raw[edits->raw_count].value = value;
    edits->raw_count++;
    return 0;
}

static int parse_timestamp(const char *text, u8 output[5])
{
    int values[5];
    int i;
    if (strlen(text) == 10) {
        const int positions[5][2] = {{8, 9}, {6, 7}, {4, 5}, {2, 3}, {0, 1}};
        for (i = 0; i < 5; i++) {
            int first = positions[i][0];
            int second = positions[i][1];
            if (!isdigit((unsigned char)text[first]) || !isdigit((unsigned char)text[second])) return -1;
            values[i] = (text[first] - '0') * 10 + text[second] - '0';
        }
    } else if (strlen(text) == 16 && text[4] == '-' && text[7] == '-' &&
               (text[10] == 'T' || text[10] == ' ') && text[13] == ':') {
        int year;
        int month;
        int day;
        int hour;
        int minute;
        for (i = 0; i < 16; i++) {
            if (i == 4 || i == 7 || i == 10 || i == 13) continue;
            if (!isdigit((unsigned char)text[i])) return -1;
        }
        year = (text[0] - '0') * 1000 + (text[1] - '0') * 100 +
               (text[2] - '0') * 10 + text[3] - '0';
        month = (text[5] - '0') * 10 + text[6] - '0';
        day = (text[8] - '0') * 10 + text[9] - '0';
        hour = (text[11] - '0') * 10 + text[12] - '0';
        minute = (text[14] - '0') * 10 + text[15] - '0';
        if (year < 2000 || year > 2255) return -1;
        values[0] = minute;
        values[1] = hour;
        values[2] = day;
        values[3] = month;
        values[4] = year - 2000;
    } else {
        return -1;
    }
    if (values[0] > 59 || values[1] > 23 || values[2] == 0 || values[2] > 31 ||
        values[3] == 0 || values[3] > 12) return -1;
    for (i = 0; i < 5; i++) output[i] = (u8)values[i];
    return 0;
}

static int parse_header_edit_option(int argc, char **argv, int *index, HeaderEdits *edits)
{
    const char *option = argv[*index];
    u32 value;
    u16 offset;
    u8 width;

    if (strcmp(option, "--identifier") == 0) {
        if (*index + 1 >= argc || strlen(argv[*index + 1]) != 4) {
            print_error("--identifier requires exactly four bytes");
            return -1;
        }
        memcpy(edits->identifier, argv[++*index], 4);
        edits->has_identifier = 1;
        return 1;
    }
    if (strcmp(option, "--timestamp") == 0) {
        if (*index + 1 >= argc || parse_timestamp(argv[*index + 1], edits->timestamp) != 0) {
            print_error("--timestamp expects YYMMDDHHMM or YYYY-MM-DDTHH:MM");
            return -1;
        }
        ++*index;
        edits->has_timestamp = 1;
        return 1;
    }
    if (strcmp(option, "--set-u8") == 0 || strcmp(option, "--set-u16") == 0 ||
        strcmp(option, "--set-u32") == 0) {
        if (*index + 2 >= argc || parse_u32(argv[*index + 1], &value) != 0 || value > 0xFFFFu) {
            print_error("%s expects OFFSET VALUE", option);
            return -1;
        }
        offset = (u16)value;
        if (parse_u32(argv[*index + 2], &value) != 0) return -1;
        width = strcmp(option, "--set-u8") == 0 ? 1 :
                (strcmp(option, "--set-u16") == 0 ? 2 : 4);
        if ((width == 1 && value > 0xFFu) || (width == 2 && value > 0xFFFFu)) {
            print_error("value is too large for %s", option);
            return -1;
        }
        *index += 2;
        return add_raw_header_edit(edits, offset, width, value) == 0 ? 1 : -1;
    }

    {
        struct FieldOption {
            const char *name;
            u16 offset;
            u8 width;
        };
        static const struct FieldOption fields[] = {
            {"--console-type", (u16)offsetof(FW_HEADER, console_type), 1},
            {"--shift-amounts", (u16)offsetof(FW_HEADER, shift_amounts), 2},
            {"--part1-ramaddr", (u16)offsetof(FW_HEADER, part1_ramaddr), 2},
            {"--part2-ramaddr", (u16)offsetof(FW_HEADER, part2_ramaddr), 2},
            {"--settings-offset", (u16)offsetof(FW_HEADER, user_settings_offset), 2}
        };
        size_t i;
        for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
            if (strcmp(option, fields[i].name) != 0) continue;
            if (*index + 1 >= argc || parse_u32(argv[*index + 1], &value) != 0) {
                print_error("%s expects a numeric value", option);
                return -1;
            }
            if ((fields[i].width == 1 && value > 0xFFu) ||
                (fields[i].width == 2 && value > 0xFFFFu)) {
                print_error("value is too large for %s", option);
                return -1;
            }
            ++*index;
            return add_raw_header_edit(edits, fields[i].offset, fields[i].width, value) == 0 ? 1 : -1;
        }
    }
    return 0;
}

static int apply_header_edits(Blob *header_blob, const HeaderEdits *edits)
{
    int i;
    if (header_blob->size < HEADER_BYTES) {
        print_error("header is smaller than 0x%X bytes", HEADER_BYTES);
        return -1;
    }
    for (i = 0; i < edits->raw_count; i++) {
        const RawHeaderEdit *edit = &edits->raw[i];
        u8 *destination = header_blob->data + edit->offset;
        if (edit->width == 1) destination[0] = (u8)edit->value;
        else if (edit->width == 2) write_le16(destination, (u16)edit->value);
        else {
            destination[0] = (u8)(edit->value & 0xFF);
            destination[1] = (u8)((edit->value >> 8) & 0xFF);
            destination[2] = (u8)((edit->value >> 16) & 0xFF);
            destination[3] = (u8)((edit->value >> 24) & 0xFF);
        }
    }
    if (edits->has_identifier) memcpy(((FW_HEADER *)header_blob->data)->fw_identifier, edits->identifier, 4);
    if (edits->has_timestamp) memcpy(((FW_HEADER *)header_blob->data)->fw_timestamp, edits->timestamp, 5);
    return 0;
}

/* Offline FlashMe header synchronization.  Component patches construct H and
   FH from the same original header.  After H has been fully recalculated, it
   is authoritative for every shared field, including all component CRC16s,
   Wi-Fi configuration data and its CRC16, and the original-firmware marker at
   0x17E.  FH differs only in its FP1/FP2 ROM/RAM descriptor at 0x0C--0x13 and
   its independent FlashMe type at 0x17C; restore just those fields after
   copying H. */
static int sync_flashme_header_from_primary(Blob *primary_blob, Blob *flash_blob)
{
    const u32 p12_descriptor_offset = (u32)offsetof(FW_HEADER, part1_romaddr);
    const u32 p12_descriptor_size = (u32)offsetof(FW_HEADER, shift_amounts) -
                                    p12_descriptor_offset;
    const u32 type_offset = 0x17Cu;
    u8 secondary_p12_descriptor[8];
    u8 secondary_type[2];
    if (primary_blob->size < HEADER_BYTES || flash_blob->size < HEADER_BYTES) {
        print_error("FlashMe header is smaller than 0x%X bytes", HEADER_BYTES);
        return -1;
    }

    memcpy(secondary_p12_descriptor, flash_blob->data + p12_descriptor_offset,
           p12_descriptor_size);
    memcpy(secondary_type, flash_blob->data + type_offset, sizeof(secondary_type));
    memcpy(flash_blob->data, primary_blob->data, HEADER_BYTES);
    memcpy(flash_blob->data + p12_descriptor_offset, secondary_p12_descriptor,
           p12_descriptor_size);
    memcpy(flash_blob->data + type_offset, secondary_type, sizeof(secondary_type));
    return 0;
}

static int update_header_config_checksum(Blob *header_blob)
{
    u16 config_length;
    u16 checksum;

    if (header_blob->size < 0x2Eu) {
        print_error("header is too small to contain the Wi-Fi configuration checksum");
        return -1;
    }
    config_length = read_le16(header_blob->data + 0x2C);
    /* A number of retail headers use FFFF as an absent/unknown configuration marker. */
    if (config_length == 0 || config_length == 0xFFFFu) return 0;
    if ((u32)config_length > header_blob->size - 0x2Cu) {
        print_error("header Wi-Fi configuration length 0x%04X exceeds the exported header", config_length);
        return -1;
    }
    checksum = swiCRC(0, header_blob->data + 0x2C, config_length);
    write_le16(header_blob->data + 0x2A, checksum);
    return 0;
}

static int read_header_file(const char *path, Blob *header)
{
    Blob full;
    blob_init(&full);
    blob_init(header);
    if (read_file(path, &full) != 0) return -1;
    if (full.size < HEADER_BYTES) {
        print_error("header file '%s' is smaller than 0x%X bytes", path, HEADER_BYTES);
        blob_free(&full);
        return -1;
    }
    if (blob_alloc(header, HEADER_BYTES) != 0) {
        blob_free(&full);
        return -1;
    }
    memcpy(header->data, full.data, HEADER_BYTES);
    blob_free(&full);
    return 0;
}

/* The user-settings pointer names the first of two 0x100-byte settings
   sectors.  The preceding 0x400 bytes hold the three Wi-Fi access-point
   sectors and their adjacent reserved sector.  Treat all six sectors as one
   unit so a base image can safely transfer per-console state when its output
   capacity changes. */
static int get_base_settings_tail(const Blob *base, u32 *source_offset, u32 *source_size)
{
    const FW_HEADER *header;
    u32 user_settings_start;
    u32 tail_start;
    u32 available;

    if (base->size < HEADER_BYTES || base->size > UINT_MAX) {
        print_error("base firmware is too small or too large");
        return -1;
    }
    header = (const FW_HEADER *)base->data;
    user_settings_start = (u32)header->user_settings_offset * 8u;
    if (user_settings_start < WIFI_ACCESS_POINT_BYTES || user_settings_start > base->size) {
        print_error("base firmware has an invalid user-settings offset 0x%04X",
                    header->user_settings_offset);
        return -1;
    }
    tail_start = user_settings_start - WIFI_ACCESS_POINT_BYTES;
    available = (u32)base->size - tail_start;
    if (available < WIFI_ACCESS_POINT_BYTES) {
        print_error("base firmware does not contain its Wi-Fi settings area");
        return -1;
    }
    if (available > SETTINGS_TAIL_BYTES) available = SETTINGS_TAIL_BYTES;
    *source_offset = tail_start;
    *source_size = available;
    return 0;
}

static int trailing_settings_location(u32 capacity, u32 *tail_offset,
                                      u16 *user_settings_offset)
{
    u32 user_settings_start;

    if (capacity < SETTINGS_TAIL_BYTES) {
        print_error("firmware capacity is too small for Wi-Fi and user settings");
        return -1;
    }
    user_settings_start = capacity - USER_SETTINGS_BYTES;
    if (user_settings_start / 8u > 0xFFFFu) {
        print_error("firmware capacity 0x%08X cannot represent a trailing user-settings address",
                    capacity);
        return -1;
    }
    *tail_offset = user_settings_start - WIFI_ACCESS_POINT_BYTES;
    *user_settings_offset = (u16)(user_settings_start / 8u);
    return 0;
}

static int set_header_user_settings_offset(Blob *header_blob, u16 user_settings_offset)
{
    if (header_blob->size < HEADER_BYTES) {
        print_error("header is smaller than 0x%X bytes", HEADER_BYTES);
        return -1;
    }
    write_le16(header_blob->data + offsetof(FW_HEADER, user_settings_offset), user_settings_offset);
    return 0;
}

static int decode_p12(const u8 *compressed, size_t compressed_size, Blob *plain)
{
    u32 decompressed_size;
    u32 actual_size;

    blob_init(plain);
    if (compressed_size < 4 || compressed[0] != 0x10) {
        print_error("P1/P2 data is not an LZ77/LZ10 stream");
        return -1;
    }
    decompressed_size = (u32)compressed[1] | ((u32)compressed[2] << 8) |
                        ((u32)compressed[3] << 16);
    if (decompressed_size == 0) {
        print_error("P1/P2 stream declares an empty output");
        return -1;
    }
    if (blob_alloc(plain, decompressed_size) != 0) return -1;
    actual_size = decompressLZ77(plain->data, compressed);
    if (actual_size != decompressed_size) {
        print_error("P1/P2 decompressor returned an unexpected size");
        blob_free(plain);
        return -1;
    }
    return 0;
}

static int decode_p345(const u8 *compressed, size_t compressed_size, Blob *plain)
{
    u32 decompressed_size;
    u32 actual_size;

    blob_init(plain);
    if (compressed_size < 12 || compressed[4] != 0x80) {
        print_error("P3/P4/P5 data does not have a recognised compression header");
        return -1;
    }
    decompressed_size = ((u32)compressed[5] << 16) | ((u32)compressed[6] << 8) |
                        (u32)compressed[7];
    if (decompressed_size == 0) {
        print_error("P3/P4/P5 stream declares an empty output");
        return -1;
    }
    if (blob_alloc(plain, decompressed_size) != 0) return -1;
    actual_size = decompress_part345(plain->data, (u8 *)compressed);
    if (actual_size != decompressed_size) {
        print_error("P3/P4/P5 decompressor returned an unexpected size");
        blob_free(plain);
        return -1;
    }
    return 0;
}

static int decrypt_p12(const FW_HEADER *header, const Blob *encrypted, Blob *decrypted)
{
    u32 encrypted_size;
    u32 effective_size;
    int output_size;

    blob_init(decrypted);
    if (encrypted->size == 0 || encrypted->size > UINT_MAX ||
        (encrypted->size & 7u) != 0) {
        print_error("encrypted P1/P2 input must be a non-zero multiple of 8 bytes");
        return -1;
    }
    encrypted_size = (u32)encrypted->size;
    if (blob_alloc(decrypted, encrypted_size) != 0) return -1;
    init_keycode(read_le32(header->fw_identifier), 2, 0x0C);
    output_size = decrypt_buffer(encrypted->data, decrypted->data, (int)encrypted_size);
    if (output_size < 0 || (u32)output_size != encrypted_size) {
        print_error("P1/P2 decryption failed");
        blob_free(decrypted);
        return -1;
    }
    /* Decrypt every supplied complete block before examining the stream.  The
       exported P12 layer is then trimmed only after a successful
       post-decryption LZ77 parse; no caller may pre-trim ciphertext to the
       effective stream size. */
    if (p12_stream_size(decrypted->data, encrypted_size, &effective_size) != 0) {
        print_error("P1/P2 decryption input is invalid or does not match the supplied header");
        blob_free(decrypted);
        return -1;
    }
    decrypted->size = effective_size;
    return 0;
}

static int encrypt_p12(const FW_HEADER *header, const Blob *compressed, Blob *encrypted)
{
    u32 effective_size;
    u32 input_size;
    u32 padded_size;
    int output_size;

    blob_init(encrypted);
    if (compressed->size == 0 || compressed->size > UINT_MAX ||
        p12_stream_size(compressed->data, (u32)compressed->size, &effective_size) != 0) {
        print_error("invalid P1/P2 compressed component size");
        return -1;
    }
    input_size = (u32)compressed->size;
    if (round_up_u32(input_size, 8, &padded_size) != 0) {
        print_error("P1/P2 encryption size cannot be aligned to 8 bytes");
        return -1;
    }
    if (blob_alloc(encrypted, padded_size) != 0) return -1;
    init_keycode(read_le32(header->fw_identifier), 2, 0x0C);
    /* p12_stream_size() validates the logical prefix, but intentionally does
       not shorten the caller's input.  A fully aligned P12 buffer may carry
       an explicitly supplied official tail; encrypt_buffer() preserves all
       supplied bytes and constructs official KEY1-derived padding only for
       a partial final block, before encrypting it. */
    output_size = encrypt_buffer(compressed->data, encrypted->data, (int)input_size);
    if (output_size < 0 || (u32)output_size != padded_size) {
        print_error("P1/P2 encryption failed");
        blob_free(encrypted);
        return -1;
    }
    return 0;
}

static int compress_p12(const Blob *plain, Blob *compressed)
{
    u32 capacity;
    u32 actual_size;

    blob_init(compressed);
    if (plain->size == 0 || plain->size > 0xFFFFFFu) {
        print_error("P1/P2 plain input must be between 1 and 0xFFFFFF bytes");
        return -1;
    }
    if (plain->size > UINT_MAX - ((plain->size + 7) / 8) - 16) {
        print_error("P1/P2 input is too large to compress");
        return -1;
    }
    capacity = (u32)plain->size + ((u32)plain->size + 7) / 8 + 16;
    if (blob_alloc(compressed, capacity) != 0) return -1;
    actual_size = compressLZ77(compressed->data, plain->data, (u32)plain->size);
    if (actual_size == 0 || actual_size > capacity) {
        print_error("P1/P2 compression failed");
        blob_free(compressed);
        return -1;
    }
    compressed->size = actual_size;
    return 0;
}

static int compress_p345(const Blob *plain, Blob *compressed)
{
    unsigned long long capacity64;
    u32 actual_size;
    u32 effective_size;

    blob_init(compressed);
    if (plain->size == 0 || plain->size > 0xFFFFFFu) {
        print_error("P3/P4/P5 plain input must be between 1 and 0xFFFFFF bytes");
        return -1;
    }
    capacity64 = (unsigned long long)plain->size * 2u + 0x10000u + 12u;
    if (capacity64 > UINT_MAX) {
        print_error("P3/P4/P5 input is too large to compress");
        return -1;
    }
    if (blob_alloc(compressed, (size_t)capacity64) != 0) return -1;
    actual_size = compress_part345(compressed->data, plain->data, (u32)plain->size);
    if (actual_size == 0 || actual_size > capacity64) {
        print_error("P3/P4/P5 compression failed");
        blob_free(compressed);
        return -1;
    }
    /* The encoder pads its two bitstreams to 32 bits.  Firmware component
       boundaries, however, are defined by the decoder's last consumed byte.
       Retain only that effective stream, so a P3/P4/P5 component neither
       gains a synthetic zero byte nor shifts the next component. */
    effective_size = getCompressedPart345Size(compressed->data);
    if (effective_size < 12 || effective_size > actual_size) {
        print_error("P3/P4/P5 encoder produced an invalid effective size");
        blob_free(compressed);
        return -1;
    }
    compressed->size = effective_size;
    return 0;
}

static int calculate_primary_crcs(const FW_HEADER *header, Blob parts[5],
                                  u16 *part12_crc, u16 *part34_crc, u16 *part5_crc)
{
    Blob decrypted[2];
    Blob plain[5];
    u16 crc;
    int i;
    int result = -1;

    for (i = 0; i < 2; i++) blob_init(&decrypted[i]);
    for (i = 0; i < 5; i++) blob_init(&plain[i]);

    for (i = 0; i < 2; i++) {
        if (decrypt_p12(header, &parts[i], &decrypted[i]) != 0 ||
            decode_p12(decrypted[i].data, decrypted[i].size, &plain[i]) != 0) {
            print_error("%s cannot be decrypted and decompressed using the supplied header",
                        primary_part_names[i]);
            goto cleanup;
        }
    }
    for (i = 2; i < 5; i++) {
        if (decode_p345(parts[i].data, parts[i].size, &plain[i]) != 0) {
            print_error("%s cannot be decompressed", primary_part_names[i]);
            goto cleanup;
        }
    }

    crc = swiCRC(0xFFFF, plain[0].data, (u32)plain[0].size);
    *part12_crc = swiCRC(crc, plain[1].data, (u32)plain[1].size);
    crc = swiCRC(0xFFFF, plain[2].data, (u32)plain[2].size);
    *part34_crc = swiCRC(crc, plain[3].data, (u32)plain[3].size);
    *part5_crc = swiCRC(0xFFFF, plain[4].data, (u32)plain[4].size);
    result = 0;

cleanup:
    for (i = 0; i < 2; i++) blob_free(&decrypted[i]);
    for (i = 0; i < 5; i++) blob_free(&plain[i]);
    return result;
}

static int build_relocated_positions_with_overrides(const u32 *original_offsets,
                                                    const u32 *alignments,
                                                    const u32 *reserved_sizes,
                                                    const int *has_override,
                                                    const u32 *override_offsets,
                                                    int count, u32 *new_offsets)
{
    int indices[MAX_RELOCATED_COMPONENTS];
    u32 sort_offsets[MAX_RELOCATED_COMPONENTS];
    int placed[MAX_RELOCATED_COMPONENTS];
    int i;

    if (count <= 0 || count > MAX_RELOCATED_COMPONENTS) return -1;
    for (i = 0; i < count; i++) {
        int fixed = has_override != NULL && has_override[i];
        if (alignments[i] == 0) {
            print_error("component %d has an invalid alignment", i + 1);
            return -1;
        }
        placed[i] = fixed;
        if (fixed) {
            /* Do not reject a low address here.  The range table below owns
               the diagnostic for a component that intersects the primary
               header, so callers receive the useful "overlaps primary
               header" error rather than a generic bad-offset message. */
            if (override_offsets == NULL ||
                override_offsets[i] % alignments[i] != 0) {
                print_error("component %d has an invalid explicit offset", i + 1);
                return -1;
            }
            new_offsets[i] = override_offsets[i];
            sort_offsets[i] = override_offsets[i];
        } else {
            if (original_offsets[i] < HEADER_BYTES) {
                print_error("component %d has an invalid original offset", i + 1);
                return -1;
            }
            sort_offsets[i] = original_offsets[i];
        }
    }
    sort_indices_by_offset(sort_offsets, count, indices);
    for (i = 0; i < count; i++) {
        int index = indices[i];
        u32 candidate;
        int j;

        if (placed[index]) continue;
        if (i == 0) {
            candidate = original_offsets[index];
        } else {
            int previous = indices[i - 1];
            if (reserved_sizes[previous] > UINT_MAX - new_offsets[previous]) {
                print_error("component layout exceeds the firmware address space");
                return -1;
            }
            candidate = new_offsets[previous] + reserved_sizes[previous];
            if (candidate < original_offsets[index]) candidate = original_offsets[index];
        }

        for (;;) {
            u32 end;
            u32 conflict_end = 0;
            if (round_up_u32(candidate, alignments[index], &candidate) != 0 ||
                reserved_sizes[index] > UINT_MAX - candidate) {
                print_error("component layout cannot be aligned");
                return -1;
            }
            end = candidate + reserved_sizes[index];
            for (j = 0; j < count; j++) {
                u32 other_end;
                if (j == index || !placed[j]) continue;
                if (reserved_sizes[j] > UINT_MAX - new_offsets[j]) {
                    print_error("component layout exceeds the firmware address space");
                    return -1;
                }
                other_end = new_offsets[j] + reserved_sizes[j];
                if (candidate < other_end && new_offsets[j] < end && other_end > conflict_end) {
                    conflict_end = other_end;
                }
            }
            if (conflict_end == 0) break;
            candidate = conflict_end;
        }
        new_offsets[index] = candidate;
        placed[index] = 1;
    }
    return 0;
}

/* FlashMe components are physically interleaved with P1--P5.  Rebuilding
   each group independently is safe only while all stream sizes stay below
   their original spans; a community-compressed stream can grow and then
   collide with a component from the other group.  Relocate all seven starts
   in their shared physical order, while retaining the alignment encoded by
   the header that owns each start. */
static int build_flashme_combined_positions(const u32 primary_original_offsets[5],
                                            const u32 primary_alignments[5],
                                            const u32 primary_sizes[5],
                                            const int primary_has_override[5],
                                            const u32 primary_override_offsets[5],
                                            const u32 flash_original_offsets[2],
                                            const u32 flash_alignments[2],
                                            const u32 flash_sizes[2],
                                            const int flash_has_override[2],
                                            const u32 flash_override_offsets[2],
                                            u32 primary_new_offsets[5],
                                            u32 flash_new_offsets[2])
{
    u32 original_offsets[MAX_RELOCATED_COMPONENTS];
    u32 alignments[MAX_RELOCATED_COMPONENTS];
    u32 sizes[MAX_RELOCATED_COMPONENTS];
    u32 override_offsets[MAX_RELOCATED_COMPONENTS];
    int has_override[MAX_RELOCATED_COMPONENTS];
    u32 new_offsets[MAX_RELOCATED_COMPONENTS];
    static const int physical_order[MAX_RELOCATED_COMPONENTS] = {0, 1, 4, 3, 2, 5, 6};
    int i;

    for (i = 0; i < 5; i++) {
        original_offsets[i] = primary_original_offsets[i];
        alignments[i] = primary_alignments[i];
        sizes[i] = primary_sizes[i];
        has_override[i] = primary_has_override[i];
        override_offsets[i] = primary_override_offsets[i];
    }
    for (i = 0; i < 2; i++) {
        original_offsets[5 + i] = flash_original_offsets[i];
        alignments[5 + i] = flash_alignments[i];
        sizes[5 + i] = flash_sizes[i];
        has_override[5 + i] = flash_has_override[i];
        override_offsets[5 + i] = flash_override_offsets[i];
    }
    if (flashme_has_automatic_component_offsets(has_override,
                                                MAX_RELOCATED_COMPONENTS)) {
        seed_automatic_layout_offsets(original_offsets, has_override, physical_order,
                                      MAX_RELOCATED_COMPONENTS);
        printf("Using automatic FlashMe component layout for components without --offset.\n");
    }
    if (build_relocated_positions_with_overrides(original_offsets, alignments, sizes,
                                                 has_override, override_offsets,
                                                 MAX_RELOCATED_COMPONENTS, new_offsets) != 0) {
        return -1;
    }
    for (i = 0; i < 5; i++) primary_new_offsets[i] = new_offsets[i];
    for (i = 0; i < 2; i++) flash_new_offsets[i] = new_offsets[5 + i];
    return 0;
}

static int set_primary_component_offsets(FW_HEADER *header, const u32 offsets[5])
{
    u32 values[5];
    int i;

    values[0] = offsets[0] / primary_part_alignment(header, 0);
    values[1] = offsets[1] / primary_part_alignment(header, 1);
    values[2] = offsets[2] / 8;
    values[3] = offsets[3] / 8;
    values[4] = offsets[4] / 8;
    for (i = 0; i < 5; i++) {
        if (values[i] > 0xFFFFu) {
            print_error("component P%d offset cannot be represented in the firmware header", i + 1);
            return -1;
        }
    }
    header->part1_romaddr = (u16)values[0];
    header->part2_romaddr = (u16)values[1];
    header->part3_romaddr = (u16)values[2];
    header->part4_romaddr = (u16)values[3];
    header->part5_romaddr = (u16)values[4];
    return 0;
}

static int set_flashme_component_offsets(FW_HEADER *header, const u32 offsets[2])
{
    u32 value1 = offsets[0] / flashme_part_alignment(header, 0);
    u32 value2 = offsets[1] / flashme_part_alignment(header, 1);
    if (value1 > 0xFFFFu || value2 > 0xFFFFu) {
        print_error("FlashMe component offset cannot be represented in the secondary header");
        return -1;
    }
    header->part1_romaddr = (u16)value1;
    header->part2_romaddr = (u16)value2;
    return 0;
}

static int add_firmware_range(FirmwareRange ranges[MAX_RANGES], int *count,
                              u32 start, u32 size, u32 image_size, const char *name)
{
    u32 end;
    int i;
    if (size == 0 || start > image_size || size > image_size - start) {
        print_error("%s does not fit in the requested firmware capacity", name);
        return -1;
    }
    end = start + size;
    for (i = 0; i < *count; i++) {
        if (start < ranges[i].end && ranges[i].start < end) {
            print_error("%s overlaps %s", name, ranges[i].name);
            return -1;
        }
    }
    if (*count >= MAX_RANGES) {
        print_error("internal range table is full");
        return -1;
    }
    ranges[*count].start = start;
    ranges[*count].end = end;
    ranges[*count].name = name;
    (*count)++;
    return 0;
}

static void print_usage(void)
{
    printf("dsfwtool v%s - Nintendo DS firmware toolkit\n", DSFWTOOL_VERSION);
    printf("\n");
    printf("Output paths may include new parent directories, which are created automatically.\n");
    printf("\n");
    printf("Information:\n");
    printf("  dsfwtool -i FIRMWARE.bin [-o REPORT.txt]\n");
    printf("\n");
    printf("Extract from a firmware image:\n");
    printf("  dsfwtool -x FIRMWARE.bin -h HEADER.bin\n");
    printf("      -p1 [-decrypt [-uncomp]] FILE\n");
    printf("      -p2 [-decrypt [-uncomp]] FILE\n");
    printf("      -p3 [-uncomp] FILE  -p4 [-uncomp] FILE  -p5 [-uncomp] FILE\n");
    printf("  Without a modifier, a component is exported in its on-image form.\n");
    printf("  P1/P2 -uncomp requires the preceding -decrypt.  If either P1/P2 is\n");
    printf("  decrypted, -h HEADER.bin must also be requested as an output file.\n");
    printf("\n");
    printf("Create an official firmware image:\n");
    printf("  dsfwtool -c OUTPUT.bin -h HEADER.bin\n");
    printf("      -p1 [-encrypt | -comp -encrypt] [--offset OFFSET] FILE\n");
    printf("      -p2 [-encrypt | -comp -encrypt] [--offset OFFSET] FILE\n");
    printf("      -p3 [-comp] [--offset OFFSET] FILE  -p4 [-comp] [--offset OFFSET] FILE\n");
    printf("      -p5 [-comp] [--offset OFFSET] FILE\n");
    printf("      [-fh [--offset OFFSET] FLASH_HEADER.bin\n");
    printf("       -fp1 [-comp] [--offset OFFSET] FILE -fp2 [-comp] [--offset OFFSET] FILE]\n");
    printf("      [-b BASE.bin] [-s 256K|512K|1M|auto] [--fill 00|FF]\n");
    printf("  P1/P2 with no modifier are already encrypted P12 streams.  -encrypt\n");
    printf("  encrypts an already compressed P12 stream; -comp -encrypt compresses\n");
    printf("  plain data before encryption.  P3/P4/P5 -comp uses P345 compression.\n");
    printf("  P3/P4/P5 effective streams are zero-padded through the next 8-byte boundary\n");
    printf("  while assembling the image; that padding is not part of an exported component.\n");
    printf("  In -c, --offset pins P1--P5, -fh, -fp1, or -fp2 at a physical ROM byte\n");
    printf("  address.  P/FP starts must meet their header alignment.  The packed\n");
    printf("  (compressed/encrypted) size is used for overlap checks and header offsets\n");
    printf("  are rewritten automatically.  The primary header occupies 0x000000--0x00017F.\n");
    printf("  Normal firmware reserves its final 0x600 bytes; FlashMe reserves only its\n");
    printf("  secondary 0x180-byte header and final 0x200 user-settings bytes.\n");
    printf("  Without -fh --offset, FlashMe uses capacity minus 0x980 (0x3F680 at 256 KiB,\n");
    printf("  0x7F680 at 512 KiB) for compatibility.\n");
    printf("  Unused output bytes are filled with FF by default (override with --fill).\n");
    printf("  -b transfers the base firmware's 0x600-byte settings tail (through 512 KiB).\n");
    printf("  In FlashMe mode, components may deliberately replace its Wi-Fi-area bytes.\n");
    printf("\n");
    printf("Independent component operations:\n");
    printf("  dsfwtool -p1 -comp INPUT.bin -o OUTPUT.bin\n");
    printf("  dsfwtool -p1 -uncomp INPUT.bin -o OUTPUT.bin\n");
    printf("  dsfwtool -p1 -crypt INPUT.bin -h HEADER.bin -o OUTPUT.bin\n");
    printf("  dsfwtool -p2 -decrypt INPUT.bin -h HEADER.bin -o OUTPUT.bin\n");
    printf("  dsfwtool -p1 -decrypt -uncomp INPUT.bin -h HEADER.bin -o OUTPUT.bin\n");
    printf("  dsfwtool -p1 -comp -crypt INPUT.bin -h HEADER.bin -o OUTPUT.bin\n");
    printf("  dsfwtool -p3 -comp INPUT.bin -o OUTPUT.bin\n");
    printf("  dsfwtool -p3 -uncomp INPUT.bin -o OUTPUT.bin\n");
    printf("  -p1/-p2 select P12; -p3/-p4/-p5 select P345 automatically.\n");
    printf("  P1/P2 crypt/decrypt requires -h because fw_identifier supplies the key.\n");
    printf("  Decryption requires a complete 8-byte-aligned ciphertext and writes only the effective P12 stream;\n");
    printf("  encryption preserves an aligned input and completes a partial final block internally.\n");
    printf("\n");
    printf("FlashMe components (select as needed with -x; supply all three with -c):\n");
    printf("  -fh FLASH_HEADER.bin  -fp1 [-comp|-uncomp] FILE  -fp2 [-comp|-uncomp] FILE\n");
    printf("  dsfwtool -fp1 -uncomp INPUT.bin -o OUTPUT.bin\n");
    printf("  dsfwtool -fp1 -comp INPUT.bin -o OUTPUT.bin\n");
    printf("  FlashMe FP1/FP2 are unencrypted P12/LZ77 streams; -encrypt/-decrypt is invalid.\n");
    printf("  Creation writes the complete logical 256 KiB multiple; use -s for a larger capacity.\n");
    printf("\n");
    printf("Header edits for -c (applied before P1/P2 encryption):\n");
    printf("  --identifier ABCD  --console-type VALUE  --timestamp YYMMDDHHMM\n");
    printf("  --shift-amounts VALUE  --part1-ramaddr VALUE  --part2-ramaddr VALUE\n");
    printf("  --settings-offset VALUE  --set-u8 OFFSET VALUE  --set-u16 OFFSET VALUE\n");
    printf("  --set-u32 OFFSET VALUE\n");
    printf("  ROM offsets and component CRCs are recalculated, not user-settable.\n");
    printf("  --shift-amounts and --settings-offset are advanced: the former controls\n");
    printf("  P1/P2 alignment and RAM interpretation; the latter must match the\n");
    printf("  user-data area expected by the resulting firmware image.\n");
}

enum {
    FWCOMP_HEADER = 0,
    FWCOMP_P1,
    FWCOMP_P2,
    FWCOMP_P3,
    FWCOMP_P4,
    FWCOMP_P5,
    FWCOMP_FLASH_HEADER,
    FWCOMP_FLASH_P1,
    FWCOMP_FLASH_P2,
    FWCOMP_COUNT
};

typedef struct {
    const char *path;
    int has_offset;
    u32 offset;
    int compress;
    int encrypt;
    int decrypt;
    int uncompress;
} ExplicitComponent;

typedef struct {
    ExplicitComponent components[FWCOMP_COUNT];
    const char *base_path;
    const char *size_text;
    u8 fill_byte;
    HeaderEdits edits;
} ExplicitFirmwareRequest;

typedef struct {
    int component;
    const char *header_path;
    const char *input_path;
    const char *output_path;
    int compress;
    int uncompress;
    int encrypt;
    int decrypt;
} ExplicitStreamRequest;

static const char *explicit_component_name(int component)
{
    static const char *const names[FWCOMP_COUNT] = {
        "header", "P1", "P2", "P3", "P4", "P5", "FlashMe header", "FlashMe P1", "FlashMe P2"
    };
    if (component < 0 || component >= FWCOMP_COUNT) return "component";
    return names[component];
}

static int explicit_component_kind(const char *option)
{
    if (strcmp(option, "-h") == 0) return FWCOMP_HEADER;
    if (strcmp(option, "-p1") == 0) return FWCOMP_P1;
    if (strcmp(option, "-p2") == 0) return FWCOMP_P2;
    if (strcmp(option, "-p3") == 0) return FWCOMP_P3;
    if (strcmp(option, "-p4") == 0) return FWCOMP_P4;
    if (strcmp(option, "-p5") == 0) return FWCOMP_P5;
    if (strcmp(option, "-fh") == 0) return FWCOMP_FLASH_HEADER;
    if (strcmp(option, "-fp1") == 0) return FWCOMP_FLASH_P1;
    if (strcmp(option, "-fp2") == 0) return FWCOMP_FLASH_P2;
    return -1;
}

static int is_primary_p12_component(int component)
{
    return component == FWCOMP_P1 || component == FWCOMP_P2;
}

static int is_primary_p345_component(int component)
{
    return component >= FWCOMP_P3 && component <= FWCOMP_P5;
}

static int is_flashme_p12_component(int component)
{
    return component == FWCOMP_FLASH_P1 || component == FWCOMP_FLASH_P2;
}

static int is_p12_component(int component)
{
    return is_primary_p12_component(component) || is_flashme_p12_component(component);
}

static int is_encrypt_option(const char *option)
{
    return strcmp(option, "-encrypt") == 0 || strcmp(option, "-crypt") == 0;
}

static int take_option_value(int argc, char **argv, int *index, const char *option,
                             const char **value)
{
    if (*index + 1 >= argc) {
        print_error("%s requires a value", option);
        return -1;
    }
    *value = argv[++*index];
    return 0;
}

static int parse_fill_byte(const char *text, u8 *fill_byte)
{
    u32 value;
    if (strlen(text) == 2 && isxdigit((unsigned char)text[0]) &&
        isxdigit((unsigned char)text[1])) {
        int high = isdigit((unsigned char)text[0]) ? text[0] - '0' :
                   tolower((unsigned char)text[0]) - 'a' + 10;
        int low = isdigit((unsigned char)text[1]) ? text[1] - '0' :
                  tolower((unsigned char)text[1]) - 'a' + 10;
        *fill_byte = (u8)((high << 4) | low);
        return 0;
    }
    if (parse_u32(text, &value) != 0 || value > 0xFFu) {
        print_error("--fill expects one byte, for example 00, FF, or 0xFF");
        return -1;
    }
    *fill_byte = (u8)value;
    return 0;
}

static int parse_explicit_component(int argc, char **argv, int *index, int create,
                                    ExplicitFirmwareRequest *request)
{
    int component = explicit_component_kind(argv[*index]);
    ExplicitComponent *entry;
    u32 offset;

    if (component < 0) return -1;
    entry = &request->components[component];
    if (entry->path != NULL) {
        print_error("%s is specified more than once", explicit_component_name(component));
        return -1;
    }
    ++*index;

    if (is_primary_p12_component(component)) {
        if (create) {
            if (*index < argc && strcmp(argv[*index], "-comp") == 0) {
                entry->compress = 1;
                ++*index;
                if (*index >= argc || !is_encrypt_option(argv[*index])) {
                    print_error("%s -comp must be followed by -encrypt", explicit_component_name(component));
                    return -1;
                }
                entry->encrypt = 1;
                ++*index;
            } else if (*index < argc && is_encrypt_option(argv[*index])) {
                entry->encrypt = 1;
                ++*index;
            }
        } else {
            if (*index < argc && strcmp(argv[*index], "-decrypt") == 0) {
                entry->decrypt = 1;
                ++*index;
                if (*index < argc && strcmp(argv[*index], "-uncomp") == 0) {
                    entry->uncompress = 1;
                    ++*index;
                }
            } else if (*index < argc && strcmp(argv[*index], "-uncomp") == 0) {
                print_error("%s -uncomp is only valid after -decrypt in -x mode",
                            explicit_component_name(component));
                return -1;
            }
        }
    } else if (is_primary_p345_component(component) || is_flashme_p12_component(component)) {
        if (create) {
            if (*index < argc && strcmp(argv[*index], "-comp") == 0) {
                entry->compress = 1;
                ++*index;
            }
        } else if (*index < argc && strcmp(argv[*index], "-uncomp") == 0) {
            entry->uncompress = 1;
            ++*index;
        }
    }

    if (*index < argc && strcmp(argv[*index], "--offset") == 0) {
        const char *value;
        if (!create) {
            print_error("%s --offset is only valid with -c", explicit_component_name(component));
            return -1;
        }
        if (component == FWCOMP_HEADER) {
            print_error("the primary header is fixed at physical offset 0x000000");
            return -1;
        }
        if (entry->has_offset) {
            print_error("%s offset is specified more than once", explicit_component_name(component));
            return -1;
        }
        if (take_option_value(argc, argv, index, "--offset", &value) != 0 ||
            parse_u32(value, &offset) != 0) return -1;
        entry->has_offset = 1;
        entry->offset = offset;
        ++*index;
    }

    if (*index >= argc || argv[*index][0] == '-') {
        print_error("%s requires an explicit filename", explicit_component_name(component));
        return -1;
    }
    entry->path = argv[*index];
    return 0;
}

static int parse_explicit_firmware_request(int argc, char **argv, int first_option, int create,
                                           ExplicitFirmwareRequest *request)
{
    int i;
    memset(request, 0, sizeof(*request));
    request->fill_byte = 0xFF;

    for (i = first_option; i < argc; i++) {
        int component = explicit_component_kind(argv[i]);
        int edit_result;

        if (component >= 0) {
            if (parse_explicit_component(argc, argv, &i, create, request) != 0) return -1;
            continue;
        }
        if (create && strcmp(argv[i], "-b") == 0) {
            if (request->base_path != NULL) {
                print_error("-b is specified more than once");
                return -1;
            }
            if (take_option_value(argc, argv, &i, "-b", &request->base_path) != 0) return -1;
            continue;
        }
        if (create && (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--size") == 0)) {
            if (take_option_value(argc, argv, &i, argv[i], &request->size_text) != 0) return -1;
            continue;
        }
        if (create && strcmp(argv[i], "--fill") == 0) {
            const char *value;
            if (take_option_value(argc, argv, &i, "--fill", &value) != 0 ||
                parse_fill_byte(value, &request->fill_byte) != 0) return -1;
            continue;
        }
        if (create) {
            edit_result = parse_header_edit_option(argc, argv, &i, &request->edits);
            if (edit_result < 0) return -1;
            if (edit_result > 0) continue;
        }
        print_error("unknown option '%s'", argv[i]);
        return -1;
    }
    return 0;
}

static int count_explicit_components(const ExplicitFirmwareRequest *request)
{
    int count = 0;
    int i;
    for (i = 0; i < FWCOMP_COUNT; i++) {
        if (request->components[i].path != NULL) count++;
    }
    return count;
}

static int validate_explicit_firmware_request(const ExplicitFirmwareRequest *request, int create)
{
    int i;
    int flashme_any;

    if (!create) {
        if (count_explicit_components(request) == 0) {
            print_error("-x requires at least one output component option");
            return -1;
        }
        if ((request->components[FWCOMP_P1].decrypt || request->components[FWCOMP_P2].decrypt) &&
            request->components[FWCOMP_HEADER].path == NULL) {
            print_error("-x with P1/P2 -decrypt must also export -h HEADER.bin");
            return -1;
        }
        return 0;
    }

    if (request->components[FWCOMP_HEADER].path == NULL) {
        print_error("-c requires -h HEADER.bin");
        return -1;
    }
    for (i = FWCOMP_P1; i <= FWCOMP_P5; i++) {
        if (request->components[i].path == NULL) {
            print_error("-c requires %s", explicit_component_name(i));
            return -1;
        }
    }
    flashme_any = request->components[FWCOMP_FLASH_HEADER].path != NULL ||
                   request->components[FWCOMP_FLASH_P1].path != NULL ||
                   request->components[FWCOMP_FLASH_P2].path != NULL;
    if (flashme_any && (request->components[FWCOMP_FLASH_HEADER].path == NULL ||
                        request->components[FWCOMP_FLASH_P1].path == NULL ||
                        request->components[FWCOMP_FLASH_P2].path == NULL)) {
        print_error("FlashMe creation requires -fh, -fp1, and -fp2 together");
        return -1;
    }
    return 0;
}

static int blob_copy_data(Blob *blob, const u8 *data, size_t size)
{
    blob_init(blob);
    if (blob_alloc(blob, size) != 0) return -1;
    if (size != 0) memcpy(blob->data, data, size);
    return 0;
}

static int component_span(const Blob *component, u32 *span)
{
    if (component->size == 0 || component->size > UINT_MAX) {
        print_error("component has an invalid size");
        return -1;
    }
    /* The header's shift value constrains a component's *start* address.
       The next component is aligned independently.  Rounding this component
       to its own start alignment would invent bytes and move valid P3/P4/P5
       offsets (some official P2 streams end only on an 8-byte boundary). */
    *span = (u32)component->size;
    return 0;
}

static int max_reserved_end(const u32 *offsets, const u32 *reserved_sizes, int count, u32 *end)
{
    int i;
    *end = HEADER_BYTES;
    for (i = 0; i < count; i++) {
        u32 component_end;
        if (reserved_sizes[i] > UINT_MAX - offsets[i]) {
            print_error("component layout exceeds the firmware address space");
            return -1;
        }
        component_end = offsets[i] + reserved_sizes[i];
        if (component_end > *end) *end = component_end;
    }
    return 0;
}

/* P3/P4/P5 store their effective bitstream size, not their trailing address
   alignment bytes.  Keep those bytes out of exported component files, then
   synthesize zeroes through the next 8-byte boundary while assembling the
   image.  The boundary must never overwrite a following component or the
   FlashMe secondary header or protected settings tail.  P1/P2 are different: their encryption layer owns
   the corresponding 8-byte padding. */
static int write_p345_alignment_padding(Blob *output, const u32 primary_offsets[5],
                                        const Blob primary_parts[5],
                                        const u32 flash_offsets[2], int has_flashme,
                                        u32 flash_header_offset,
                                        u32 protected_tail_offset)
{
    int i;

    for (i = 2; i < 5; i++) {
        u32 end;
        u32 padded_end;
        u32 next_start = UINT_MAX;
        int j;

        if (primary_parts[i].size > UINT_MAX - primary_offsets[i]) {
            print_error("P%d alignment padding exceeds the firmware address space", i + 1);
            return -1;
        }
        end = primary_offsets[i] + (u32)primary_parts[i].size;
        if (round_up_u32(end, 8, &padded_end) != 0) {
            print_error("P%d alignment padding cannot be calculated", i + 1);
            return -1;
        }
        for (j = 0; j < 5; j++) {
            if (j != i && primary_offsets[j] > end && primary_offsets[j] < next_start) {
                next_start = primary_offsets[j];
            }
        }
        if (has_flashme) {
            for (j = 0; j < 2; j++) {
                if (flash_offsets[j] > end && flash_offsets[j] < next_start) {
                    next_start = flash_offsets[j];
                }
            }
            if (flash_header_offset > end && flash_header_offset < next_start) {
                next_start = flash_header_offset;
            }
        }
        if (protected_tail_offset > end && protected_tail_offset < next_start) {
            next_start = protected_tail_offset;
        }
        if (next_start != UINT_MAX && padded_end > next_start) padded_end = next_start;
        if (padded_end > output->size) {
            print_error("P%d alignment padding does not fit in the output image", i + 1);
            return -1;
        }
        if (padded_end > end) memset(output->data + end, 0, padded_end - end);
    }
    return 0;
}

static int extract_primary_p12_component(const Blob *image, const FirmwareLayout *layout,
                                         int index, const ExplicitComponent *request)
{
    const FW_HEADER *header = (const FW_HEADER *)image->data;
    u32 raw_size;
    Blob encrypted;
    Blob decrypted;
    Blob plain;
    int result = -1;

    blob_init(&encrypted);
    blob_init(&decrypted);
    blob_init(&plain);
    /* The stored component occupies its full range up to the next component.
       Decrypt that complete original ciphertext range first; decrypt_p12()
       determines and trims the logical stream only afterwards. */
    raw_size = layout->spans[index];
    if (raw_size == 0) {
        print_error("cannot determine the encrypted range of %s", primary_part_names[index]);
        goto cleanup;
    }
    if (!request->decrypt) {
        result = write_file(request->path, image->data + layout->offsets[index], raw_size);
        goto cleanup;
    }
    if (blob_copy_data(&encrypted, image->data + layout->offsets[index], raw_size) != 0 ||
        decrypt_p12(header, &encrypted, &decrypted) != 0) goto cleanup;
    if (!request->uncompress) {
        result = write_file(request->path, decrypted.data, decrypted.size);
        goto cleanup;
    }
    if (decode_p12(decrypted.data, decrypted.size, &plain) != 0) goto cleanup;
    result = write_file(request->path, plain.data, plain.size);

cleanup:
    blob_free(&encrypted);
    blob_free(&decrypted);
    blob_free(&plain);
    return result;
}

static int extract_primary_p345_component(const Blob *image, const FirmwareLayout *layout,
                                          int index, const ExplicitComponent *request)
{
    Blob plain;
    int result;

    blob_init(&plain);
    if (!request->uncompress) {
        return write_file(request->path, image->data + layout->offsets[index],
                          layout->effective_sizes[index]);
    }
    result = decode_p345(image->data + layout->offsets[index], layout->effective_sizes[index], &plain);
    if (result == 0) result = write_file(request->path, plain.data, plain.size);
    blob_free(&plain);
    return result;
}

static int extract_flashme_p12_component(const Blob *image, const FirmwareLayout *layout,
                                         int index, const ExplicitComponent *request)
{
    Blob plain;
    int result;

    blob_init(&plain);
    if (!request->uncompress) {
        return write_file(request->path, image->data + layout->flashme_offsets[index],
                          layout->flashme_effective_sizes[index]);
    }
    result = decode_p12(image->data + layout->flashme_offsets[index],
                        layout->flashme_effective_sizes[index], &plain);
    if (result == 0) result = write_file(request->path, plain.data, plain.size);
    blob_free(&plain);
    return result;
}

static int command_explicit_info(int argc, char **argv)
{
    Blob image;
    FirmwareLayout layout;
    FILE *report = NULL;
    int result = 1;

    blob_init(&image);
    if (argc != 3 && (argc != 5 || strcmp(argv[3], "-o") != 0)) {
        print_error("usage: dsfwtool -i FIRMWARE.bin [-o REPORT.txt]");
        return 1;
    }
    if (read_file(argv[2], &image) != 0 || parse_firmware_layout(&image, &layout, 1) != 0) goto cleanup;
    print_layout(argv[2], &image, &layout);
    if (argc == 5) {
        if (ensure_parent_directory(argv[4]) != 0) goto cleanup;
        report = fopen(argv[4], "wb");
        if (report == NULL) {
            print_error("cannot create '%s'", argv[4]);
            goto cleanup;
        }
        fprint_layout(report, argv[2], &image, &layout);
        if (fclose(report) != 0) {
            report = NULL;
            print_error("cannot close '%s'", argv[4]);
            goto cleanup;
        }
        report = NULL;
        printf("Wrote layout report: %s\n", argv[4]);
    }
    result = 0;

cleanup:
    if (report != NULL) fclose(report);
    blob_free(&image);
    return result;
}

static int command_explicit_extract(int argc, char **argv)
{
    const char *firmware_path;
    ExplicitFirmwareRequest request;
    Blob image;
    FirmwareLayout layout;
    int i;
    int result = 1;

    blob_init(&image);
    if (argc < 4) {
        print_error("usage: dsfwtool -x FIRMWARE.bin COMPONENT_OPTIONS");
        return 1;
    }
    firmware_path = argv[2];
    if (parse_explicit_firmware_request(argc, argv, 3, 0, &request) != 0 ||
        validate_explicit_firmware_request(&request, 0) != 0) goto cleanup;
    if (read_file(firmware_path, &image) != 0 || parse_firmware_layout(&image, &layout, 1) != 0) goto cleanup;

    if (request.components[FWCOMP_HEADER].path != NULL &&
        write_file(request.components[FWCOMP_HEADER].path, image.data, HEADER_BYTES) != 0) goto cleanup;

    for (i = 0; i < 2; i++) {
        ExplicitComponent *entry = &request.components[FWCOMP_P1 + i];
        if (entry->path != NULL && extract_primary_p12_component(&image, &layout, i, entry) != 0) goto cleanup;
    }
    for (i = 2; i < 5; i++) {
        ExplicitComponent *entry = &request.components[FWCOMP_P1 + i];
        if (entry->path != NULL && extract_primary_p345_component(&image, &layout, i, entry) != 0) goto cleanup;
    }

    if (request.components[FWCOMP_FLASH_HEADER].path != NULL ||
        request.components[FWCOMP_FLASH_P1].path != NULL ||
        request.components[FWCOMP_FLASH_P2].path != NULL) {
        if (!layout.has_flashme) {
            print_error("the source firmware does not contain a recognised FlashMe layout");
            goto cleanup;
        }
        if (request.components[FWCOMP_FLASH_HEADER].path != NULL &&
            write_file(request.components[FWCOMP_FLASH_HEADER].path,
                       image.data + layout.flashme_header_offset, HEADER_BYTES) != 0) goto cleanup;
        for (i = 0; i < 2; i++) {
            ExplicitComponent *entry = &request.components[FWCOMP_FLASH_P1 + i];
            if (entry->path != NULL && extract_flashme_p12_component(&image, &layout, i, entry) != 0) goto cleanup;
        }
    }

    printf("Extracted explicit component files from %s\n", firmware_path);
    result = 0;

cleanup:
    blob_free(&image);
    return result;
}

static int prepare_component_for_create(int component, const ExplicitComponent *request,
                                        const FW_HEADER *primary_header, Blob *output)
{
    Blob source;
    Blob transformed;
    int result = -1;

    blob_init(&source);
    blob_init(&transformed);
    blob_init(output);
    if (read_file(request->path, &source) != 0 || source.size == 0 || source.size > UINT_MAX) {
        if (source.size == 0 && request->path != NULL) {
            print_error("%s input '%s' is empty", explicit_component_name(component), request->path);
        }
        goto cleanup;
    }
    if (request->compress) {
        if (is_p12_component(component)) result = compress_p12(&source, &transformed);
        else result = compress_p345(&source, &transformed);
        if (result != 0) goto cleanup;
        blob_free(&source);
        source = transformed;
        blob_init(&transformed);
    }
    if (request->encrypt) {
        if (!is_primary_p12_component(component)) {
            print_error("%s cannot be encrypted", explicit_component_name(component));
            goto cleanup;
        }
        if (encrypt_p12(primary_header, &source, &transformed) != 0) goto cleanup;
        blob_free(&source);
        source = transformed;
        blob_init(&transformed);
    }
    if (is_primary_p12_component(component) && (source.size & 7u) != 0) {
        print_error("raw %s input must be 8-byte aligned; use -encrypt for an unencrypted stream",
                    explicit_component_name(component));
        goto cleanup;
    }
    *output = source;
    blob_init(&source);
    result = 0;

cleanup:
    blob_free(&source);
    blob_free(&transformed);
    return result;
}

static int command_explicit_create(int argc, char **argv)
{
    const char *output_path;
    ExplicitFirmwareRequest request;
    Blob header;
    Blob flash_header;
    Blob base;
    Blob primary_parts[5];
    Blob flash_parts[2];
    Blob output;
    FW_HEADER *primary_header;
    FW_HEADER *secondary_header;
    u32 primary_original_offsets[5];
    u32 primary_alignments[5];
    u32 primary_reserved_sizes[5];
    u32 primary_new_offsets[5];
    int primary_has_override[5];
    u32 primary_override_offsets[5];
    u32 flash_original_offsets[2];
    u32 flash_alignments[2];
    u32 flash_reserved_sizes[2];
    u32 flash_new_offsets[2];
    int flash_has_override[2];
    u32 flash_override_offsets[2];
    u32 primary_end;
    u32 flash_end = HEADER_BYTES;
    u32 minimum_size;
    u32 logical_capacity;
    u32 output_size;
    u32 flash_header_offset = 0;
    u32 flash_header_size = 0;
    u32 base_settings_tail_offset = 0;
    u32 base_settings_tail_size = 0;
    u32 base_copy_source_offset = 0;
    u32 base_copy_target_offset = 0;
    u32 base_copy_size = 0;
    u32 base_copy_expected_size = 0;
    u32 protected_tail_offset = 0;
    u32 protected_tail_size = 0;
    u32 requested_size = 0;
    u16 target_user_settings_offset = 0;
    int automatic_size = 1;
    int has_flashme;
    int flash_header_has_override = 0;
    FirmwareRange ranges[MAX_RANGES];
    int range_count = 0;
    int i;
    int result = 1;

    blob_init(&header);
    blob_init(&flash_header);
    blob_init(&base);
    blob_init(&output);
    for (i = 0; i < 5; i++) blob_init(&primary_parts[i]);
    for (i = 0; i < 2; i++) blob_init(&flash_parts[i]);

    if (argc < 4) {
        print_error("usage: dsfwtool -c OUTPUT.bin -h HEADER.bin -p1 ... -p5 ...");
        goto cleanup;
    }
    output_path = argv[2];
    if (parse_explicit_firmware_request(argc, argv, 3, 1, &request) != 0 ||
        validate_explicit_firmware_request(&request, 1) != 0) goto cleanup;
    if (request.base_path != NULL &&
        (read_file(request.base_path, &base) != 0 ||
         get_base_settings_tail(&base, &base_settings_tail_offset, &base_settings_tail_size) != 0)) {
        goto cleanup;
    }
    if (read_header_file(request.components[FWCOMP_HEADER].path, &header) != 0 ||
        apply_header_edits(&header, &request.edits) != 0) goto cleanup;
    primary_header = (FW_HEADER *)header.data;

    for (i = 0; i < 5; i++) {
        int component = FWCOMP_P1 + i;
        if (prepare_component_for_create(component, &request.components[component], primary_header,
                                         &primary_parts[i]) != 0 ||
            component_span(&primary_parts[i], &primary_reserved_sizes[i]) != 0) goto cleanup;
        primary_original_offsets[i] = primary_part_offset(primary_header, i);
        primary_alignments[i] = primary_part_alignment(primary_header, i);
        primary_has_override[i] = request.components[component].has_offset;
        primary_override_offsets[i] = request.components[component].offset;
    }
    has_flashme = request.components[FWCOMP_FLASH_HEADER].path != NULL;
    if (has_flashme) {
        flash_header_has_override = request.components[FWCOMP_FLASH_HEADER].has_offset;
        if (read_header_file(request.components[FWCOMP_FLASH_HEADER].path, &flash_header) != 0 ||
            sync_flashme_header_from_primary(&header, &flash_header) != 0) goto cleanup;
        secondary_header = (FW_HEADER *)flash_header.data;
        for (i = 0; i < 2; i++) {
            int component = FWCOMP_FLASH_P1 + i;
            if (prepare_component_for_create(component, &request.components[component], primary_header,
                                             &flash_parts[i]) != 0 ||
                component_span(&flash_parts[i], &flash_reserved_sizes[i]) != 0) goto cleanup;
            flash_original_offsets[i] = flashme_part_offset(secondary_header, i);
            flash_alignments[i] = flashme_part_alignment(secondary_header, i);
            flash_has_override[i] = request.components[component].has_offset;
            flash_override_offsets[i] = request.components[component].offset;
        }
        if (build_flashme_combined_positions(primary_original_offsets, primary_alignments,
                                             primary_reserved_sizes, primary_has_override,
                                             primary_override_offsets, flash_original_offsets,
                                             flash_alignments, flash_reserved_sizes,
                                             flash_has_override, flash_override_offsets,
                                             primary_new_offsets, flash_new_offsets) != 0) goto cleanup;
    } else if (build_relocated_positions_with_overrides(primary_original_offsets, primary_alignments,
                                                        primary_reserved_sizes, primary_has_override,
                                                        primary_override_offsets, 5,
                                                        primary_new_offsets) != 0) {
        goto cleanup;
    }
    if (max_reserved_end(primary_new_offsets, primary_reserved_sizes, 5, &primary_end) != 0 ||
        (has_flashme && max_reserved_end(flash_new_offsets, flash_reserved_sizes, 2,
                                          &flash_end) != 0)) goto cleanup;

    if (request.size_text != NULL) {
        if (parse_size(request.size_text, &requested_size, &automatic_size) != 0) goto cleanup;
    }
    minimum_size = primary_end > flash_end ? primary_end : flash_end;
    if (automatic_size) {
        u32 logical_minimum = minimum_size;
        if (has_flashme) {
            if (flash_header_has_override) {
                u32 explicit_header_end;
                if (request.components[FWCOMP_FLASH_HEADER].offset > UINT_MAX - HEADER_BYTES) {
                    print_error("FlashMe header offset exceeds the firmware address space");
                    goto cleanup;
                }
                explicit_header_end = request.components[FWCOMP_FLASH_HEADER].offset + HEADER_BYTES;
                if (logical_minimum < explicit_header_end) logical_minimum = explicit_header_end;
                /* Unlike the standard FlashMe trailer, an explicitly placed
                   secondary header does not imply an unused 0x980-byte tail.
                   Reserve only the final user-settings sectors. */
                if (logical_minimum > UINT_MAX - USER_SETTINGS_BYTES) {
                    print_error("FlashMe layout is too large for a firmware image");
                    goto cleanup;
                }
                logical_minimum += USER_SETTINGS_BYTES;
            } else {
                /* The standard secondary header lives 0x980 bytes before the
                   end of the logical image.  Retain that legacy placement for
                   automatic layouts. */
                if (logical_minimum > UINT_MAX - FLASHME_TRAILER) {
                    print_error("FlashMe layout is too large for a firmware image");
                    goto cleanup;
                }
                logical_minimum += FLASHME_TRAILER;
            }
        } else {
            /* Normal firmware keeps the four Wi-Fi sectors and two user
               settings sectors at the end, whether or not -b supplies the
               bytes to migrate. */
            if (logical_minimum > UINT_MAX - SETTINGS_TAIL_BYTES) {
                print_error("firmware layout is too large for its settings tail");
                goto cleanup;
            }
            logical_minimum += SETTINGS_TAIL_BYTES;
        }
        if (logical_minimum < CAPACITY_UNIT) logical_minimum = CAPACITY_UNIT;
        if (round_up_u32(logical_minimum, CAPACITY_UNIT, &logical_capacity) != 0) {
            print_error("automatic firmware capacity is out of range");
            goto cleanup;
        }
        output_size = logical_capacity;
    } else {
        logical_capacity = requested_size;
        output_size = logical_capacity;
    }
    if (logical_capacity < CAPACITY_UNIT || logical_capacity % CAPACITY_UNIT != 0) {
        print_error("firmware capacity must be 256 KiB or another 256 KiB multiple");
        goto cleanup;
    }
    protected_tail_size = has_flashme ? USER_SETTINGS_BYTES : SETTINGS_TAIL_BYTES;
    if (logical_capacity < protected_tail_size) {
        print_error("firmware capacity is too small for its settings tail");
        goto cleanup;
    }
    protected_tail_offset = logical_capacity - protected_tail_size;
    if (request.base_path != NULL) {
        u32 target_settings_tail_offset;
        if (trailing_settings_location(logical_capacity, &target_settings_tail_offset,
                                       &target_user_settings_offset) != 0 ||
            set_header_user_settings_offset(&header, target_user_settings_offset) != 0 ||
            (has_flashme && set_header_user_settings_offset(&flash_header,
                                                            target_user_settings_offset) != 0)) {
            goto cleanup;
        }
        base_copy_source_offset = base_settings_tail_offset;
        base_copy_target_offset = target_settings_tail_offset;
        base_copy_size = base_settings_tail_size;
        base_copy_expected_size = SETTINGS_TAIL_BYTES;
    }
    if (has_flashme) {
        flash_header_offset = flash_header_has_override ?
            request.components[FWCOMP_FLASH_HEADER].offset : logical_capacity - FLASHME_TRAILER;
        if (flash_header_offset > output_size || HEADER_BYTES > output_size - flash_header_offset) {
            print_error("the output is too short to contain the FlashMe secondary header");
            goto cleanup;
        }
        flash_header_size = HEADER_BYTES;
    }
    if (primary_end > output_size || flash_end > output_size) {
        print_error("the selected output size is too small for these components");
        goto cleanup;
    }

    if (add_firmware_range(ranges, &range_count, 0, HEADER_BYTES, output_size, "primary header") != 0) goto cleanup;
    if (add_firmware_range(ranges, &range_count, protected_tail_offset, protected_tail_size,
                           output_size, has_flashme ? "FlashMe user-settings tail" :
                           "Wi-Fi and user-settings tail") != 0) {
        goto cleanup;
    }
    if (has_flashme &&
        add_firmware_range(ranges, &range_count, flash_header_offset, flash_header_size, output_size,
                           "FlashMe secondary header") != 0) {
        goto cleanup;
    }
    for (i = 0; i < 5; i++) {
        if (add_firmware_range(ranges, &range_count, primary_new_offsets[i], primary_reserved_sizes[i],
                               output_size, primary_part_labels[i]) != 0) goto cleanup;
    }
    if (has_flashme) {
        for (i = 0; i < 2; i++) {
            if (add_firmware_range(ranges, &range_count, flash_new_offsets[i], flash_reserved_sizes[i],
                                   output_size, flashme_part_names[i]) != 0) goto cleanup;
        }
    }

    if (set_primary_component_offsets(primary_header, primary_new_offsets) != 0 ||
        update_header_config_checksum(&header) != 0 ||
        calculate_primary_crcs(primary_header, primary_parts, &primary_header->part12_crc16,
                               &primary_header->part34_crc16, &primary_header->part5_crc16) != 0) {
        goto cleanup;
    }
    if (has_flashme) {
        if (sync_flashme_header_from_primary(&header, &flash_header) != 0) goto cleanup;
        secondary_header = (FW_HEADER *)flash_header.data;
        if (set_flashme_component_offsets(secondary_header, flash_new_offsets) != 0) goto cleanup;
    }

    if (blob_alloc(&output, output_size) != 0) goto cleanup;
    memset(output.data, request.fill_byte, output.size);
    memcpy(output.data, header.data, HEADER_BYTES);
    /* A normal image reserves the whole copied 0x600 tail.  FlashMe v1--v4,
       however, may put code in the preceding Wi-Fi area, so lay down the
       copied base bytes first in that mode and let an explicitly placed
       component deliberately replace only the bytes it owns. */
    if (request.base_path != NULL && has_flashme) {
        memcpy(output.data + base_copy_target_offset,
               base.data + base_copy_source_offset, base_copy_size);
    }
    for (i = 0; i < 5; i++) {
        memcpy(output.data + primary_new_offsets[i], primary_parts[i].data, primary_parts[i].size);
    }
    if (has_flashme) {
        memcpy(output.data + flash_header_offset, flash_header.data, HEADER_BYTES);
        for (i = 0; i < 2; i++) {
            memcpy(output.data + flash_new_offsets[i], flash_parts[i].data, flash_parts[i].size);
        }
    }
    if (write_p345_alignment_padding(&output, primary_new_offsets, primary_parts,
                                     flash_new_offsets, has_flashme, flash_header_offset,
                                     protected_tail_offset) != 0) goto cleanup;
    if (request.base_path != NULL && !has_flashme) {
        memcpy(output.data + base_copy_target_offset,
               base.data + base_copy_source_offset, base_copy_size);
    }
    if (write_file(output_path, output.data, output.size) != 0) goto cleanup;

    printf("Created %s (0x%08X bytes, fill=%02X)\n", output_path, output_size, request.fill_byte);
    for (i = 0; i < 5; i++) {
        printf("  P%d offset=0x%06X data=0x%06lX span=0x%06X\n", i + 1,
               primary_new_offsets[i], (unsigned long)primary_parts[i].size, primary_reserved_sizes[i]);
    }
    if (has_flashme) {
        printf("  FlashMe header offset=0x%06X\n", flash_header_offset);
        for (i = 0; i < 2; i++) {
            printf("  FP%d offset=0x%06X data=0x%06lX span=0x%06X\n", i + 1,
                   flash_new_offsets[i], (unsigned long)flash_parts[i].size, flash_reserved_sizes[i]);
        }
    }
    if (request.base_path != NULL) {
        printf("  Base settings: %s  source=0x%06X data=0x%03X target=0x%06X\n",
               request.base_path, base_copy_source_offset, base_copy_size,
               base_copy_target_offset);
        if (base_copy_size != base_copy_expected_size) {
            fprintf(stderr, "dsfwtool: warning: base firmware is missing final settings bytes; "
                    "the remaining 0x%03X bytes use the output fill value\n",
                    base_copy_expected_size - base_copy_size);
        }
    }
    result = 0;

cleanup:
    blob_free(&header);
    blob_free(&flash_header);
    blob_free(&base);
    blob_free(&output);
    for (i = 0; i < 5; i++) blob_free(&primary_parts[i]);
    for (i = 0; i < 2; i++) blob_free(&flash_parts[i]);
    return result;
}

static int parse_explicit_stream_request(int argc, char **argv, ExplicitStreamRequest *request)
{
    int i;
    memset(request, 0, sizeof(*request));
    request->component = -1;

    for (i = 1; i < argc; i++) {
        int component;
        if (strcmp(argv[i], "-h") == 0) {
            if (take_option_value(argc, argv, &i, "-h", &request->header_path) != 0) return -1;
            continue;
        }
        component = explicit_component_kind(argv[i]);
        if (component >= 0) {
            if (component == FWCOMP_HEADER || component == FWCOMP_FLASH_HEADER) {
                print_error("%s is not a standalone stream type", argv[i]);
                return -1;
            }
            if (request->component >= 0) {
                print_error("select exactly one component type for an independent operation");
                return -1;
            }
            request->component = component;
            continue;
        }
        if (strcmp(argv[i], "-o") == 0) {
            if (take_option_value(argc, argv, &i, "-o", &request->output_path) != 0) return -1;
            continue;
        }
        if (strcmp(argv[i], "-comp") == 0) {
            if (request->component < 0) {
                print_error("declare -p1, -p2, -p3, -p4, -p5, -fp1, or -fp2 before -comp");
                return -1;
            }
            request->compress = 1;
            continue;
        }
        if (strcmp(argv[i], "-uncomp") == 0) {
            if (request->component < 0) {
                print_error("declare a component type before -uncomp");
                return -1;
            }
            request->uncompress = 1;
            continue;
        }
        if (is_encrypt_option(argv[i])) {
            if (request->component < 0) {
                print_error("declare a component type before %s", argv[i]);
                return -1;
            }
            request->encrypt = 1;
            continue;
        }
        if (strcmp(argv[i], "-decrypt") == 0 || strcmp(argv[i], "-de") == 0) {
            if (request->component < 0) {
                print_error("declare a component type before %s", argv[i]);
                return -1;
            }
            request->decrypt = 1;
            continue;
        }
        if (argv[i][0] == '-') {
            print_error("unknown option '%s'", argv[i]);
            return -1;
        }
        if (request->input_path != NULL) {
            print_error("only one independent input file may be specified");
            return -1;
        }
        request->input_path = argv[i];
    }

    if (request->component < 0 || request->input_path == NULL || request->output_path == NULL) {
        print_error("independent operations require COMPONENT INPUT.bin -o OUTPUT.bin");
        return -1;
    }
    if (is_primary_p12_component(request->component)) {
        if (!request->compress && !request->uncompress && !request->encrypt && !request->decrypt) {
            print_error("select -comp, -uncomp, -crypt, or -decrypt");
            return -1;
        }
        if ((request->compress && request->uncompress) || (request->encrypt && request->decrypt) ||
            (request->compress && request->decrypt) || (request->uncompress && request->encrypt)) {
            print_error("P1/P2 supports only decrypt+uncomp, comp+crypt, or one individual operation");
            return -1;
        }
        if ((request->encrypt || request->decrypt) && request->header_path == NULL) {
            print_error("P1/P2 -crypt/-decrypt requires -h HEADER.bin");
            return -1;
        }
    } else {
        if (request->encrypt || request->decrypt) {
            print_error("%s has no encryption layer", explicit_component_name(request->component));
            return -1;
        }
        if (request->compress == request->uncompress) {
            print_error("%s requires exactly one of -comp or -uncomp", explicit_component_name(request->component));
            return -1;
        }
    }
    return 0;
}

static int command_explicit_stream(int argc, char **argv)
{
    ExplicitStreamRequest request;
    Blob input;
    Blob header;
    Blob stage1;
    Blob stage2;
    Blob stage3;
    const Blob *current;
    const FW_HEADER *key_header;
    int result = 1;

    blob_init(&input);
    blob_init(&header);
    blob_init(&stage1);
    blob_init(&stage2);
    blob_init(&stage3);
    if (parse_explicit_stream_request(argc, argv, &request) != 0) goto cleanup;
    if (read_file(request.input_path, &input) != 0 || input.size == 0) {
        if (input.size == 0) print_error("input '%s' is empty", request.input_path);
        goto cleanup;
    }
    if (request.encrypt || request.decrypt) {
        if (read_header_file(request.header_path, &header) != 0) goto cleanup;
    }
    key_header = (const FW_HEADER *)header.data;
    current = &input;

    if (request.decrypt) {
        if (decrypt_p12(key_header, current, &stage1) != 0) goto cleanup;
        current = &stage1;
    }
    if (request.uncompress) {
        int decode_result = is_p12_component(request.component)
                                ? decode_p12(current->data, current->size, &stage2)
                                : decode_p345(current->data, current->size, &stage2);
        if (decode_result != 0) goto cleanup;
        current = &stage2;
    }
    if (request.compress) {
        int compress_result = is_p12_component(request.component)
                                  ? compress_p12(current, &stage3)
                                  : compress_p345(current, &stage3);
        if (compress_result != 0) goto cleanup;
        current = &stage3;
    }
    if (request.encrypt) {
        Blob encrypted;
        blob_init(&encrypted);
        if (encrypt_p12(key_header, current, &encrypted) != 0) {
            blob_free(&encrypted);
            goto cleanup;
        }
        blob_free(&stage3);
        stage3 = encrypted;
        current = &stage3;
    }
    if (write_file(request.output_path, current->data, current->size) != 0) goto cleanup;
    printf("Wrote %s using %s\n", request.output_path, explicit_component_name(request.component));
    result = 0;

cleanup:
    blob_free(&input);
    blob_free(&header);
    blob_free(&stage1);
    blob_free(&stage2);
    blob_free(&stage3);
    return result;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage();
        return 1;
    }
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "-?") == 0 ||
        strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "help") == 0) {
        print_usage();
        return 0;
    }
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "version") == 0) {
        printf("dsfwtool v%s\n", DSFWTOOL_VERSION);
        return 0;
    }
    if (strcmp(argv[1], "-i") == 0) return command_explicit_info(argc, argv);
    if (strcmp(argv[1], "-x") == 0) return command_explicit_extract(argc, argv);
    if (strcmp(argv[1], "-c") == 0) return command_explicit_create(argc, argv);
    if (explicit_component_kind(argv[1]) >= FWCOMP_P1) return command_explicit_stream(argc, argv);

    print_error("unknown command '%s'", argv[1]);
    print_usage();
    return 1;
}
