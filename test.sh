 # unpack
./release/fwpack -unpack v7.bin -o ./v7/1-1_raw
 # decrypt part12
./release/fwcrypt -f ./v7/1-1_raw/header.bin -de ./v7/1-1_raw/arm9_boot_code.bin -o ./v7/1-2_decrypted/arm9_boot_code.bin
./release/fwcrypt -f ./v7/1-1_raw/header.bin -de ./v7/1-1_raw/arm7_boot_code.bin -o ./v7/1-2_decrypted/arm7_boot_code.bin
 # decompress part12345
./release/fwcomp -t 12 -de ./v7/1-2_decrypted/arm9_boot_code.bin -o ./v7/1-3_decompressed/arm9_boot_code.bin
./release/fwcomp -t 12 -de ./v7/1-2_decrypted/arm7_boot_code.bin -o ./v7/1-3_decompressed/arm7_boot_code.bin
./release/fwcomp -t 345 -de ./v7/1-1_raw/arm9_gui_code.bin -o ./v7/1-3_decompressed/arm9_gui_code.bin
./release/fwcomp -t 345 -de ./v7/1-1_raw/arm7_wifi_code.bin -o ./v7/1-3_decompressed/arm7_wifi_code.bin
./release/fwcomp -t 345 -de ./v7/1-1_raw/data_gfx.bin -o ./v7/1-3_decompressed/data_gfx.bin
 # compress part12345
./release/fwcomp -t 12 -co ./v7/1-3_decompressed/arm9_boot_code.bin -o ./v7/2-2_modified_compressed/arm9_boot_code.bin
./release/fwcomp -t 12 -co ./v7/1-3_decompressed/arm7_boot_code.bin -o ./v7/2-2_modified_compressed/arm7_boot_code.bin
./release/fwcomp -t 345 -co ./v7/1-3_decompressed/arm9_gui_code.bin -o ./v7/2-2_modified_compressed/arm9_gui_code.bin
./release/fwcomp -t 345 -co ./v7/1-3_decompressed/arm7_wifi_code.bin -o ./v7/2-2_modified_compressed/arm7_wifi_code.bin
./release/fwcomp -t 345 -co ./v7/1-3_decompressed/data_gfx.bin -o ./v7/2-2_modified_compressed/data_gfx.bin
 #encrypt part12
./release/fwcrypt -f ./v7/1-1_raw/header.bin -en ./v7/2-2_modified_compressed/arm9_boot_code.bin -o ./v7/2-3_modified_encrypted/arm9_boot_code.bin
./release/fwcrypt -f ./v7/1-1_raw/header.bin -en ./v7/2-2_modified_compressed/arm7_boot_code.bin -o ./v7/2-3_modified_encrypted/arm7_boot_code.bin
 # pack
mkdir -p ./v7/2-4_final
cp ./v7/1-1_raw/header.bin ./v7/2-4_final
cp ./v7/2-3_modified_encrypted/arm9_boot_code.bin ./v7/2-4_final/
cp ./v7/2-3_modified_encrypted/arm7_boot_code.bin ./v7/2-4_final/
cp ./v7/2-2_modified_compressed/arm9_gui_code.bin ./v7/2-4_final/
cp ./v7/2-2_modified_compressed/arm7_wifi_code.bin ./v7/2-4_final/
cp ./v7/2-2_modified_compressed/data_gfx.bin ./v7/2-4_final/
./release/fwpack -pack ./v7/2-4_final -o newv7.bin

 # unpack
./release/fwpack -unpack ique.bin -o ./ique/1-1_raw
 # decrypt part12
./release/fwcrypt -f ./ique/1-1_raw/header.bin -de ./ique/1-1_raw/arm9_boot_code.bin -o ./ique/1-2_decrypted/arm9_boot_code.bin
./release/fwcrypt -f ./ique/1-1_raw/header.bin -de ./ique/1-1_raw/arm7_boot_code.bin -o ./ique/1-2_decrypted/arm7_boot_code.bin
 # decompress part12345
./release/fwcomp -t 12 -de ./ique/1-2_decrypted/arm9_boot_code.bin -o ./ique/1-3_decompressed/arm9_boot_code.bin
./release/fwcomp -t 12 -de ./ique/1-2_decrypted/arm7_boot_code.bin -o ./ique/1-3_decompressed/arm7_boot_code.bin
./release/fwcomp -t 345 -de ./ique/1-1_raw/arm9_gui_code.bin -o ./ique/1-3_decompressed/arm9_gui_code.bin
./release/fwcomp -t 345 -de ./ique/1-1_raw/arm7_wifi_code.bin -o ./ique/1-3_decompressed/arm7_wifi_code.bin
./release/fwcomp -t 345 -de ./ique/1-1_raw/data_gfx.bin -o ./ique/1-3_decompressed/data_gfx.bin
 # compress part12345
./release/fwcomp -t 12 -co ./ique/1-3_decompressed/arm9_boot_code.bin -o ./ique/2-2_modified_compressed/arm9_boot_code.bin
./release/fwcomp -t 12 -co ./ique/1-3_decompressed/arm7_boot_code.bin -o ./ique/2-2_modified_compressed/arm7_boot_code.bin
./release/fwcomp -t 345 -co ./ique/1-3_decompressed/arm9_gui_code.bin -o ./ique/2-2_modified_compressed/arm9_gui_code.bin
./release/fwcomp -t 345 -co ./ique/1-3_decompressed/arm7_wifi_code.bin -o ./ique/2-2_modified_compressed/arm7_wifi_code.bin
./release/fwcomp -t 345 -co ./ique/1-3_decompressed/data_gfx.bin -o ./ique/2-2_modified_compressed/data_gfx.bin
 #encrypt part12
./release/fwcrypt -f ./ique/1-1_raw/header.bin -en ./ique/2-2_modified_compressed/arm9_boot_code.bin -o ./ique/2-3_modified_encrypted/arm9_boot_code.bin
./release/fwcrypt -f ./ique/1-1_raw/header.bin -en ./ique/2-2_modified_compressed/arm7_boot_code.bin -o ./ique/2-3_modified_encrypted/arm7_boot_code.bin
 # pack
mkdir -p ./ique/2-4_final
cp ./ique/1-1_raw/header.bin ./ique/2-4_final
cp ./ique/2-3_modified_encrypted/arm9_boot_code.bin ./ique/2-4_final/
cp ./ique/2-3_modified_encrypted/arm7_boot_code.bin ./ique/2-4_final/
cp ./ique/2-2_modified_compressed/arm9_gui_code.bin ./ique/2-4_final/
cp ./ique/2-2_modified_compressed/arm7_wifi_code.bin ./ique/2-4_final/
cp ./ique/2-2_modified_compressed/data_gfx.bin ./ique/2-4_final/
./release/fwpack -pack ./ique/2-4_final -o newique.bin


./release/fwcrypt -f ./ique/1-1_raw/header.bin -de ./ique/1-1_raw/unknown.bin -o ./ique/1-2_decrypted/unknown.bin
./release/fwcomp -t 12 -de ./ique/1-2_decrypted/unknown.bin -o ./ique/1-3_decompressed/unknown.bin
./release/fwcomp -t 345 -de ./ique/1-1_raw/unknown.bin -o ./ique/1-3_decompressed/unknown.bin