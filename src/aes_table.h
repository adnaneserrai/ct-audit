/* Minimal AES-128 encryption with a 256-byte S-box lookup table (the classic
 * secret-indexed table access). With aes_scan set, every lookup instead reads all
 * 256 entries and selects one by masking, so the access pattern is input independent. */
#ifndef AES_TABLE_H
#define AES_TABLE_H
#include <stdint.h>
#include <string.h>
static int aes_scan;
static uint8_t sbox[256];
static void init_sbox(void) {
    uint8_t p = 1, q = 1;
    do {
        p = p ^ (p << 1) ^ ((p & 0x80) ? 0x1B : 0);
        q ^= q << 1; q ^= q << 2; q ^= q << 4; if (q & 0x80) q ^= 0x09;
        uint8_t x = q ^ (q << 1 | q >> 7) ^ (q << 2 | q >> 6) ^ (q << 3 | q >> 5) ^ (q << 4 | q >> 4);
        sbox[p] = x ^ 0x63;
    } while (p != 1);
    sbox[0] = 0x63;
}
static uint8_t xt(uint8_t x) { return (x << 1) ^ ((x >> 7) * 0x1b); }

static inline uint8_t SB(uint8_t x) {
    if (!aes_scan) return sbox[x];
    uint8_t r = 0;
    for (int i = 0; i < 256; i++) r |= sbox[i] & (uint8_t)-(uint8_t)(i == x);
    return r;
}
static uint8_t rk[11][16];
static void table_keyexp(const uint8_t *key) {
    memcpy(rk[0], key, 16); uint8_t rc = 1;
    for (int r = 1; r <= 10; r++) {
        uint8_t *p = rk[r - 1], *n = rk[r];
        n[0] = p[0] ^ SB(p[13]) ^ rc; n[1] = p[1] ^ SB(p[14]);
        n[2] = p[2] ^ SB(p[15]);      n[3] = p[3] ^ SB(p[12]);
        for (int i = 4; i < 16; i++) n[i] = p[i] ^ n[i - 4];
        rc = xt(rc);
    }
}
__attribute__((noinline)) static void aes_table_enc(uint8_t *out, const uint8_t *in) {
    uint8_t s[16], t[16];
    for (int i = 0; i < 16; i++) s[i] = in[i] ^ rk[0][i];
    for (int r = 1; r <= 10; r++) {
        for (int c = 0; c < 4; c++) for (int j = 0; j < 4; j++) t[4*c+j] = SB(s[4*((c+j)&3)+j]);
        if (r < 10) for (int c = 0; c < 4; c++) {
            uint8_t *a = t + 4*c, a0 = a[0], x = a[0]^a[1]^a[2]^a[3];
            a[0] ^= x ^ xt(a[0]^a[1]); a[1] ^= x ^ xt(a[1]^a[2]);
            a[2] ^= x ^ xt(a[2]^a[3]); a[3] ^= x ^ xt(a[3]^a0);
        }
        for (int i = 0; i < 16; i++) s[i] = t[i] ^ rk[r][i];
    }
    memcpy(out, s, 16);
}
#endif
