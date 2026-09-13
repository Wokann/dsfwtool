
#ifndef ENCRYPTION_H
#define ENCRYPTION_H

#include "nds_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void init_keycode(u32 idcode, u32 level, u32 modulo);
void crypt_64bit_down(u8 *ptr);
void crypt_64bit_up(u8 *ptr);

/* Initialize the P1/P2 KEY1 schedule with level 2 and modulo 0x0C before use.
   encrypt_buffer() reproduces Nintendo retail padding: it rounds a partial
   final block up to eight bytes, fills the first word with zeroes as needed,
   and uses the first four bytes of KEY1(keybuf[0:8]) when its second word is
   required.  encrypt_buffer_zero_padded() uses all-zero final padding for
   FlashMe's historical P1/P2 streams.  Both supply padding before encryption;
   an already 8-byte-aligned input is preserved as supplied.  decrypt_buffer()
   requires a non-zero, 8-byte-aligned ciphertext length and returns exactly
   that length; logical P12/LZ trimming belongs to its caller. */
int decrypt_buffer(const u8 *src, u8 *dest, int src_size);
int encrypt_buffer(const u8 *src, u8 *dest, int src_size);
int encrypt_buffer_zero_padded(const u8 *src, u8 *dest, int src_size);

#ifdef __cplusplus
}
#endif

#endif
