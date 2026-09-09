
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
   encrypt_buffer() accepts any non-zero plaintext length and rounds it up to
   eight bytes.  A partial final block is zero-filled to four bytes; if a second
   word is still needed, it receives the first four bytes of KEY1(keybuf[0:8]).
   All padding is supplied before encryption.  Already 8-byte-aligned input,
   including any explicitly supplied tail, is preserved before transformation.
   decrypt_buffer() requires a non-zero, 8-byte-aligned ciphertext length and
   returns exactly that length; logical P12/LZ trimming belongs to its caller. */
int decrypt_buffer(const u8 *src, u8 *dest, int src_size);
int encrypt_buffer(const u8 *src, u8 *dest, int src_size);

#ifdef __cplusplus
}
#endif

#endif
