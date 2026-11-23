/* ctgrind-style check on tag comparison: the MAC key is marked uninitialised, so the
 * computed tag is secret-dependent. The early-exit comparison branches on it. */
#include <stdio.h>
#include <string.h>
#include <valgrind/memcheck.h>
#include "fips202.h"

static int tag_early(const uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < 32; i++) { if (a[i] != b[i]) return 0; }
    return 1;
}
static int tag_ct(const uint8_t *a, const uint8_t *b) {
    uint8_t d = 0;
    for (int i = 0; i < 32; i++) d |= a[i] ^ b[i];
    return d;
}

int main(void) {
    uint8_t key[32], buf[64] = {0}, tag[32], forged[32];
    memset(key, 7, 32);
    memset(forged, 9, 32);
    VALGRIND_MAKE_MEM_UNDEFINED(key, 32);
    memcpy(buf, key, 32);
    shake256(tag, 32, buf, 64);
    fprintf(stderr, "--- constant-time comparison (no report expected) ---\n");
    volatile uint8_t r = tag_ct(tag, forged);
    (void)r;
    fprintf(stderr, "--- early-exit comparison (report expected) ---\n");
    volatile int e = tag_early(tag, forged);
    (void)e;
    fprintf(stderr, "--- end ---\n");
    return 0;
}
