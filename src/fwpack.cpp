#define _CRT_SECURE_NO_DEPRECATE 1 
#define _CRT_NONSTDC_NO_DEPRECATE 1

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
#define mkdir_dir(path) mkdir(path, 0755)
#endif

#include "nds_types.h"
#include "crc.h"
#include "firmware.h"
#include "encryption.h"
#include "part345_comp.h"
#include "part12_comp.h"

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

const char* part_filenames[5] = {
    "arm9_boot_code.bin",
    "arm7_boot_code.bin",
    "arm9_gui_code.bin",
    "arm7_wifi_code.bin",
    "data_gfx.bin"
};

const char* console_types[256] = {0};
void init_console_types() {
    console_types[0xFF] = "NTR/Worldwide (phat)";
    console_types[0x20] = "USG/Worldwide (lite)";
    console_types[0x35] = "USG/Korea (lite)";
    console_types[0x43] = "NTR/China (iQue) (phat)";
    console_types[0x63] = "USG/China (iQue) (lite)";
    console_types[0x01] = "NIS/Worldwide (Nintendo Zone Box) (Debug)";
}

void print_fw_header(FW_HEADER* hdr) {
    int shift1 = hdr->shift_amounts & 7;
    int shift2 = (hdr->shift_amounts >> 3) & 7;
    int shift3 = (hdr->shift_amounts >> 6) & 7;
    int shift4 = (hdr->shift_amounts >> 9) & 7;

    u32 part1_rom = hdr->part1_romaddr * (1 << (2 + shift1));
    u32 part1_ram = 0x02800000 - hdr->part1_ramaddr * (1 << (2 + shift2));
    u32 part2_rom = hdr->part2_romaddr * (1 << (2 + shift3));
    u32 part2_ram = 0x03810000 - hdr->part2_ramaddr * (1 << (2 + shift4));
    u32 part3_rom = hdr->part3_romaddr * 8;
    u32 part4_rom = hdr->part4_romaddr * 8;
    u32 part5_rom = hdr->part5_romaddr * 8;

    printf("FW Header info:\n");
    printf("part3_romaddr(raw/calc): 0x%04X / 0x%06X\n", hdr->part3_romaddr, part3_rom);
    printf("part4_romaddr(raw/calc): 0x%04X / 0x%06X\n", hdr->part4_romaddr, part4_rom);
    printf("part34_crc16: 0x%04X\n", hdr->part34_crc16);
    printf("part12_crc16: 0x%04X\n", hdr->part12_crc16);
    printf("fw_identifier: '%c%c%c%c' 0x%02X 0x%02X 0x%02X 0x%02X\n",
           hdr->fw_identifier[0], hdr->fw_identifier[1],
           hdr->fw_identifier[2], hdr->fw_identifier[3],
           hdr->fw_identifier[0], hdr->fw_identifier[1],
           hdr->fw_identifier[2], hdr->fw_identifier[3]);
    printf("part1_romaddr(raw/calc): 0x%04X / 0x%06X\n", hdr->part1_romaddr, part1_rom);
    printf("part1_ramaddr(raw/calc): 0x%04X / 0x%08X\n", hdr->part1_ramaddr, part1_ram);
    printf("part2_romaddr(raw/calc): 0x%04X / 0x%06X\n", hdr->part2_romaddr, part2_rom);
    printf("part2_ramaddr(raw/calc): 0x%04X / 0x%08X\n", hdr->part2_ramaddr, part2_ram);
    printf("shift_amounts: 0x%04X (shift1=%d shift2=%d shift3=%d shift4=%d)\n",
           hdr->shift_amounts, shift1, shift2, shift3, shift4);
    printf("part5_romaddr(raw/8): 0x%04X / calc: 0x%08X\n", hdr->part5_romaddr, part5_rom);
    printf("fw_timestamp: 20%02X-%02X-%02X %02X:%02X\n",
           hdr->fw_timestamp[4], hdr->fw_timestamp[3], hdr->fw_timestamp[2],
           hdr->fw_timestamp[1], hdr->fw_timestamp[0]);
    printf("console_type: 0x%02X %s\n", hdr->console_type,
           console_types[hdr->console_type] ? console_types[hdr->console_type] : "Unknown");
}

int main(int argc, char* argv[]) {
    if(argc < 2) {
        printf("Usage: %s -unpack input.bin -o folder\n", argv[0]);
        printf("       %s -pack folder -o output.bin\n", argv[0]);
        return -1;
    }

    /* 分离变量：专门为 unpack 与 pack 各自保留独立变量，避免混淆 */
    char unpack_input_file[512] = {0};
    char unpack_output_folder[512] = {0};

    char pack_input_folder[512] = {0};
    char pack_output_file[512] = {0};

    char temp_o[512] = {0}; /* 暂存 -o 的值，循环结束后根据模式分配 */

    int unpack_mode = 0;
    int pack_mode = 0;

    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"-unpack")==0 && i+1<argc) {
            unpack_mode = 1;
            strcpy(unpack_input_file, argv[++i]);
        } else if(strcmp(argv[i],"-pack")==0 && i+1<argc) {
            pack_mode = 1;
            strcpy(pack_input_folder, argv[++i]);
        } else if(strcmp(argv[i],"-o")==0 && i+1<argc) {
            strcpy(temp_o, argv[++i]);
        } else {
            printf("Unknown or incomplete parameter: %s\n", argv[i]);
            return -1;
        }
    }

    if(unpack_mode && pack_mode) { printf("Cannot use both -unpack and -pack\n"); return -1; }
    if(!unpack_mode && !pack_mode) { printf("Specify -unpack or -pack\n"); return -1; }

    /* 根据模式把 -o 的临时值分配到各自变量 */
    if(unpack_mode) {
        if(temp_o[0] == '\0') { printf("Please specify output folder with -o for unpack mode\n"); return -1; }
        strcpy(unpack_output_folder, temp_o);
    } else if(pack_mode) {
        if(temp_o[0] == '\0') { printf("Please specify output file with -o for pack mode\n"); return -1; }
        strcpy(pack_output_file, temp_o);
    }

    init_console_types();

    if(unpack_mode) {
        // --- 保留原解包逻辑 ---
        FILE* fw_bin = fopen(unpack_input_file, "rb");
        if(!fw_bin) { printf("Failed to open file\n"); return -1; }
        fseek(fw_bin, 0, SEEK_END);
        size_t fw_size = ftell(fw_bin);
        fseek(fw_bin, 0, SEEK_SET);
        u8* fw_data = (u8*)malloc(fw_size);
        fread(fw_data, 1, fw_size, fw_bin);
        fclose(fw_bin);

        if(mkdir_recursive(unpack_output_folder) != 0){
            printf("Failed to create output folder: %s\n", unpack_output_folder);
            free(fw_data);
            return -1;
        }

        FW_HEADER* hdr = (FW_HEADER*)fw_data;
        print_fw_header(hdr);

        char out_path[1024];
        snprintf(out_path,sizeof(out_path),"%s/header.bin",unpack_output_folder);
        FILE* out = fopen(out_path,"wb");
        fwrite(fw_data,1,FW_HEADER_SIZE,out);
        fclose(out);

        int shift1 = hdr->shift_amounts & 7;
        int shift2 = (hdr->shift_amounts >> 3) & 7;
        int shift3 = (hdr->shift_amounts >> 6) & 7;
        int shift4 = (hdr->shift_amounts >> 9) & 7;

        u32 rom_addrs[5];
        rom_addrs[0] = hdr->part1_romaddr * (1 << (2 + shift1));
        rom_addrs[1] = hdr->part2_romaddr * (1 << (2 + shift3));
        rom_addrs[2] = hdr->part3_romaddr * 8;
        rom_addrs[3] = hdr->part4_romaddr * 8;
        rom_addrs[4] = hdr->part5_romaddr * 8;

        int indices[5] = {0,1,2,3,4};
        for(int i=0;i<5;i++)
            for(int j=i+1;j<5;j++)
                if(rom_addrs[indices[i]] > rom_addrs[indices[j]]) {
                    int t=indices[i]; indices[i]=indices[j]; indices[j]=t;
                }

        u32 sizes[5];
        for(int i=0;i<5;i++){
            int cur = indices[i];
            u32 next_addr = (i==4)? fw_size : rom_addrs[indices[i+1]];
            sizes[cur] = next_addr - rom_addrs[cur];
        }

        init_keycode(*(u32*)hdr->fw_identifier, 2, 0x0C);
        u8* part1_decrypted = (u8*)malloc(sizes[0]);
        u8* part2_decrypted = (u8*)malloc(sizes[1]);
        decrypt_buffer(fw_data + rom_addrs[0], part1_decrypted, sizes[0]);
        decrypt_buffer(fw_data + rom_addrs[1], part2_decrypted, sizes[1]);

        u32 effective_size[5];
        effective_size[0] = getCompressedLZ77Size(part1_decrypted); 
        effective_size[1] = getCompressedLZ77Size(part2_decrypted);
        effective_size[2] = getCompressedPart345Size(fw_data + rom_addrs[2]);
        effective_size[3] = getCompressedPart345Size(fw_data + rom_addrs[3]);
        effective_size[4] = getCompressedPart345Size(fw_data + rom_addrs[4]);

        for(int i=0;i<5;i++){
            u32 sz = sizes[i]<effective_size[i]? sizes[i] : effective_size[i];
            snprintf(out_path,sizeof(out_path),"%s/%s",unpack_output_folder,part_filenames[i]);
            out = fopen(out_path,"wb");
            if(!out){printf("Failed to create %s\n",out_path); continue;}
            if(i==0||i==1){ fwrite(fw_data + rom_addrs[i],1,(sz+7)&~7,out); }
            else { fwrite(fw_data + rom_addrs[i],1,sz,out); }
            fclose(out);
            printf("%s: offset=0x%06X, size=0x%06X, effective=0x%06X, pad=0x%06X\n",
                   part_filenames[i], rom_addrs[i], sizes[i], effective_size[i], sizes[i]-sz);
        }

        free(part1_decrypted);
        free(part2_decrypted);

        if(fw_data[0x17C] != 0xFF){
            printf("Flashme detected\n");
            FW_HEADER* hdr2 = (FW_HEADER*)(fw_data + 0x3F680);
            print_fw_header(hdr2);

            snprintf(out_path,sizeof(out_path),"%s/header_flashme.bin",unpack_output_folder);
            out = fopen(out_path,"wb");
            fwrite(hdr2,1,FW_HEADER_SIZE,out);
            fclose(out);

            // 计算 FlashMe part1/part2 地址
            u32 fm_rom[2];
            fm_rom[0] = hdr2->part1_romaddr * (1 << (2 + (hdr2->shift_amounts &7)));
            fm_rom[1] = hdr2->part2_romaddr * (1 << (2 + ((hdr2->shift_amounts>>6)&7)));
            const char* fm_names[2] = {"arm9_boot_code_flashme.bin","arm7_boot_code_flashme.bin"};

            // 按地址排序索引
            int fm_indices[2] = {0,1};
            if(fm_rom[0] > fm_rom[1]){
                int t = fm_indices[0]; fm_indices[0] = fm_indices[1]; fm_indices[1] = t;
            }

            u32 fm_sizes[2] = {0};
            u8* fm_parts[2] = {0};

            // 计算有效压缩大小并导出
            for(int i=0;i<2;i++){
                int idx = fm_indices[i];
                u32 next_addr = (i==1)? fw_size : fm_rom[fm_indices[i+1]];
                u32 raw_size = next_addr - fm_rom[idx];
                u32 eff_size = getCompressedLZ77Size(fw_data + fm_rom[idx]);

                // 分配并拷贝有效大小
                fm_sizes[idx] = (raw_size < eff_size) ? raw_size : eff_size;
                fm_parts[idx] = (u8*)malloc(fm_sizes[idx]);
                memcpy(fm_parts[idx], fw_data + fm_rom[idx], fm_sizes[idx]);

                // 写文件
                snprintf(out_path,sizeof(out_path),"%s/%s",unpack_output_folder,fm_names[idx]);
                out = fopen(out_path,"wb");
                if(out){ fwrite(fm_parts[idx],1,fm_sizes[idx],out); fclose(out); }

                printf("%s: offset=0x%06X, size=0x%06X, effective=0x%06X, pad=0x%06X\n",
                    fm_names[idx], fm_rom[idx], raw_size, fm_sizes[idx], raw_size-fm_sizes[idx]);

                free(fm_parts[idx]);
                fm_parts[idx] = NULL;
            }
        }

        free(fw_data);
        printf("Firmware unpacked successfully: %s\n",out_path);
        printf("Done.\n");
    }

	if(pack_mode){
		printf("Packing firmware from folder: %s\n", pack_input_folder);

		// 读取 header.bin
		char header_path[1024];
		snprintf(header_path,sizeof(header_path),"%s/header.bin",pack_input_folder);
		FILE* header_bin = fopen(header_path,"rb");
		if(!header_bin){ printf("Failed to open header: %s\n", header_path); return -1; }
		FW_HEADER hdr;
		fread(&hdr,1,FW_HEADER_SIZE,header_bin);
		fclose(header_bin);

		// 保存原始 ROM 地址顺序
		int shift1 = hdr.shift_amounts & 7;
		int shift2 = (hdr.shift_amounts >> 3) & 7;
		int shift3 = (hdr.shift_amounts >> 6) & 7;
		int shift4 = (hdr.shift_amounts >> 9) & 7;

		u32 orig_rom_addrs[5];
		orig_rom_addrs[0] = hdr.part1_romaddr * (1 << (2 + shift1));
		orig_rom_addrs[1] = hdr.part2_romaddr * (1 << (2 + shift3));
		orig_rom_addrs[2] = hdr.part3_romaddr * 8;
		orig_rom_addrs[3] = hdr.part4_romaddr * 8;
		orig_rom_addrs[4] = hdr.part5_romaddr * 8;

		// 排序索引（按原始地址升序）
		int indices[5] = {0,1,2,3,4};
		for(int i=0;i<5;i++)
			for(int j=i+1;j<5;j++)
				if(orig_rom_addrs[indices[i]] > orig_rom_addrs[indices[j]]) {
					int t = indices[i]; indices[i] = indices[j]; indices[j] = t;
				}

		// 读取子文件，计算大小并对齐，补0
		u32 sizes[5] = {0};
		u8* parts[5] = {0};
		for(int i=0;i<5;i++){
			char part_path[1024];
			snprintf(part_path,sizeof(part_path),"%s/%s",pack_input_folder,part_filenames[i]);
			FILE* f = fopen(part_path,"rb");
			if(!f){ printf("Missing file: %s\n", part_path); return -1; }
			fseek(f,0,SEEK_END);
			size_t fsize = ftell(f);
			fseek(f,0,SEEK_SET);

			// 对齐字节
			u32 align = (i<2) ? (1<<(2+(i==0?shift1:shift3))) : 8;
			sizes[i] = (fsize + align - 1)/align*align;

			// 分配对齐大小内存并补零
			parts[i] = (u8*)malloc(sizes[i]);
			memset(parts[i], 0, sizes[i]);
			fread(parts[i], 1, fsize, f);
			fclose(f);
		}

		// 计算新地址（按原始地址顺序叠放）
		u32 new_rom_addrs[5];
		for(int i=0;i<5;i++){
			int idx = indices[i];
			if(i==0){
				// 第一个按原始地址最小的放
				new_rom_addrs[idx] = orig_rom_addrs[idx];
			} else {
				int prev_idx = indices[i-1];
				u32 tentative = new_rom_addrs[prev_idx] + sizes[prev_idx];
				// 如果前一个 part 变小导致本 part 新地址比原地址更前面，则保留原地址
				if(tentative < orig_rom_addrs[idx]) tentative = orig_rom_addrs[idx];
				new_rom_addrs[idx] = tentative;
			}
		}
		// 根据新地址更新 header 中的字段
		hdr.part1_romaddr = new_rom_addrs[0] / (1 << (2 + shift1));
		hdr.part2_romaddr = new_rom_addrs[1] / (1 << (2 + shift3));
		hdr.part3_romaddr = new_rom_addrs[2] / 8;
		hdr.part4_romaddr = new_rom_addrs[3] / 8;
		hdr.part5_romaddr = new_rom_addrs[4] / 8;

        // 更新crc16
        u8 *part1_decrypted = NULL, *part2_decrypted = NULL;
        u8 *part1_decomp = NULL, *part2_decomp = NULL;
        u32 part1_decomp_size = 0, part2_decomp_size = 0;
        // part1 2 解密
        part1_decrypted = (u8*)malloc(sizes[0]);
        memcpy(part1_decrypted, parts[0], sizes[0]);
        part2_decrypted = (u8*)malloc(sizes[1]);
        memcpy(part2_decrypted, parts[1], sizes[1]);
        init_keycode(*(u32*)hdr.fw_identifier, 2, 0x0C);
        decrypt_buffer(parts[0], part1_decrypted, sizes[0]);
        decrypt_buffer(parts[1], part2_decrypted, sizes[1]);
        // 计算解压后大小并解压
        part1_decomp_size = decompressLZ77(NULL,part1_decrypted);
        part1_decomp = (u8*)malloc(part1_decomp_size);
        decompressLZ77(part1_decomp,part1_decrypted);
        part2_decomp_size = decompressLZ77(NULL,part2_decrypted);
        part2_decomp = (u8*)malloc(part2_decomp_size);
        decompressLZ77(part2_decomp,part2_decrypted);
        // 计算 part12 CRC（先 part1 再 part2）
        u16 crc_p1 = swiCRC(0xFFFF, (u32*)part1_decomp, (u32)part1_decomp_size);
        u16 part12_crc = swiCRC(crc_p1, (u32*)part2_decomp, (u32)part2_decomp_size);

        // --- 计算 part3/part4/part5 的解压并计算 part34 & part5 CRC ---
        u8 *p3_decomp = NULL, *p4_decomp = NULL, *p5_decomp = NULL;
        u32 p3_decomp_size = 0, p4_decomp_size = 0, p5_decomp_size = 0;
        // part345 解压
        p3_decomp_size = decompress_part345(NULL, parts[2]);
        p3_decomp = (u8*)malloc(p3_decomp_size);
        decompress_part345(p3_decomp, parts[2]);
        p4_decomp_size = decompress_part345(NULL, parts[3]);
        p4_decomp = (u8*)malloc(p3_decomp_size);
        decompress_part345(p4_decomp, parts[3]);
        p5_decomp_size = decompress_part345(NULL, parts[4]);
        p5_decomp = (u8*)malloc(p5_decomp_size);
        decompress_part345(p5_decomp, parts[4]);
        // 计算 part34 CRC（先 part3 再 part4）
        u16 crc_p3 = swiCRC(0xFFFF, (u32*)p3_decomp, (u32)p3_decomp_size);
        u16 part34_crc = swiCRC(crc_p3, (u32*)p4_decomp, (u32)p4_decomp_size);
        // 计算 part5 CRC
        u16 part5_crc = swiCRC(0xFFFF, (u32*)p5_decomp, (u32)p5_decomp_size);

        free(part1_decrypted); part1_decrypted = NULL;
        free(part2_decrypted); part2_decrypted = NULL;
        free(part1_decomp); part1_decomp = NULL;
        free(part2_decomp); part2_decomp = NULL;
        free(p3_decomp); p3_decomp = NULL;
        free(p4_decomp); p4_decomp = NULL;
        free(p5_decomp); p5_decomp = NULL;

        hdr.part12_crc16 = part12_crc;
        hdr.part34_crc16 = part34_crc;
        hdr.part5_crc16 = part5_crc;

		// 计算 firmware 总大小，支持 256K / 512K 模式
		u32 fw_size = new_rom_addrs[4] + sizes[4];
		if(fw_size <= 0x40000) fw_size = 0x40000; // 256KB
		else if(fw_size <= 0x80000) fw_size = 0x80000; // 512KB

		u8* fw_data = (u8*)calloc(1, fw_size);

		// 写入 header
		memcpy(fw_data, &hdr, FW_HEADER_SIZE);

		// 写入各 part
		for(int i=0;i<5;i++){
			memcpy(fw_data + new_rom_addrs[i], parts[i], sizes[i]);
			free(parts[i]);
			printf("%s: old_offset=0x%06X, new_offset=0x%06X, size=0x%06X\n",
				part_filenames[i], orig_rom_addrs[i], new_rom_addrs[i], sizes[i]);
		}

        // --- 写入 FlashMe (如果存在) ---
        char flashme_header_path[1024];
        snprintf(flashme_header_path, sizeof(flashme_header_path), "%s/header_flashme.bin", pack_input_folder);

        if(access(flashme_header_path, 0) == 0) {
            FILE* fh = fopen(flashme_header_path, "rb");
            if (fh) {
                FW_HEADER hdr2;
                fread(&hdr2, 1, FW_HEADER_SIZE, fh);
                fclose(fh);

                // FlashMe header 固定写入偏移 0x3F680
                u32 hdr2_offset;
                hdr2_offset = 0x3F680;
                //hdr2_offset = (fw_size == 0x40000) ? 0x3F680 : 0x7F680;
                memcpy(fw_data + hdr2_offset, &hdr2, FW_HEADER_SIZE);
                printf("FlashMe header written at offset 0x%06X\n", hdr2_offset);

                // 计算 FlashMe 各部分地址
                int fshift1 = hdr2.shift_amounts & 7;
                int fshift3 = (hdr2.shift_amounts >> 6) & 7;

                u32 fm_rom[2];
                fm_rom[0] = hdr2.part1_romaddr * (1 << (2 + fshift1));
                fm_rom[1] = hdr2.part2_romaddr * (1 << (2 + fshift3));

                const char* fm_files[2] = {
                    "arm9_boot_code_flashme.bin",
                    "arm7_boot_code_flashme.bin"
                };

                for (int i = 0; i < 2; i++) {
                    char path[1024];
                    snprintf(path, sizeof(path), "%s/%s", pack_input_folder, fm_files[i]);
                    FILE* f = fopen(path, "rb");
                    if (!f) {
                        printf("Missing FlashMe file: %s\n", path);
                        continue;
                    }
                    fseek(f, 0, SEEK_END);
                    size_t sz = ftell(f);
                    fseek(f, 0, SEEK_SET);
                    u8* buf = (u8*)malloc(sz);
                    fread(buf, 1, sz, f);
                    fclose(f);

                    memcpy(fw_data + fm_rom[i], buf, sz);
                    free(buf);
                    printf("%s written at 0x%06X (size 0x%06X)\n", fm_files[i], fm_rom[i], (u32)sz);
                }
            }
        }

		// 输出 firmware 到指定文件（-o 指向文件路径）
		// 确保输出文件的父目录存在
		char out_dir[1024];
		strncpy(out_dir, pack_output_file, sizeof(out_dir));
		out_dir[sizeof(out_dir)-1] = '\0';
		char *last_slash = strrchr(out_dir, '/');
		char *last_back  = strrchr(out_dir, '\\');
		char *last_sep = last_slash;
		if(!last_sep || (last_back && last_back > last_sep)) last_sep = last_back;
		if(last_sep) {
			*last_sep = '\0';
			if(mkdir_recursive(out_dir)!=0){ printf("Failed to create output folder\n"); free(fw_data); return -1; }
		}

		FILE* out = fopen(pack_output_file,"wb");
		if(!out){ printf("Failed to create output file: %s\n", pack_output_file); free(fw_data); return -1; }
		fwrite(fw_data,1,fw_size,out);
		fclose(out);
		free(fw_data);

		printf("Firmware packed successfully: %s\n",pack_output_file);
		printf("Done.\n");
	}



    return 0;
}
