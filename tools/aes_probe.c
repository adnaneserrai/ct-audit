/* Follow-up experiments on the S-box AES-128 timing observation.
 * Variants: different fixed plaintexts, null test (both classes random), S-box access
 * made input independent (aes_scan), cold S-box (cache lines flushed before each call),
 * and the bitsliced implementation as a reference. */
#include "ctaudit.h"
#include "aes.h"
#include "aes_table.h"
#include "randombytes.h"

int randombytes(uint8_t *out, size_t n) { memset(out, 0, n); return 0; }

static volatile uint8_t sink;
static aes128ctx actx;
static uint8_t fixedA[16], fixedB[16];

static void prep_A(int c, uint8_t *in)    { if (c == 0) memcpy(in, fixedA, 16); else ct_randbytes(in, 16); }
static void prep_B(int c, uint8_t *in)    { if (c == 0) memcpy(in, fixedB, 16); else ct_randbytes(in, 16); }
static void prep_zero(int c, uint8_t *in) { if (c == 0) memset(in, 0, 16); else ct_randbytes(in, 16); }
static void prep_null(int c, uint8_t *in) { (void)c; ct_randbytes(in, 16); }

static void run_table(const uint8_t *in) { uint8_t o[16]; aes_scan = 0; aes_table_enc(o, in); sink = o[0]; }
static void run_scan(const uint8_t *in)  { uint8_t o[16]; aes_scan = 1; aes_table_enc(o, in); sink = o[0]; }
static void run_cold(const uint8_t *in) {
    uint8_t o[16];
    for (int i = 0; i < 256; i += 64) _mm_clflush(&sbox[i]);
    _mm_mfence();
    aes_scan = 0; aes_table_enc(o, in); sink = o[0];
}
static void run_bs(const uint8_t *in) { uint8_t o[16]; aes128_ecb(o, in, 1, &actx); sink = o[0]; }

int main(int argc, char **argv) {
    uint64_t n; FILE *csv = ct_args(argc, argv, &n);
    uint8_t key[16];
    ct_randbytes(key, 16); ct_randbytes(fixedA, 16); ct_randbytes(fixedB, 16);
    init_sbox(); table_keyexp(key); aes128_ecb_keyexp(&actx, key);
    ct_target ts[] = {
        {"table_fixedA",      16, prep_A,    run_table},
        {"table_fixedB",      16, prep_B,    run_table},
        {"table_fixed_zero",  16, prep_zero, run_table},
        {"table_null",        16, prep_null, run_table},
        {"table_scan_fixedA", 16, prep_A,    run_scan},
        {"table_scan_null",   16, prep_null, run_scan},
        {"table_cold_fixedA", 16, prep_A,    run_cold},
        {"table_cold_null",   16, prep_null, run_cold},
        {"bitsliced_fixedA",  16, prep_A,    run_bs},
        {"bitsliced_null",    16, prep_null, run_bs},
    };
    for (size_t i = 0; i < sizeof ts / sizeof *ts; i++) {
        ct_result r = ct_run(&ts[i], n, csv);
        ct_report(&ts[i], &r);
    }
    return 0;
}
