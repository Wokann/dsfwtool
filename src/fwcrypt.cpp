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
#include "firmware.h"
#include "encryption.h"

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

#define HEADER_SIZE 0x200

typedef enum {
    MODE_ENCRYPT = 0,
    MODE_DECRYPT = 1
} CRYPT_MODE;

void print_usage(void)
{
    printf("fwcrypt - Nintendo DS Firmware Encryptor / Decryptor\n");
    printf("----------------------------------------------------\n");
    printf("Usage:\n");
    printf("  fwcrypt -f [firmware_header.bin] -en [input_file] -o [output_file]\n");
    printf("  fwcrypt -f [firmware_header.bin] -de [input_file] -o [output_file]\n");
}

int main(int argc, char* argv[]) 
{
    printf("fwcrypt - Nintendo DS Firmware Encryptor / Decryptor\n\n");

    const char* header_path = NULL;
    const char* input_path = NULL;
    const char* output_path = NULL;
    CRYPT_MODE mode = MODE_DECRYPT; // 默认解密

    // 参数解析
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            printf("Unknown parameter: %s\n", argv[i]);
            return -1;
        }
        switch (argv[i][1]) {
            case 'f': // -f 固件头
                if (i + 1 >= argc) { printf("Header file missing for %s\n", argv[i]); return -1; }
                header_path = argv[i + 1];
                i++;
                break;
            case 'o': // -o 输出文件
                if (i + 1 >= argc) { printf("Output file missing for %s\n", argv[i]); return -1; }
                output_path = argv[i + 1];
                i++;
                break;
            case 'e': // -en
                if (argv[i][2] != 'n' || argv[i][3] != '\0') { printf("Invalid parameter: %s\n", argv[i]); return -1; }
                if (i + 1 >= argc) { printf("Input file missing for %s\n", argv[i]); return -1; }
                input_path = argv[i + 1];
                mode = MODE_ENCRYPT;
                i++;
                break;
            case 'd': // -de
                if (argv[i][2] != 'e' || argv[i][3] != '\0') { printf("Invalid parameter: %s\n", argv[i]); return -1; }
                if (i + 1 >= argc) { printf("Input file missing for %s\n", argv[i]); return -1; }
                input_path = argv[i + 1];
                mode = MODE_DECRYPT;
                i++;
                break;
            default:
                printf("Unknown parameter: %s\n", argv[i]);
                return -1;
        }
    }

    if (!header_path || !input_path || !output_path) {
        printf("Error: Missing required parameter.\n");
        print_usage();
        return -1;
    }

    // 读取 header
    FILE* header_bin = fopen(header_path, "rb");
    if (!header_bin) { printf("Error: Failed to open header file: %s\n", header_path); return -1; }
    u8* header_data = (u8*)malloc(HEADER_SIZE);
    if (!header_data) { printf("Error: Failed to allocate memory for header.\n"); fclose(header_bin); return -1; }
    if (fread(header_data, 1, HEADER_SIZE, header_bin) < HEADER_SIZE) {
        printf("Error: Header file too small\n");
        fclose(header_bin); free(header_data); return -1;
    }
    fclose(header_bin);

    // 读取输入文件
    FILE* input_bin = fopen(input_path, "rb");
    if (!input_bin) { printf("Error: Failed to open input file: %s\n", input_path); free(header_data); return -1; }
    fseek(input_bin, 0, SEEK_END);
    size_t input_size = ftell(input_bin);
    fseek(input_bin, 0, SEEK_SET);
    u8* input_data = (u8*)malloc(input_size);
    if (!input_data) { printf("Error: Failed to allocate input buffer.\n"); fclose(input_bin); free(header_data); return -1; }
    fread(input_data, 1, input_size, input_bin);
    fclose(input_bin);
    printf("Input file size: 0x%08X\n", (unsigned int)input_size);

    // 初始化密钥
    u32 idcode = ((u32*)header_data)[2];
    init_keycode(idcode, 2, 0x0C);
    u8 idcode_bytes[4] = { idcode & 0xFF, (idcode >> 8) & 0xFF, (idcode >> 16) & 0xFF, (idcode >> 24) & 0xFF };
    printf("IDcode for crypt key: %.4s (%02X %02X %02X %02X)\n", idcode_bytes, idcode_bytes[0], idcode_bytes[1], idcode_bytes[2], idcode_bytes[3]);

    u8* output_data = (u8*)malloc(input_size);
    if (!output_data) { printf("Error: Failed to allocate output buffer.\n"); free(input_data); free(header_data); return -1; }

    int output_size = 0;
    if (mode == MODE_DECRYPT) {
        output_size = decrypt_buffer(input_data, output_data, input_size);
        printf("Decrypted size: 0x%08X\n", output_size);
    } else {
        output_size = encrypt_buffer(input_data, output_data, input_size);
        printf("Encrypted size: 0x%08X\n", output_size);
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
			free(output_data); free(input_data); free(header_data);
			return -1;
		}
	}


    // 写输出文件
    FILE* output_bin = fopen(output_path, "wb");
    if (!output_bin) { printf("Error: Failed to open output file: %s\n", output_path); free(output_data); free(input_data); free(header_data); return -1; }
    fwrite(output_data, 1, output_size, output_bin);
    fclose(output_bin);
    printf("Output written: %s\n", output_path);

    free(output_data);
    free(input_data);
    free(header_data);
    printf("Done.\n");

    return 0;
}
