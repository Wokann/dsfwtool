#define _CRT_SECURE_NO_DEPRECATE 1
#define _CRT_NONSTDC_NO_DEPRECATE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir_dir(path) _mkdir(path)
#else
#define mkdir_dir(path) mkdir(path, 0755)
#endif

#include "nds_types.h"
#include "part12_comp.h"
#include "part345_comp.h"

int mkdir_recursive(const char *path) {
    char tmp[1024];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if(len == 0) return -1;
    if(tmp[len - 1] == '/' || tmp[len - 1] == '\\')
        tmp[len - 1] = '\0';

    for(p = tmp + 1; *p; p++) {
        if(*p == '/' || *p == '\\') {
            *p = '\0';
            struct stat st = {0};
            if(stat(tmp, &st) == -1) {
                if(mkdir_dir(tmp) != 0) return -1;
            }
            *p = '/';
        }
    }
    struct stat st = {0};
    if(stat(tmp, &st) == -1) {
        if(mkdir_dir(tmp) != 0) return -1;
    }
    return 0;
}

typedef enum {
    PART_12 = 0,
    PART_345 = 1
} FW_PART_TYPE;

typedef enum {
    MODE_COMPRESS = 0,
    MODE_DECOMPRESS = 1
} COMP_MODE;

void print_usage(void)
{
    printf("fwcomp - Nintendo DS Firmware Compressor / Decompressor\n");
    printf("----------------------------------------------------\n");
    printf("Usage:\n");
    printf("  fwcomp -t [12|345] -co [input_file] -o [output_file]\n");
    printf("  fwcomp -t [12|345] -de [input_file] -o [output_file]\n");
    printf("\nExamples:\n");
    printf("  fwcomp -t 12 -co arm9boot.bin -o arm9boot_comp.bin\n");
    printf("  fwcomp -t 345 -de arm7wifi.bin -o arm7boot_decomp.bin\n");
}

int main(int argc, char* argv[])
{
    printf("fwcomp - Nintendo DS Firmware Compressor / Decompressor\n\n");

    if (argc < 7) {
        print_usage();
        return -1;
    }

    FW_PART_TYPE type = PART_12;
    COMP_MODE mode = MODE_COMPRESS;
    const char* input_path = NULL;
    const char* output_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            printf("Unknown parameter: %s\n", argv[i]);
            return -1;
        }

        switch (argv[i][1]) {
            case 't': //-t
                if (argv[i][2] != '\0') { printf("Invalid parameter: %s\n", argv[i]); return -1;                 }
                if (i + 1 >= argc) { printf("Parameter value missing for %s\n", argv[i]); return -1;                 }
                if (strcmp(argv[i + 1], "12") == 0) 
                    type = PART_12;
                else if (strcmp(argv[i + 1], "345") == 0) 
                    type = PART_345;
                else { printf("Unknown type: %s\n", argv[i + 1]); return -1;                 }
                i++;
                break;

            case 'c': //-co
                if (argv[i][2] == 'o' && argv[i][3] == '\0') {
                    mode = MODE_COMPRESS;
                    if (i + 1 >= argc) { printf("Input file missing for %s\n", argv[i]); return -1; }
                    input_path = argv[i + 1];
                    i++;
                } else { printf("Invalid parameter: %s\n", argv[i]); return -1; }
                break;

            case 'd': //-de
                if (argv[i][2] == 'e' && argv[i][3] == '\0') {
                    mode = MODE_DECOMPRESS;
                    if (i + 1 >= argc) { printf("Input file missing for %s\n", argv[i]); return -1; }
                    input_path = argv[i + 1];
                    i++;
                } else { printf("Invalid parameter: %s\n", argv[i]); return -1; }
                break;

            case 'o': //-o
                if (argv[i][2] != '\0') { printf("Invalid parameter: %s\n", argv[i]); return -1; }
                if (i + 1 >= argc) { printf("Output file missing for %s\n", argv[i]); return -1; }
                output_path = argv[i + 1];
                i++;
                break;

            default:
                printf("Unknown parameter: %s\n", argv[i]);
                return -1;
        }
    }

    if (!input_path || !output_path) {
        printf("Error: Input or output file missing.\n");
        print_usage();
        return -1;
    }

    FILE* input_bin = fopen(input_path, "rb");
    if (!input_bin) {
        printf("Error: Failed to open input file: %s\n", input_path);
        return -1;
    }

    fseek(input_bin, 0, SEEK_END);
    size_t input_size = ftell(input_bin);
    fseek(input_bin, 0, SEEK_SET);

    u8* input_data = (u8*)malloc(input_size);
    if (!input_data) {
        printf("Error: Failed to allocate memory for input file.\n");
        fclose(input_bin);
        return -1;
    }

    fread(input_data, 1, input_size, input_bin);
    fclose(input_bin);

    printf("Input file size: 0x%08X\n", (unsigned int)input_size);

    size_t out_buf_size = 1024 * 1024;
    u8* output_data = (u8*)malloc(out_buf_size);
    if (!output_data) {
        printf("Error: Failed to allocate output buffer.\n");
        free(input_data);
        return -1;
    }

    u32 output_size = 0;
    u32 effective_size = 0;
    switch (type) {
        case PART_12:
            switch (mode) {
                case MODE_DECOMPRESS:
                    output_size = decompressLZ77(output_data, input_data);
                    effective_size = getCompressedLZ77Size(input_data);
                    printf("Operation : Decompress PART_12\n");
                    printf("Effect compressed size: 0x%08X\n", effective_size);
                    printf("Decompressed size: 0x%08X\n", output_size);
                    break;
                case MODE_COMPRESS:
                    output_size = compressLZ77(output_data, input_data, (u32)input_size);
                    printf("Operation : Compress PART_12\n");
                    printf("Compressed size: 0x%08X\n", output_size);
                    break;
            }
            break;

        case PART_345:
            switch (mode) {
                case MODE_DECOMPRESS:
                    output_size = decompress_part345(output_data, input_data);
                    effective_size = getCompressedPart345Size(input_data);
                    printf("Operation : Decompress PART_345\n");
                    printf("Effect compressed size: 0x%08X\n", effective_size);
                    printf("Decompressed size: 0x%08X\n", output_size);
                    break;
                case MODE_COMPRESS:
                    output_size = compress_part345(output_data, input_data, (u32)input_size);
                    printf("Operation : Compress PART_345\n");
                    printf("Compressed size: 0x%08X\n", output_size);
                    break;
            }
            break;

        default:
            printf("Error: Unknown part type.\n");
            free(output_data);
            free(input_data);
            return -1;
    }
    // output folder check
    char folder_path[1024];
    strcpy(folder_path, output_path);
    char* last_slash = strrchr(folder_path, '/');
    if (!last_slash) last_slash = strrchr(folder_path, '\\');
    if (last_slash) {
        *last_slash = '\0'; // 去掉文件名
        if(mkdir_recursive(folder_path) != 0) {
            printf("Failed to create output folder: %s\n", folder_path);
            free(output_data); free(input_data);
            return -1;
        }
    }

    // output compress or decompress file
    FILE* output_bin = fopen(output_path, "wb");
    if (!output_bin) {
        printf("Error: Failed to open output file: %s\n", output_path);
        free(output_data);
        free(input_data);
        return -1;
    }

    fwrite(output_data, 1, output_size, output_bin);
    fclose(output_bin);

    printf("Output written: %s\n", output_path);
    printf("Done.\n");

    free(output_data);
    free(input_data);

    return 0;
}
