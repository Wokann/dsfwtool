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
#define HEADER_BYTES (FW_HEADER_SIZE - 0x80u)
#define CAPACITY_UNIT 0x40000u
#define FLASHME_TRAILER 0x980u
#define PATH_BUFFER_SIZE 2048
#define MAX_HEADER_EDITS 32
#define MAX_RANGES 10
#define MAX_RELOCATED_COMPONENTS 7

#if defined(__GNUC__) || defined(__clang__)
#define DSFWTOOL_UNUSED __attribute__((unused))
#else
#define DSFWTOOL_UNUSED
#endif

enum {
    SELECT_HEADER = 1u << 0,
    SELECT_P1 = 1u << 1,
    SELECT_P2 = 1u << 2,
    SELECT_P3 = 1u << 3,
    SELECT_P4 = 1u << 4,
    SELECT_P5 = 1u << 5,
    SELECT_FLASHME = 1u << 6,
    SELECT_PRIMARY = SELECT_P1 | SELECT_P2 | SELECT_P3 | SELECT_P4 | SELECT_P5,
    SELECT_ALL = SELECT_HEADER | SELECT_PRIMARY | SELECT_FLASHME
};

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
    int has_image_size;
    u32 image_size;
    int has_flashme_header_offset;
    u32 flashme_header_offset;
} LayoutMetadata;

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

/* Compatibility helpers for the legacy implementation below. */
static DSFWTOOL_UNUSED int file_exists(const char *path)
{
    return access(path, 0) == 0;
}

static int join_path(char *output, size_t output_size, const char *directory, const char *name)
{
    size_t directory_length = strlen(directory);
    const char *separator = (directory_length != 0 &&
                             (directory[directory_length - 1] == '/' || directory[directory_length - 1] == '\\'))
                                ? ""
                                : "/";
    if (snprintf(output, output_size, "%s%s%s", directory, separator, name) >= (int)output_size) {
        print_error("combined path is too long: '%s' + '%s'", directory, name);
        return -1;
    }
    return 0;
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

static int equal_ci(const char *left, const char *right)
{
    while (*left && *right) {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) return 0;
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
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
    u32 padded;
    int decrypted_size;

    if (span < 4 || round_up_u32(span, 8, &padded) != 0) return -1;
    blob_init(&decrypted);
    if (blob_alloc(&decrypted, padded) != 0) return -1;
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
    u32 candidates[3];
    int count = 0;
    int i;

    if (image->size < HEADER_BYTES) return;
    if (image->size >= FLASHME_TRAILER) {
        add_flashme_candidate(candidates, &count, (u32)image->size - FLASHME_TRAILER, image->size);
    }
    add_flashme_candidate(candidates, &count, 0x3F680u, image->size);
    add_flashme_candidate(candidates, &count, 0x7F680u, image->size);

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

static DSFWTOOL_UNUSED void print_ascii_identifier(const u8 identifier[4])
{
    fprint_ascii_identifier(stdout, identifier);
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
        u16 calculated = swiCRC(0, (u32 *)(bytes + 0x2C), config_length);
        fprintf(file, "  Wi-Fi config CRC: 0x%04X  length=0x%04X (%s)\n",
                config_checksum, config_length, calculated == config_checksum ? "valid" : "mismatch");
    } else {
        fprintf(file, "  Wi-Fi config CRC: 0x%04X  length=0x%04X (out of range)\n",
                config_checksum, config_length);
    }
}

static DSFWTOOL_UNUSED void print_header_fields(const FW_HEADER *header, const char *label)
{
    fprint_header_fields(stdout, header, label);
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

static DSFWTOOL_UNUSED int parse_component_selection(const char *text, unsigned int *selection)
{
    char buffer[256];
    char *token;
    unsigned int result = 0;

    if (snprintf(buffer, sizeof(buffer), "%s", text) >= (int)sizeof(buffer)) {
        print_error("component selection is too long");
        return -1;
    }
    token = strtok(buffer, ",");
    while (token != NULL) {
        if (equal_ci(token, "all")) result |= SELECT_ALL;
        else if (equal_ci(token, "header")) result |= SELECT_HEADER;
        else if (equal_ci(token, "p1") || equal_ci(token, "part1") || equal_ci(token, "arm9boot")) result |= SELECT_P1;
        else if (equal_ci(token, "p2") || equal_ci(token, "part2") || equal_ci(token, "arm7boot")) result |= SELECT_P2;
        else if (equal_ci(token, "p3") || equal_ci(token, "part3") || equal_ci(token, "arm9gui")) result |= SELECT_P3;
        else if (equal_ci(token, "p4") || equal_ci(token, "part4") || equal_ci(token, "arm7wifi")) result |= SELECT_P4;
        else if (equal_ci(token, "p5") || equal_ci(token, "part5") || equal_ci(token, "gfx")) result |= SELECT_P5;
        else if (equal_ci(token, "flashme")) result |= SELECT_FLASHME;
        else {
            print_error("unknown component selector '%s'", token);
            return -1;
        }
        token = strtok(NULL, ",");
    }
    if (result == 0) {
        print_error("component selection is empty");
        return -1;
    }
    *selection = result;
    return 0;
}

static DSFWTOOL_UNUSED int write_layout_metadata(const char *directory, size_t image_size, const FirmwareLayout *layout)
{
    char path[PATH_BUFFER_SIZE];
    FILE *file;
    if (join_path(path, sizeof(path), directory, "fwtool.meta") != 0) return -1;
    if (ensure_parent_directory(path) != 0) return -1;
    file = fopen(path, "wb");
    if (file == NULL) {
        print_error("cannot create '%s'", path);
        return -1;
    }
    fprintf(file, "FWTOOL-META 1\n");
    fprintf(file, "image_size=0x%08lX\n", (unsigned long)image_size);
    fprintf(file, "flashme_header_offset=");
    if (layout->has_flashme) fprintf(file, "0x%08X\n", layout->flashme_header_offset);
    else fprintf(file, "none\n");
    if (fclose(file) != 0) {
        print_error("cannot close '%s'", path);
        return -1;
    }
    return 0;
}

static DSFWTOOL_UNUSED void read_layout_metadata(const char *path, LayoutMetadata *metadata)
{
    FILE *file;
    char line[128];
    memset(metadata, 0, sizeof(*metadata));
    file = fopen(path, "rb");
    if (file == NULL) return;
    while (fgets(line, sizeof(line), file) != NULL) {
        char *newline = strpbrk(line, "\r\n");
        u32 value;
        if (newline != NULL) *newline = '\0';
        if (strncmp(line, "image_size=", 11) == 0 && parse_u32(line + 11, &value) == 0) {
            metadata->has_image_size = 1;
            metadata->image_size = value;
        } else if (strncmp(line, "flashme_header_offset=", 23) == 0 &&
                   strcmp(line + 23, "none") != 0 && parse_u32(line + 23, &value) == 0) {
            metadata->has_flashme_header_offset = 1;
            metadata->flashme_header_offset = value;
        }
    }
    fclose(file);
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
    checksum = swiCRC(0, (u32 *)(header_blob->data + 0x2C), config_length);
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

static DSFWTOOL_UNUSED int read_aligned_component(const char *path, u32 alignment, int require_eight_byte_blocks, Blob *component)
{
    Blob raw;
    u32 aligned_size;

    blob_init(&raw);
    blob_init(component);
    if (read_file(path, &raw) != 0) return -1;
    if (raw.size == 0 || raw.size > UINT_MAX) {
        print_error("component '%s' is empty or too large", path);
        blob_free(&raw);
        return -1;
    }
    if (require_eight_byte_blocks && (raw.size & 7u) != 0) {
        print_error("encrypted component '%s' is not 8-byte aligned", path);
        blob_free(&raw);
        return -1;
    }
    if (round_up_u32((u32)raw.size, alignment, &aligned_size) != 0) {
        print_error("component '%s' is too large to align", path);
        blob_free(&raw);
        return -1;
    }
    if (blob_alloc(component, aligned_size) != 0) {
        blob_free(&raw);
        return -1;
    }
    memset(component->data, 0, component->size);
    memcpy(component->data, raw.data, raw.size);
    blob_free(&raw);
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
    actual_size = decompressLZ77(plain->data, (u8 *)compressed);
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
    u32 padded_size;
    int output_size;

    blob_init(decrypted);
    if (encrypted->size == 0 || encrypted->size > UINT_MAX ||
        round_up_u32((u32)encrypted->size, 8, &padded_size) != 0) {
        print_error("invalid encrypted P1/P2 component size");
        return -1;
    }
    if (blob_alloc(decrypted, padded_size) != 0) return -1;
    init_keycode(read_le32(header->fw_identifier), 2, 0x0C);
    output_size = decrypt_buffer(encrypted->data, decrypted->data, (int)encrypted->size);
    if (output_size < 0 || (u32)output_size != padded_size) {
        print_error("P1/P2 decryption failed");
        blob_free(decrypted);
        return -1;
    }
    return 0;
}

static int encrypt_p12(const FW_HEADER *header, const Blob *compressed, Blob *encrypted)
{
    u32 padded_size;
    int output_size;

    blob_init(encrypted);
    if (compressed->size == 0 || compressed->size > UINT_MAX ||
        round_up_u32((u32)compressed->size, 8, &padded_size) != 0) {
        print_error("invalid P1/P2 compressed component size");
        return -1;
    }
    if (blob_alloc(encrypted, padded_size) != 0) return -1;
    init_keycode(read_le32(header->fw_identifier), 2, 0x0C);
    output_size = encrypt_buffer(compressed->data, encrypted->data, (int)compressed->size);
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

    crc = swiCRC(0xFFFF, (u32 *)plain[0].data, (u32)plain[0].size);
    *part12_crc = swiCRC(crc, (u32 *)plain[1].data, (u32)plain[1].size);
    crc = swiCRC(0xFFFF, (u32 *)plain[2].data, (u32)plain[2].size);
    *part34_crc = swiCRC(crc, (u32 *)plain[3].data, (u32)plain[3].size);
    *part5_crc = swiCRC(0xFFFF, (u32 *)plain[4].data, (u32)plain[4].size);
    result = 0;

cleanup:
    for (i = 0; i < 2; i++) blob_free(&decrypted[i]);
    for (i = 0; i < 5; i++) blob_free(&plain[i]);
    return result;
}

static DSFWTOOL_UNUSED int calculate_flashme_crc(Blob parts[2], u16 *part12_crc)
{
    Blob plain[2];
    u16 first_crc;
    int i;
    int result = -1;

    blob_init(&plain[0]);
    blob_init(&plain[1]);
    for (i = 0; i < 2; i++) {
        if (decode_p12(parts[i].data, parts[i].size, &plain[i]) != 0) {
            print_error("%s cannot be decompressed", flashme_part_names[i]);
            goto cleanup;
        }
    }
    first_crc = swiCRC(0xFFFF, (u32 *)plain[0].data, (u32)plain[0].size);
    *part12_crc = swiCRC(first_crc, (u32 *)plain[1].data, (u32)plain[1].size);
    result = 0;

cleanup:
    blob_free(&plain[0]);
    blob_free(&plain[1]);
    return result;
}

static int build_relocated_positions_aligned(const u32 *original_offsets, const u32 *alignments,
                                             const u32 *reserved_sizes, int count, u32 *new_offsets)
{
    int indices[MAX_RELOCATED_COMPONENTS];
    int i;

    if (count <= 0 || count > MAX_RELOCATED_COMPONENTS) return -1;
    sort_indices_by_offset(original_offsets, count, indices);
    for (i = 0; i < count; i++) {
        int index = indices[i];
        u32 candidate;
        if (original_offsets[index] < HEADER_BYTES) {
            print_error("component %d has an invalid original offset", index + 1);
            return -1;
        }
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
        if (round_up_u32(candidate, alignments[index], &new_offsets[index]) != 0) {
            print_error("component layout cannot be aligned");
            return -1;
        }
    }
    return 0;
}

static DSFWTOOL_UNUSED int build_relocated_positions(const u32 *original_offsets, const Blob *parts,
                                     int count, u32 *new_offsets)
{
    u32 alignments[MAX_RELOCATED_COMPONENTS];
    u32 reserved_sizes[MAX_RELOCATED_COMPONENTS];
    int i;
    if (count <= 0 || count > MAX_RELOCATED_COMPONENTS) return -1;
    for (i = 0; i < count; i++) {
        alignments[i] = 1;
        if (parts[i].size > UINT_MAX) return -1;
        reserved_sizes[i] = (u32)parts[i].size;
    }
    return build_relocated_positions_aligned(original_offsets, alignments, reserved_sizes,
                                             count, new_offsets);
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
                                            const u32 flash_original_offsets[2],
                                            const u32 flash_alignments[2],
                                            const u32 flash_sizes[2],
                                            u32 primary_new_offsets[5],
                                            u32 flash_new_offsets[2])
{
    u32 original_offsets[MAX_RELOCATED_COMPONENTS];
    u32 alignments[MAX_RELOCATED_COMPONENTS];
    u32 sizes[MAX_RELOCATED_COMPONENTS];
    u32 new_offsets[MAX_RELOCATED_COMPONENTS];
    int i;

    for (i = 0; i < 5; i++) {
        original_offsets[i] = primary_original_offsets[i];
        alignments[i] = primary_alignments[i];
        sizes[i] = primary_sizes[i];
    }
    for (i = 0; i < 2; i++) {
        original_offsets[5 + i] = flash_original_offsets[i];
        alignments[5 + i] = flash_alignments[i];
        sizes[5 + i] = flash_sizes[i];
    }
    if (build_relocated_positions_aligned(original_offsets, alignments, sizes,
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

static DSFWTOOL_UNUSED int max_component_end(const u32 *offsets, const Blob *parts, int count, u32 *end)
{
    int i;
    *end = HEADER_BYTES;
    for (i = 0; i < count; i++) {
        u32 component_end;
        if (parts[i].size > UINT_MAX - offsets[i]) {
            print_error("component size overflows the firmware address space");
            return -1;
        }
        component_end = offsets[i] + (u32)parts[i].size;
        if (component_end > *end) *end = component_end;
    }
    return 0;
}

static void print_usage(void)
{
    printf("dsfwtool v%s - Nintendo DS firmware toolkit\n", DSFWTOOL_VERSION);
    printf("\n");
    printf("All -x and -c artifacts are explicit files; no directory or manifest mode exists.\n");
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
    printf("      -p1 [-encrypt | -comp -encrypt] FILE\n");
    printf("      -p2 [-encrypt | -comp -encrypt] FILE\n");
    printf("      -p3 [-comp] FILE -p4 [-comp] FILE -p5 [-comp] FILE\n");
    printf("      [-s 256K|512K|1M|auto] [-b BASE.bin] [--fill 00|FF]\n");
    printf("  P1/P2 with no modifier are already encrypted P12 streams.  -encrypt\n");
    printf("  encrypts an already compressed P12 stream; -comp -encrypt compresses\n");
    printf("  plain data before encryption.  P3/P4/P5 -comp uses P345 compression.\n");
    printf("  -b preserves non-component bytes from an explicit base image.  Without\n");
    printf("  -b, unused bytes are filled with FF by default (override with --fill).\n");
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
    printf("\n");
    printf("FlashMe components (select as needed with -x; supply all three with -c):\n");
    printf("  -fh FLASH_HEADER.bin  -fp1 [-comp|-uncomp] FILE  -fp2 [-comp|-uncomp] FILE\n");
    printf("  dsfwtool -fp1 -uncomp INPUT.bin -o OUTPUT.bin\n");
    printf("  dsfwtool -fp1 -comp INPUT.bin -o OUTPUT.bin\n");
    printf("  FlashMe FP1/FP2 are unencrypted P12/LZ77 streams; -encrypt/-decrypt is invalid.\n");
    printf("  With -c -b, an 0x...FE00 FlashMe dump keeps its truncated physical length;\n");
    printf("  use -s to write the full logical 256 KiB multiple instead.\n");
    printf("\n");
    printf("Header edits for -c (applied before P1/P2 encryption):\n");
    printf("  --identifier ABCD  --console-type VALUE  --timestamp YYMMDDHHMM\n");
    printf("  --shift-amounts VALUE  --part1-ramaddr VALUE  --part2-ramaddr VALUE\n");
    printf("  --settings-offset VALUE  --set-u8 OFFSET VALUE  --set-u16 OFFSET VALUE\n");
    printf("  --set-u32 OFFSET VALUE\n");
    printf("  ROM offsets and component CRCs are recalculated, not user-settable.\n");
    printf("  --shift-amounts and --settings-offset are advanced: the former controls\n");
    printf("  P1/P2 alignment and RAM interpretation; the latter must match user-data\n");
    printf("  blocks retained in -b or written by a future settings operation.\n");
}

/* The former directory/manifest command family was superseded by the
   explicit-file interface below.  It remains excluded while the historical
   source is retained for reference; no command dispatch can reach it. */
#if 0
static int require_option_value(int argc, char **argv, int *index, const char *option, const char **output)
{
    if (*index + 1 >= argc) {
        print_error("%s requires a value", option);
        return -1;
    }
    *output = argv[++*index];
    return 0;
}

static int command_info(int argc, char **argv)
{
    const char *firmware = NULL;
    Blob image;
    FirmwareLayout layout;
    int i;
    int result = 1;

    blob_init(&image);
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--firmware") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &firmware) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage();
            result = 0;
            goto cleanup;
        } else {
            print_error("unknown info option '%s'", argv[i]);
            goto cleanup;
        }
    }
    if (firmware == NULL) {
        print_error("info requires -f/--firmware");
        goto cleanup;
    }
    if (read_file(firmware, &image) != 0 || parse_firmware_layout(&image, &layout, 1) != 0) goto cleanup;
    print_layout(firmware, &image, &layout);
    result = 0;

cleanup:
    blob_free(&image);
    return result;
}

typedef struct {
    const char *firmware;
    const char *directory;
    const char *selection;
    const char *header_out;
    const char *part_out[5];
    const char *flash_header_out;
    const char *flash_part_out[2];
    int no_base;
} UnpackArguments;

static int output_path_or_default(char *path, size_t path_size, const char *directory,
                                  const char *override_path, const char *default_name)
{
    if (override_path != NULL) {
        if (snprintf(path, path_size, "%s", override_path) >= (int)path_size) {
            print_error("output path is too long");
            return -1;
        }
        return 0;
    }
    if (directory == NULL) {
        print_error("no output path was provided for '%s'", default_name);
        return -1;
    }
    return join_path(path, path_size, directory, default_name);
}

static int command_unpack(int argc, char **argv)
{
    UnpackArguments arguments;
    Blob image;
    FirmwareLayout layout;
    unsigned int selection = SELECT_ALL;
    char path[PATH_BUFFER_SIZE];
    int i;
    int result = 1;

    memset(&arguments, 0, sizeof(arguments));
    blob_init(&image);
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--firmware") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.firmware) != 0) goto cleanup;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--directory") == 0 ||
                   strcmp(argv[i], "--output-directory") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.directory) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--only") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.selection) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--header-out") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.header_out) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--p1-out") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.part_out[0]) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--p2-out") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.part_out[1]) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--p3-out") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.part_out[2]) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--p4-out") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.part_out[3]) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--p5-out") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.part_out[4]) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--flash-header-out") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.flash_header_out) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--flash-p1-out") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.flash_part_out[0]) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--flash-p2-out") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.flash_part_out[1]) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--no-base") == 0) {
            arguments.no_base = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage();
            result = 0;
            goto cleanup;
        } else {
            print_error("unknown unpack option '%s'", argv[i]);
            goto cleanup;
        }
    }
    if (arguments.firmware == NULL) {
        print_error("unpack requires -f/--firmware");
        goto cleanup;
    }
    if (arguments.directory == NULL) {
        selection = 0;
        arguments.no_base = 1;
        if (arguments.selection != NULL) {
            print_error("--only requires -d/--directory; use explicit --*-out paths without a directory");
            goto cleanup;
        }
    } else if (arguments.selection != NULL && parse_component_selection(arguments.selection, &selection) != 0) {
        goto cleanup;
    }
    if (arguments.header_out != NULL) selection |= SELECT_HEADER;
    for (i = 0; i < 5; i++) if (arguments.part_out[i] != NULL) selection |= (SELECT_P1 << i);
    if (arguments.flash_header_out != NULL || arguments.flash_part_out[0] != NULL ||
        arguments.flash_part_out[1] != NULL) selection |= SELECT_FLASHME;
    if (selection == 0) {
        print_error("unpack without -d/--directory requires at least one explicit --*-out path");
        goto cleanup;
    }

    if (read_file(arguments.firmware, &image) != 0 || parse_firmware_layout(&image, &layout, 1) != 0) goto cleanup;
    if (arguments.directory != NULL && mkdir_recursive(arguments.directory) != 0) goto cleanup;

    if (selection & SELECT_HEADER) {
        if (output_path_or_default(path, sizeof(path), arguments.directory, arguments.header_out, "header.bin") != 0 ||
            write_file(path, image.data, HEADER_BYTES) != 0) goto cleanup;
        printf("Exported header: %s\n", path);
    }
    for (i = 0; i < 5; i++) {
        unsigned int bit = SELECT_P1 << i;
        u32 export_size;
        if ((selection & bit) == 0) continue;
        export_size = layout.effective_sizes[i];
        if (i < 2 && round_up_u32(export_size, 8, &export_size) != 0) {
            print_error("cannot align %s for encryption", primary_part_names[i]);
            goto cleanup;
        }
        if (export_size > layout.spans[i]) {
            print_error("%s effective size exceeds its allocated span", primary_part_names[i]);
            goto cleanup;
        }
        if (output_path_or_default(path, sizeof(path), arguments.directory, arguments.part_out[i],
                                   primary_part_names[i]) != 0 ||
            write_file(path, image.data + layout.offsets[i], export_size) != 0) goto cleanup;
        printf("Exported %s: %s\n", primary_part_labels[i], path);
    }
    if ((selection & SELECT_FLASHME) != 0) {
        if (!layout.has_flashme) {
            printf("No FlashMe secondary header was detected; no FlashMe files were exported.\n");
        } else {
            if (output_path_or_default(path, sizeof(path), arguments.directory, arguments.flash_header_out,
                                       "header_flashme.bin") != 0 ||
                write_file(path, image.data + layout.flashme_header_offset, HEADER_BYTES) != 0) goto cleanup;
            printf("Exported FlashMe header: %s\n", path);
            for (i = 0; i < 2; i++) {
                if (layout.flashme_effective_sizes[i] > layout.flashme_spans[i]) {
                    print_error("%s effective size exceeds its allocated span", flashme_part_names[i]);
                    goto cleanup;
                }
                if (output_path_or_default(path, sizeof(path), arguments.directory, arguments.flash_part_out[i],
                                           flashme_part_names[i]) != 0 ||
                    write_file(path, image.data + layout.flashme_offsets[i],
                               layout.flashme_effective_sizes[i]) != 0) goto cleanup;
                printf("Exported FlashMe component: %s\n", path);
            }
        }
    }
    if (!arguments.no_base) {
        if (join_path(path, sizeof(path), arguments.directory, "firmware-base.bin") != 0 ||
            write_file(path, image.data, image.size) != 0) goto cleanup;
        printf("Preserved base image: %s\n", path);
    }
    if (arguments.directory != NULL) {
        if (write_layout_metadata(arguments.directory, image.size, &layout) != 0) goto cleanup;
        printf("Unpack complete: %s\n", arguments.directory);
    } else {
        printf("Unpack complete.\n");
    }
    result = 0;

cleanup:
    blob_free(&image);
    return result;
}

static int command_comp(int argc, char **argv, int forced_mode)
{
    const char *type = NULL;
    const char *input_path = NULL;
    const char *output_path = NULL;
    int compress = forced_mode;
    Blob input;
    Blob output;
    int i;
    int result = 1;

    blob_init(&input);
    blob_init(&output);
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-p12") == 0) {
            if (type != NULL && strcmp(type, "12") != 0) {
                print_error("only one component type may be selected");
                goto cleanup;
            }
            type = "12";
        } else if (strcmp(argv[i], "-p345") == 0) {
            if (type != NULL && strcmp(type, "345") != 0) {
                print_error("only one component type may be selected");
                goto cleanup;
            }
            type = "345";
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--type") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &type) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--compress") == 0 || strcmp(argv[i], "-co") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &input_path) != 0) goto cleanup;
            if (forced_mode == 0) {
                print_error("uncomp cannot be combined with --compress");
                goto cleanup;
            }
            compress = 1;
        } else if (strcmp(argv[i], "--decompress") == 0 || strcmp(argv[i], "-de") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &input_path) != 0) goto cleanup;
            if (forced_mode == 1) {
                print_error("compress cannot be combined with --decompress");
                goto cleanup;
            }
            compress = 0;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--input") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &input_path) != 0) goto cleanup;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &output_path) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage();
            result = 0;
            goto cleanup;
        } else if (argv[i][0] != '-') {
            if (input_path != NULL) {
                print_error("input file is already specified");
                goto cleanup;
            }
            input_path = argv[i];
        } else {
            print_error("unknown comp option '%s'", argv[i]);
            goto cleanup;
        }
    }
    if (type == NULL || input_path == NULL || output_path == NULL || compress < 0 ||
        (strcmp(type, "12") != 0 && strcmp(type, "345") != 0)) {
        print_error("comp requires -p12 or -p345, one operation, and -o/--output");
        goto cleanup;
    }
    if (read_file(input_path, &input) != 0) goto cleanup;
    if (strcmp(type, "12") == 0) {
        if (compress ? compress_p12(&input, &output) : decode_p12(input.data, input.size, &output)) goto cleanup;
    } else {
        if (compress ? compress_p345(&input, &output) : decode_p345(input.data, input.size, &output)) goto cleanup;
    }
    if (write_file(output_path, output.data, output.size) != 0) goto cleanup;
    printf("%s P%s: %s -> %s (0x%lX bytes)\n",
           compress ? "Compressed" : "Decompressed", type, input_path, output_path,
           (unsigned long)output.size);
    result = 0;

cleanup:
    blob_free(&input);
    blob_free(&output);
    return result;
}

static int command_crypt(int argc, char **argv, int forced_mode)
{
    const char *header_path = NULL;
    const char *input_path = NULL;
    const char *output_path = NULL;
    int encrypt = forced_mode;
    Blob header;
    Blob input;
    Blob output;
    int i;
    int result = 1;

    blob_init(&header);
    blob_init(&input);
    blob_init(&output);
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-p12") == 0) {
            /* Encryption is defined only for the P1/P2 format. */
        } else if (strcmp(argv[i], "-p345") == 0) {
            print_error("P3/P4/P5 components do not use the P1/P2 encryption layer");
            goto cleanup;
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--header") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &header_path) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--encrypt") == 0 || strcmp(argv[i], "-en") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &input_path) != 0) goto cleanup;
            if (forced_mode == 0) {
                print_error("decrypt cannot be combined with --encrypt");
                goto cleanup;
            }
            encrypt = 1;
        } else if (strcmp(argv[i], "--decrypt") == 0 || strcmp(argv[i], "-de") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &input_path) != 0) goto cleanup;
            if (forced_mode == 1) {
                print_error("encrypt cannot be combined with --decrypt");
                goto cleanup;
            }
            encrypt = 0;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--input") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &input_path) != 0) goto cleanup;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &output_path) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage();
            result = 0;
            goto cleanup;
        } else if (argv[i][0] != '-') {
            if (input_path != NULL) {
                print_error("input file is already specified");
                goto cleanup;
            }
            input_path = argv[i];
        } else {
            print_error("unknown crypt option '%s'", argv[i]);
            goto cleanup;
        }
    }
    if (header_path == NULL || input_path == NULL || output_path == NULL || encrypt < 0) {
        print_error("crypt requires -h/--header, one operation, and -o/--output");
        goto cleanup;
    }
    if (read_header_file(header_path, &header) != 0 || read_file(input_path, &input) != 0) goto cleanup;
    if (encrypt ? encrypt_p12((const FW_HEADER *)header.data, &input, &output)
                : decrypt_p12((const FW_HEADER *)header.data, &input, &output)) goto cleanup;
    if (write_file(output_path, output.data, output.size) != 0) goto cleanup;
    printf("%s P1/P2 stream: %s -> %s (0x%lX bytes)\n",
           encrypt ? "Encrypted" : "Decrypted", input_path, output_path, (unsigned long)output.size);
    result = 0;

cleanup:
    blob_free(&header);
    blob_free(&input);
    blob_free(&output);
    return result;
}

static int command_p12(int argc, char **argv, int forced_mode)
{
    const char *header_path = NULL;
    const char *input_path = NULL;
    const char *output_path = NULL;
    int encode = forced_mode;
    Blob header;
    Blob input;
    Blob middle;
    Blob output;
    int i;
    int result = 1;

    blob_init(&header);
    blob_init(&input);
    blob_init(&middle);
    blob_init(&output);
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-p12") == 0) {
            /* The command itself is already P1/P2-specific. */
        } else if (strcmp(argv[i], "-p345") == 0) {
            print_error("p12 chain operations cannot be used with -p345");
            goto cleanup;
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--header") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &header_path) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--encode") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &input_path) != 0) goto cleanup;
            if (forced_mode == 0) {
                print_error("decode cannot be combined with --encode");
                goto cleanup;
            }
            encode = 1;
        } else if (strcmp(argv[i], "--decode") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &input_path) != 0) goto cleanup;
            if (forced_mode == 1) {
                print_error("encode cannot be combined with --decode");
                goto cleanup;
            }
            encode = 0;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--input") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &input_path) != 0) goto cleanup;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &output_path) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage();
            result = 0;
            goto cleanup;
        } else if (argv[i][0] != '-') {
            if (input_path != NULL) {
                print_error("input file is already specified");
                goto cleanup;
            }
            input_path = argv[i];
        } else {
            print_error("unknown p12 option '%s'", argv[i]);
            goto cleanup;
        }
    }
    if (header_path == NULL || input_path == NULL || output_path == NULL || encode < 0) {
        print_error("p12 requires -h/--header, --encode/--decode, and -o/--output");
        goto cleanup;
    }
    if (read_header_file(header_path, &header) != 0 || read_file(input_path, &input) != 0) goto cleanup;
    if (encode) {
        if (compress_p12(&input, &middle) != 0 ||
            encrypt_p12((const FW_HEADER *)header.data, &middle, &output) != 0) goto cleanup;
    } else {
        if (decrypt_p12((const FW_HEADER *)header.data, &input, &middle) != 0 ||
            decode_p12(middle.data, middle.size, &output) != 0) goto cleanup;
    }
    if (write_file(output_path, output.data, output.size) != 0) goto cleanup;
    printf("P1/P2 %s complete: %s -> %s (0x%lX bytes)\n",
           encode ? "compress+encrypt" : "decrypt+decompress",
           input_path, output_path, (unsigned long)output.size);
    result = 0;

cleanup:
    blob_free(&header);
    blob_free(&input);
    blob_free(&middle);
    blob_free(&output);
    return result;
}

static int command_header(int argc, char **argv)
{
    const char *input_path = NULL;
    const char *output_path = NULL;
    HeaderEdits edits;
    Blob header;
    int has_edits = 0;
    int i;
    int result = 1;

    memset(&edits, 0, sizeof(edits));
    blob_init(&header);
    for (i = 2; i < argc; i++) {
        int parsed_edit;
        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--input") == 0 ||
            strcmp(argv[i], "--header") == 0 || strcmp(argv[i], "--firmware") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &input_path) != 0) goto cleanup;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &output_path) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--show") == 0) {
            /* Showing is the default behaviour. */
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage();
            result = 0;
            goto cleanup;
        } else {
            parsed_edit = parse_header_edit_option(argc, argv, &i, &edits);
            if (parsed_edit < 0) goto cleanup;
            if (parsed_edit == 0) {
                print_error("unknown header option '%s'", argv[i]);
                goto cleanup;
            }
            has_edits = 1;
        }
    }
    if (input_path == NULL) {
        print_error("header requires -f/--input");
        goto cleanup;
    }
    if (has_edits && output_path == NULL) {
        print_error("header edits require -o/--output; the input is never overwritten implicitly");
        goto cleanup;
    }
    if (read_header_file(input_path, &header) != 0 || apply_header_edits(&header, &edits) != 0) goto cleanup;
    print_header_fields((const FW_HEADER *)header.data, "Header:");
    if (output_path != NULL) {
        if (write_file(output_path, header.data, HEADER_BYTES) != 0) goto cleanup;
        printf("Header written: %s\n", output_path);
    }
    result = 0;

cleanup:
    blob_free(&header);
    return result;
}

typedef struct {
    const char *directory;
    const char *output;
    const char *header;
    const char *parts[5];
    const char *flash_header;
    const char *flash_parts[2];
    const char *base;
    const char *size;
    const char *flashme_offset;
    int no_base;
    HeaderEdits edits;
} PackArguments;

static int resolve_input_path(char *path, size_t path_size, const char *directory,
                              const char *override_path, const char *default_name,
                              const char **result)
{
    if (override_path != NULL) {
        *result = override_path;
        return 0;
    }
    if (directory == NULL) {
        print_error("no input path was provided for '%s'", default_name);
        return -1;
    }
    if (join_path(path, path_size, directory, default_name) != 0) return -1;
    *result = path;
    return 0;
}

static int command_pack(int argc, char **argv)
{
    PackArguments arguments;
    Blob header;
    Blob parts[5];
    Blob base;
    Blob flash_header;
    Blob flash_parts[2];
    Blob output;
    LayoutMetadata metadata;
    const char *header_path;
    const char *part_paths[5];
    const char *flash_header_path;
    const char *flash_part_paths[2];
    const char *base_path = NULL;
    char header_path_buffer[PATH_BUFFER_SIZE];
    char part_path_buffers[5][PATH_BUFFER_SIZE];
    char flash_header_path_buffer[PATH_BUFFER_SIZE];
    char flash_part_path_buffers[2][PATH_BUFFER_SIZE];
    char base_path_buffer[PATH_BUFFER_SIZE];
    char metadata_path[PATH_BUFFER_SIZE];
    FW_HEADER *primary_header;
    FW_HEADER *secondary_header = NULL;
    u32 original_offsets[5];
    u32 new_offsets[5];
    u32 flash_original_offsets[2];
    u32 flash_new_offsets[2];
    u32 primary_end;
    u32 flash_end = HEADER_BYTES;
    u32 required_end;
    u32 capacity = 0;
    u32 requested_size = 0;
    u32 flashme_header_offset = 0;
    u32 parsed_flashme_offset = 0;
    u16 part12_crc;
    u16 part34_crc;
    u16 part5_crc;
    u16 flash_part12_crc;
    int has_flashme = 0;
    int has_explicit_flashme_offset = 0;
    int has_explicit_size = 0;
    int force_auto_size = 0;
    int i;
    int result = 1;

    memset(&arguments, 0, sizeof(arguments));
    memset(&metadata, 0, sizeof(metadata));
    blob_init(&header);
    blob_init(&base);
    blob_init(&flash_header);
    blob_init(&output);
    for (i = 0; i < 5; i++) blob_init(&parts[i]);
    for (i = 0; i < 2; i++) blob_init(&flash_parts[i]);

    for (i = 2; i < argc; i++) {
        int parsed_edit;
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--directory") == 0 ||
            strcmp(argv[i], "--input-directory") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.directory) != 0) goto cleanup;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.output) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--header") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.header) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--p1") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.parts[0]) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--p2") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.parts[1]) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--p3") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.parts[2]) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--p4") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.parts[3]) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--p5") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.parts[4]) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--flash-header") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.flash_header) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--flash-p1") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.flash_parts[0]) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--flash-p2") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.flash_parts[1]) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--base") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.base) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--no-base") == 0) {
            arguments.no_base = 1;
        } else if (strcmp(argv[i], "--size") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.size) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--flashme-offset") == 0) {
            if (require_option_value(argc, argv, &i, argv[i], &arguments.flashme_offset) != 0) goto cleanup;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage();
            result = 0;
            goto cleanup;
        } else {
            parsed_edit = parse_header_edit_option(argc, argv, &i, &arguments.edits);
            if (parsed_edit < 0) goto cleanup;
            if (parsed_edit == 0) {
                print_error("unknown pack option '%s'", argv[i]);
                goto cleanup;
            }
        }
    }
    if (arguments.output == NULL) {
        print_error("pack requires -o/--output");
        goto cleanup;
    }
    if (arguments.directory == NULL &&
        (arguments.header == NULL || arguments.parts[0] == NULL || arguments.parts[1] == NULL ||
         arguments.parts[2] == NULL || arguments.parts[3] == NULL || arguments.parts[4] == NULL)) {
        print_error("pack without -d/--directory requires --header and --p1 through --p5");
        goto cleanup;
    }
    if (arguments.size != NULL) {
        if (parse_size(arguments.size, &requested_size, &force_auto_size) != 0) goto cleanup;
        has_explicit_size = !force_auto_size;
    }
    if (arguments.flashme_offset != NULL) {
        if (parse_u32(arguments.flashme_offset, &parsed_flashme_offset) != 0) goto cleanup;
        has_explicit_flashme_offset = 1;
    }
    if (has_explicit_size && (requested_size % CAPACITY_UNIT) != 0) {
        print_error("--size must be a multiple of 256 KiB (0x%X bytes)", CAPACITY_UNIT);
        goto cleanup;
    }

    if (resolve_input_path(header_path_buffer, sizeof(header_path_buffer), arguments.directory,
                           arguments.header, "header.bin", &header_path) != 0 ||
        read_header_file(header_path, &header) != 0 ||
        apply_header_edits(&header, &arguments.edits) != 0) goto cleanup;
    primary_header = (FW_HEADER *)header.data;

    for (i = 0; i < 5; i++) {
        if (resolve_input_path(part_path_buffers[i], sizeof(part_path_buffers[i]), arguments.directory,
                               arguments.parts[i], primary_part_names[i], &part_paths[i]) != 0 ||
            read_aligned_component(part_paths[i], primary_part_alignment(primary_header, i), i < 2,
                                   &parts[i]) != 0) goto cleanup;
        original_offsets[i] = primary_part_offset(primary_header, i);
    }
    if (build_relocated_positions(original_offsets, parts, 5, new_offsets) != 0 ||
        calculate_primary_crcs(primary_header, parts, &part12_crc, &part34_crc, &part5_crc) != 0 ||
        set_primary_component_offsets(primary_header, new_offsets) != 0) goto cleanup;
    primary_header->part12_crc16 = part12_crc;
    primary_header->part34_crc16 = part34_crc;
    primary_header->part5_crc16 = part5_crc;
    if (max_component_end(new_offsets, parts, 5, &primary_end) != 0) goto cleanup;

    if (arguments.flash_header != NULL) {
        flash_header_path = arguments.flash_header;
        has_flashme = 1;
    } else if (arguments.directory != NULL) {
        if (join_path(flash_header_path_buffer, sizeof(flash_header_path_buffer),
                      arguments.directory, "header_flashme.bin") != 0) goto cleanup;
        flash_header_path = flash_header_path_buffer;
        has_flashme = file_exists(flash_header_path);
    } else {
        flash_header_path = NULL;
        has_flashme = 0;
    }
    if (!has_flashme && (arguments.flash_parts[0] != NULL || arguments.flash_parts[1] != NULL)) {
        print_error("--flash-p1/--flash-p2 require a FlashMe header");
        goto cleanup;
    }
    if (has_flashme) {
        if (read_header_file(flash_header_path, &flash_header) != 0) goto cleanup;
        secondary_header = (FW_HEADER *)flash_header.data;
        for (i = 0; i < 2; i++) {
            if (resolve_input_path(flash_part_path_buffers[i], sizeof(flash_part_path_buffers[i]),
                                   arguments.directory, arguments.flash_parts[i],
                                   flashme_part_names[i], &flash_part_paths[i]) != 0 ||
                read_aligned_component(flash_part_paths[i], flashme_part_alignment(secondary_header, i),
                                       0, &flash_parts[i]) != 0) goto cleanup;
            flash_original_offsets[i] = flashme_part_offset(secondary_header, i);
        }
        if (build_relocated_positions(flash_original_offsets, flash_parts, 2, flash_new_offsets) != 0 ||
            calculate_flashme_crc(flash_parts, &flash_part12_crc) != 0 ||
            set_flashme_component_offsets(secondary_header, flash_new_offsets) != 0) goto cleanup;
        secondary_header->part12_crc16 = flash_part12_crc;
        if (max_component_end(flash_new_offsets, flash_parts, 2, &flash_end) != 0) goto cleanup;
    }

    if (arguments.directory != NULL) {
        if (join_path(metadata_path, sizeof(metadata_path), arguments.directory, "fwtool.meta") != 0) goto cleanup;
        read_layout_metadata(metadata_path, &metadata);
    }

    if (!arguments.no_base) {
        if (arguments.base != NULL) {
            base_path = arguments.base;
            if (!file_exists(base_path)) {
                print_error("specified base image '%s' does not exist", base_path);
                goto cleanup;
            }
        } else if (arguments.directory != NULL) {
            if (join_path(base_path_buffer, sizeof(base_path_buffer), arguments.directory,
                          "firmware-base.bin") != 0) goto cleanup;
            if (file_exists(base_path_buffer)) base_path = base_path_buffer;
        }
        if (base_path != NULL && read_file(base_path, &base) != 0) goto cleanup;
    }

    required_end = primary_end > flash_end ? primary_end : flash_end;
    if (has_flashme && has_explicit_flashme_offset) {
        if (parsed_flashme_offset > UINT_MAX - HEADER_BYTES) {
            print_error("--flashme-offset is out of range");
            goto cleanup;
        }
        if (parsed_flashme_offset + HEADER_BYTES > required_end) {
            required_end = parsed_flashme_offset + HEADER_BYTES;
        }
    } else if (has_flashme && !has_explicit_size && !force_auto_size &&
               metadata.has_flashme_header_offset) {
        flashme_header_offset = metadata.flashme_header_offset;
        if (flashme_header_offset > UINT_MAX - HEADER_BYTES) {
            print_error("fwtool.meta contains an invalid FlashMe header offset");
            goto cleanup;
        }
        if (flashme_header_offset + HEADER_BYTES > required_end) {
            required_end = flashme_header_offset + HEADER_BYTES;
        }
    }

    if (has_explicit_size) {
        capacity = requested_size;
    } else if (!force_auto_size && metadata.has_image_size) {
        capacity = metadata.image_size;
    } else {
        u32 minimum = required_end;
        if (has_flashme && flashme_header_offset == 0 && !has_explicit_flashme_offset) {
            if (minimum > UINT_MAX - FLASHME_TRAILER) {
                print_error("FlashMe layout exceeds the firmware address space");
                goto cleanup;
            }
            minimum += FLASHME_TRAILER;
        }
        if (round_up_u32(minimum, CAPACITY_UNIT, &capacity) != 0) {
            print_error("cannot choose an automatic firmware capacity");
            goto cleanup;
        }
    }
    if (capacity == 0 || (capacity % CAPACITY_UNIT) != 0) {
        print_error("firmware capacity must be a non-zero multiple of 256 KiB");
        goto cleanup;
    }
    if (has_flashme) {
        if (has_explicit_flashme_offset) flashme_header_offset = parsed_flashme_offset;
        else if (flashme_header_offset == 0) {
            if (capacity < FLASHME_TRAILER) {
                print_error("firmware capacity is too small for a FlashMe header");
                goto cleanup;
            }
            flashme_header_offset = capacity - FLASHME_TRAILER;
        }
        if (flashme_header_offset > capacity || HEADER_BYTES > capacity - flashme_header_offset) {
            print_error("FlashMe header does not fit in the requested firmware capacity");
            goto cleanup;
        }
        if (flashme_header_offset < primary_end || flashme_header_offset < flash_end) {
            print_error("components overlap the FlashMe header; increase --size or choose --flashme-offset");
            goto cleanup;
        }
    }
    if (required_end > capacity || primary_end > capacity || flash_end > capacity) {
        print_error("components do not fit in the requested firmware capacity");
        goto cleanup;
    }

    {
        FirmwareRange ranges[MAX_RANGES];
        int range_count = 0;
        if (add_firmware_range(ranges, &range_count, 0, HEADER_BYTES, capacity, "primary header") != 0) goto cleanup;
        for (i = 0; i < 5; i++) {
            if (add_firmware_range(ranges, &range_count, new_offsets[i], (u32)parts[i].size,
                                   capacity, primary_part_names[i]) != 0) goto cleanup;
        }
        if (has_flashme) {
            if (add_firmware_range(ranges, &range_count, flashme_header_offset, HEADER_BYTES,
                                   capacity, "FlashMe header") != 0) goto cleanup;
            for (i = 0; i < 2; i++) {
                if (add_firmware_range(ranges, &range_count, flash_new_offsets[i],
                                       (u32)flash_parts[i].size, capacity,
                                       flashme_part_names[i]) != 0) goto cleanup;
            }
        }
    }

    if (blob_alloc(&output, capacity) != 0) goto cleanup;
    memset(output.data, 0, output.size);
    if (base.data != NULL) {
        size_t copy_size = base.size < output.size ? base.size : output.size;
        memcpy(output.data, base.data, copy_size);
        if (has_flashme && metadata.has_flashme_header_offset &&
            metadata.flashme_header_offset != flashme_header_offset &&
            metadata.flashme_header_offset < output.size &&
            HEADER_BYTES <= output.size - metadata.flashme_header_offset) {
            memset(output.data + metadata.flashme_header_offset, 0, HEADER_BYTES);
        }
    }
    memcpy(output.data, header.data, HEADER_BYTES);
    for (i = 0; i < 5; i++) {
        memcpy(output.data + new_offsets[i], parts[i].data, parts[i].size);
    }
    if (has_flashme) {
        memcpy(output.data + flashme_header_offset, flash_header.data, HEADER_BYTES);
        for (i = 0; i < 2; i++) {
            memcpy(output.data + flash_new_offsets[i], flash_parts[i].data, flash_parts[i].size);
        }
    }
    if (write_file(arguments.output, output.data, output.size) != 0) goto cleanup;
    printf("Packed firmware: %s\n", arguments.output);
    printf("Capacity: 0x%08X (%u KiB)%s\n", capacity, capacity / 1024,
           base.data != NULL ? ", preserved base image used" : "");
    if (has_flashme) printf("FlashMe secondary header: 0x%06X\n", flashme_header_offset);
    result = 0;

cleanup:
    blob_free(&header);
    blob_free(&base);
    blob_free(&flash_header);
    blob_free(&output);
    for (i = 0; i < 5; i++) blob_free(&parts[i]);
    for (i = 0; i < 2; i++) blob_free(&flash_parts[i]);
    return result;
}

static int append_action_argument(const char **arguments, int *count, int capacity, const char *value)
{
    if (*count >= capacity) {
        print_error("too many command-line arguments");
        return -1;
    }
    arguments[(*count)++] = value;
    return 0;
}

static int append_action_option_with_value(const char **arguments, int *count, int capacity,
                                           const char *option, int argc, char **argv, int *index)
{
    if (*index + 1 >= argc) {
        print_error("%s requires a value", argv[*index]);
        return -1;
    }
    if (append_action_argument(arguments, count, capacity, option) != 0 ||
        append_action_argument(arguments, count, capacity, argv[++*index]) != 0) return -1;
    return 0;
}

static int command_ndstool_style(int argc, char **argv)
{
    enum { ACTION_INFO, ACTION_UNPACK, ACTION_PACK } action;
    const char *converted[128];
    char *mutable_arguments[128];
    const char *firmware = NULL;
    int count = 0;
    int i;
    int mutable_count;

    if (strcmp(argv[1], "-i") == 0) action = ACTION_INFO;
    else if (strcmp(argv[1], "-x") == 0) action = ACTION_UNPACK;
    else action = ACTION_PACK;

    for (i = 2; i < argc; i++) {
        const char *option = argv[i];
        const char *mapped = NULL;
        if (option[0] != '-') {
            if (firmware != NULL) {
                print_error("firmware filename is already specified");
                return 1;
            }
            firmware = option;
            continue;
        }
        if (strcmp(option, "-?") == 0 || strcmp(option, "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(option, "-d") == 0) {
            if (action == ACTION_INFO) {
                print_error("-d is not valid with -i");
                return 1;
            }
            if (append_action_option_with_value(converted, &count, 128, "-d", argc, argv, &i) != 0) return 1;
            continue;
        }
        if (strcmp(option, "-h") == 0) mapped = action == ACTION_UNPACK ? "--header-out" : "--header";
        else if (strcmp(option, "-9") == 0) mapped = action == ACTION_UNPACK ? "--p1-out" : "--p1";
        else if (strcmp(option, "-7") == 0) mapped = action == ACTION_UNPACK ? "--p2-out" : "--p2";
        else if (strcmp(option, "-p3") == 0) mapped = action == ACTION_UNPACK ? "--p3-out" : "--p3";
        else if (strcmp(option, "-p4") == 0) mapped = action == ACTION_UNPACK ? "--p4-out" : "--p4";
        else if (strcmp(option, "-p5") == 0) mapped = action == ACTION_UNPACK ? "--p5-out" : "--p5";
        else if (strcmp(option, "-fh") == 0) mapped = action == ACTION_UNPACK ? "--flash-header-out" : "--flash-header";
        else if (strcmp(option, "-f9") == 0) mapped = action == ACTION_UNPACK ? "--flash-p1-out" : "--flash-p1";
        else if (strcmp(option, "-f7") == 0) mapped = action == ACTION_UNPACK ? "--flash-p2-out" : "--flash-p2";

        if (mapped != NULL) {
            if (action == ACTION_INFO) {
                print_error("%s is not valid with -i", option);
                return 1;
            }
            if (append_action_option_with_value(converted, &count, 128, mapped, argc, argv, &i) != 0) return 1;
            continue;
        }
        if (strcmp(option, "-s") == 0) {
            if (action != ACTION_PACK) {
                print_error("-s is only valid with -c");
                return 1;
            }
            if (append_action_option_with_value(converted, &count, 128, "--size", argc, argv, &i) != 0) return 1;
            continue;
        }
        if (strcmp(option, "-b") == 0) {
            if (action != ACTION_PACK) {
                print_error("-b is only valid with -c");
                return 1;
            }
            if (append_action_option_with_value(converted, &count, 128, "--base", argc, argv, &i) != 0) return 1;
            continue;
        }
        if (strcmp(option, "--no-base") == 0) {
            if (action == ACTION_INFO) {
                print_error("--no-base is not valid with -i");
                return 1;
            }
            if (append_action_argument(converted, &count, 128, option) != 0) return 1;
            continue;
        }
        if (strcmp(option, "--only") == 0) {
            if (action != ACTION_UNPACK) {
                print_error("--only is only valid with -x");
                return 1;
            }
            if (append_action_option_with_value(converted, &count, 128, option, argc, argv, &i) != 0) return 1;
            continue;
        }
        if (strcmp(option, "--flashme-offset") == 0 || strcmp(option, "--size") == 0 ||
            strcmp(option, "--base") == 0) {
            if (action != ACTION_PACK) {
                print_error("%s is only valid with -c", option);
                return 1;
            }
            if (append_action_option_with_value(converted, &count, 128, option, argc, argv, &i) != 0) return 1;
            continue;
        }
        if (strcmp(option, "--identifier") == 0 || strcmp(option, "--timestamp") == 0 ||
            strcmp(option, "--console-type") == 0 || strcmp(option, "--shift-amounts") == 0 ||
            strcmp(option, "--part1-romaddr") == 0 || strcmp(option, "--part1-ramaddr") == 0 ||
            strcmp(option, "--part2-romaddr") == 0 || strcmp(option, "--part2-ramaddr") == 0 ||
            strcmp(option, "--part3-romaddr") == 0 || strcmp(option, "--part4-romaddr") == 0 ||
            strcmp(option, "--part5-romaddr") == 0 || strcmp(option, "--settings-offset") == 0) {
            if (action != ACTION_PACK) {
                print_error("%s is only valid with -c", option);
                return 1;
            }
            if (append_action_option_with_value(converted, &count, 128, option, argc, argv, &i) != 0) return 1;
            continue;
        }
        if (strcmp(option, "--set-u8") == 0 || strcmp(option, "--set-u16") == 0 ||
            strcmp(option, "--set-u32") == 0) {
            if (action != ACTION_PACK || i + 2 >= argc) {
                print_error("%s is only valid with -c and requires OFFSET VALUE", option);
                return 1;
            }
            if (append_action_argument(converted, &count, 128, option) != 0 ||
                append_action_argument(converted, &count, 128, argv[++i]) != 0 ||
                append_action_argument(converted, &count, 128, argv[++i]) != 0) return 1;
            continue;
        }
        print_error("unknown ndstool-style option '%s'", option);
        return 1;
    }

    if (firmware == NULL) {
        print_error("%s requires a firmware filename", argv[1]);
        return 1;
    }
    {
        const char *command = action == ACTION_INFO ? "info" :
                              (action == ACTION_UNPACK ? "unpack" : "pack");
        int final_count = 0;
        mutable_arguments[final_count++] = argv[0];
        mutable_arguments[final_count++] = (char *)command;
        if (action == ACTION_INFO || action == ACTION_UNPACK) {
            mutable_arguments[final_count++] = (char *)"-f";
            mutable_arguments[final_count++] = (char *)firmware;
        } else {
            mutable_arguments[final_count++] = (char *)"-o";
            mutable_arguments[final_count++] = (char *)firmware;
        }
        for (i = 0; i < count; i++) mutable_arguments[final_count++] = (char *)converted[i];
        mutable_count = final_count;
    }
    if (action == ACTION_INFO) return command_info(mutable_count, mutable_arguments);
    if (action == ACTION_UNPACK) return command_unpack(mutable_count, mutable_arguments);
    return command_pack(mutable_count, mutable_arguments);
}

#endif

/* The public dsfwtool interface deliberately has no working-directory mode.
   Every artifact is named on the command line, in the same spirit as
   ndstool. */
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
        if (create && (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--size") == 0)) {
            if (take_option_value(argc, argv, &i, argv[i], &request->size_text) != 0) return -1;
            continue;
        }
        if (create && (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--base") == 0)) {
            if (take_option_value(argc, argv, &i, argv[i], &request->base_path) != 0) return -1;
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
    if (round_up_u32(layout->effective_sizes[index], 8, &raw_size) != 0 ||
        raw_size > layout->spans[index]) {
        print_error("cannot determine the encrypted size of %s", primary_part_names[index]);
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
    Blob primary_parts[5];
    Blob flash_parts[2];
    Blob base;
    Blob output;
    FW_HEADER *primary_header;
    FW_HEADER *secondary_header;
    u32 primary_original_offsets[5];
    u32 primary_alignments[5];
    u32 primary_reserved_sizes[5];
    u32 primary_new_offsets[5];
    u32 flash_original_offsets[2];
    u32 flash_alignments[2];
    u32 flash_reserved_sizes[2];
    u32 flash_new_offsets[2];
    u32 primary_end;
    u32 flash_end = HEADER_BYTES;
    u32 minimum_size;
    u32 logical_capacity;
    u32 output_size;
    u32 flash_header_offset = 0;
    u32 flash_trailer_size = 0;
    u32 requested_size = 0;
    int automatic_size = 1;
    int has_flashme;
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
    }
    has_flashme = request.components[FWCOMP_FLASH_HEADER].path != NULL;
    if (has_flashme) {
        if (read_header_file(request.components[FWCOMP_FLASH_HEADER].path, &flash_header) != 0 ||
            apply_header_edits(&flash_header, &request.edits) != 0) goto cleanup;
        secondary_header = (FW_HEADER *)flash_header.data;
        for (i = 0; i < 2; i++) {
            int component = FWCOMP_FLASH_P1 + i;
            if (prepare_component_for_create(component, &request.components[component], primary_header,
                                             &flash_parts[i]) != 0 ||
                component_span(&flash_parts[i], &flash_reserved_sizes[i]) != 0) goto cleanup;
            flash_original_offsets[i] = flashme_part_offset(secondary_header, i);
            flash_alignments[i] = flashme_part_alignment(secondary_header, i);
        }
        if (build_flashme_combined_positions(primary_original_offsets, primary_alignments,
                                             primary_reserved_sizes, flash_original_offsets,
                                             flash_alignments, flash_reserved_sizes,
                                             primary_new_offsets, flash_new_offsets) != 0) goto cleanup;
    } else if (build_relocated_positions_aligned(primary_original_offsets, primary_alignments,
                                                 primary_reserved_sizes, 5,
                                                 primary_new_offsets) != 0) {
        goto cleanup;
    }
    if (max_reserved_end(primary_new_offsets, primary_reserved_sizes, 5, &primary_end) != 0 ||
        (has_flashme && max_reserved_end(flash_new_offsets, flash_reserved_sizes, 2,
                                          &flash_end) != 0)) goto cleanup;

    if (request.base_path != NULL) {
        if (read_file(request.base_path, &base) != 0 || base.size == 0 || base.size > UINT_MAX) goto cleanup;
    }
    if (request.size_text != NULL) {
        if (parse_size(request.size_text, &requested_size, &automatic_size) != 0) goto cleanup;
    }
    minimum_size = primary_end > flash_end ? primary_end : flash_end;
    if (automatic_size) {
        if (base.data != NULL) {
            if (has_flashme) {
                /* Some FlashMe dumps omit the final 0x200-byte settings
                   sector.  Their physical file is 0x...FE00, while the
                   secondary header still belongs at the end of the logical
                   0x...0000 flash capacity.  Preserve that physical length
                   for an automatic base-image repack. */
                if (round_up_u32((u32)base.size, CAPACITY_UNIT, &logical_capacity) != 0) {
                    print_error("automatic FlashMe capacity is out of range");
                    goto cleanup;
                }
                output_size = (u32)base.size;
            } else {
                logical_capacity = (u32)base.size;
                output_size = logical_capacity;
            }
        } else {
            u32 logical_minimum = minimum_size;
            if (has_flashme) {
                if (logical_minimum > UINT_MAX - FLASHME_TRAILER) {
                    print_error("FlashMe layout is too large for a firmware image");
                    goto cleanup;
                }
                logical_minimum += FLASHME_TRAILER;
            }
            if (logical_minimum < CAPACITY_UNIT) logical_minimum = CAPACITY_UNIT;
            if (round_up_u32(logical_minimum, CAPACITY_UNIT, &logical_capacity) != 0) {
                print_error("automatic firmware capacity is out of range");
                goto cleanup;
            }
            output_size = logical_capacity;
        }
    } else {
        logical_capacity = requested_size;
        output_size = logical_capacity;
    }
    if (logical_capacity < CAPACITY_UNIT || logical_capacity % CAPACITY_UNIT != 0) {
        print_error("firmware capacity must be 256 KiB or another 256 KiB multiple");
        goto cleanup;
    }
    if (!has_flashme && output_size != logical_capacity) {
        print_error("a non-FlashMe firmware image must have a 256 KiB multiple size");
        goto cleanup;
    }
    if (base.data != NULL && base.size > output_size) {
        print_error("-b base firmware is larger than the selected output size");
        goto cleanup;
    }
    if (has_flashme) {
        flash_header_offset = logical_capacity - FLASHME_TRAILER;
        if (flash_end > flash_header_offset) {
            print_error("FlashMe components overlap the secondary-header trailer area");
            goto cleanup;
        }
        if (flash_header_offset > output_size || HEADER_BYTES > output_size - flash_header_offset) {
            print_error("the output is too short to contain the FlashMe secondary header");
            goto cleanup;
        }
        flash_trailer_size = output_size - flash_header_offset;
        if (flash_trailer_size > FLASHME_TRAILER) flash_trailer_size = FLASHME_TRAILER;
    }
    if (primary_end > output_size || flash_end > output_size) {
        print_error("the selected output size is too small for these components");
        goto cleanup;
    }

    if (add_firmware_range(ranges, &range_count, 0, HEADER_BYTES, output_size, "primary header") != 0) goto cleanup;
    for (i = 0; i < 5; i++) {
        if (add_firmware_range(ranges, &range_count, primary_new_offsets[i], primary_reserved_sizes[i],
                               output_size, primary_part_labels[i]) != 0) goto cleanup;
    }
    if (has_flashme) {
        if (add_firmware_range(ranges, &range_count, flash_header_offset, flash_trailer_size, output_size,
                               "FlashMe secondary-header trailer") != 0) goto cleanup;
        for (i = 0; i < 2; i++) {
            if (add_firmware_range(ranges, &range_count, flash_new_offsets[i], flash_reserved_sizes[i],
                                   output_size, flashme_part_names[i]) != 0) goto cleanup;
        }
    }

    if (set_primary_component_offsets(primary_header, primary_new_offsets) != 0 ||
        update_header_config_checksum(&header) != 0) goto cleanup;
    if (!has_flashme) {
        if (calculate_primary_crcs(primary_header, primary_parts, &primary_header->part12_crc16,
                                   &primary_header->part34_crc16, &primary_header->part5_crc16) != 0) goto cleanup;
    } else {
        /* FlashMe patches the boot code but deliberately retains the retail
           version CRC16 values in both headers.  They are used by FlashMe's
           version/update logic, so preserving the supplied header fields is
           the lossless default; changing them to CRCs of patched code makes
           an otherwise byte-identical repack differ at header offsets 0x04
           and 0x06. */
        secondary_header = (FW_HEADER *)flash_header.data;
        if (set_flashme_component_offsets(secondary_header, flash_new_offsets) != 0 ||
            update_header_config_checksum(&flash_header) != 0) goto cleanup;
    }

    if (blob_alloc(&output, output_size) != 0) goto cleanup;
    memset(output.data, request.fill_byte, output.size);
    if (base.data != NULL) memcpy(output.data, base.data, base.size);
    memcpy(output.data, header.data, HEADER_BYTES);
    for (i = 0; i < 5; i++) {
        memcpy(output.data + primary_new_offsets[i], primary_parts[i].data, primary_parts[i].size);
    }
    if (has_flashme) {
        memcpy(output.data + flash_header_offset, flash_header.data, HEADER_BYTES);
        for (i = 0; i < 2; i++) {
            memcpy(output.data + flash_new_offsets[i], flash_parts[i].data, flash_parts[i].size);
        }
    }
    if (write_file(output_path, output.data, output.size) != 0) goto cleanup;

    printf("Created %s (0x%08X bytes, fill=%02X%s)\n", output_path, output_size, request.fill_byte,
           base.data != NULL ? ", base preserved" : "");
    if (has_flashme && output_size != logical_capacity) {
        printf("  FlashMe logical capacity=0x%08X; retained truncated physical image length\n",
               logical_capacity);
    }
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
