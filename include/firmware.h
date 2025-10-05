
#ifndef FIRMWARE_H
#define FIRMWARE_H

typedef struct {
	u16	part3_romaddr;
	u16	part4_romaddr;
	u16	part34_crc16;
	u16	part12_crc16;
	u8	fw_identifier[4];
	u16	part1_romaddr;
	u16	part1_ramaddr;
	u16	part2_romaddr;
	u16	part2_ramaddr;
	u16	shift_amounts;
	u16	part5_romaddr;

	u8	fw_timestamp[5];
	u8	console_type;
	u16	unused1;
	u16	user_settings_offset;
	u16	unknown1;
	u16	unknown2;
	u16	part5_crc16;
	u16	unused2;
} FW_HEADER;

#define FW_HEADER_SIZE 0x200

#endif