#include "ctaudit.h"

static volatile uint32_t sink;
static uint8_t secret[32];

/* ---- 1. buffer comparison ---- */
__attribute__((noinline)) static int cmp_early(const uint8_t *a, const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++) if (a[i] != b[i]) return 1;
    return 0;
}
__attribute__((noinline)) static int cmp_ct(const uint8_t *a, const uint8_t *b, size_t n) {
    uint8_t d = 0;
    for (size_t i = 0; i < n; i++) d |= a[i] ^ b[i];
    return d != 0;
}
/* class 0: input equals secret ; class 1: random input (differs at byte 0 w.p. ~1) */
static void prep_cmp(int cls, uint8_t *in) {
    if (cls == 0) memcpy(in, secret, 32); else ct_randbytes(in, 32);
}
static void run_cmp_early(const uint8_t *in) { sink = cmp_early(in, secret, 32); }
static void run_cmp_ct(const uint8_t *in)    { sink = cmp_ct(in, secret, 32); }

/* ---- 2. KyberSlash-like secret-dependent division by q = 3329 ---- */
static volatile uint32_t q_var = 3329; /* volatile: force a real div instruction */
static uint8_t fixed_blk[256 * 2];
static void prep_div(int cls, uint8_t *in) {
    if (cls == 0) memcpy(in, fixed_blk, sizeof fixed_blk); else ct_randbytes(in, sizeof fixed_blk);
}
__attribute__((noinline)) static uint32_t compress_div(const uint8_t *in) {
    uint32_t acc = 0, q = q_var;
    for (int i = 0; i < 256; i++) {
        uint32_t a = (in[2*i] | (in[2*i+1] << 8)) % q;          /* secret coefficient */
        acc += (((a << 10) + q / 2) / q) & 0x3ff;               /* secret-dependent division */
    }
    return acc;
}
/* division-free: multiply by precomputed reciprocal, as in the KyberSlash fix */
__attribute__((noinline)) static uint32_t compress_nodiv(const uint8_t *in) {
    uint32_t acc = 0;
    for (int i = 0; i < 256; i++) {
        uint32_t a = (in[2*i] | (in[2*i+1] << 8)) % 3329u;
        uint64_t d0 = ((uint64_t)((a << 10) + 1664)) * 1290167ULL;
        acc += (uint32_t)(d0 >> 32) & 0x3ff;
    }
    return acc;
}
static void prep_null(int cls, uint8_t *in) { (void)cls; ct_randbytes(in, sizeof fixed_blk); }
static void run_div(const uint8_t *in)   { sink = compress_div(in); }
static void run_nodiv(const uint8_t *in) { sink = compress_nodiv(in); }

int main(int argc, char **argv) {
    uint64_t n; FILE *csv = ct_args(argc, argv, &n);
    ct_randbytes(secret, 32);
    memset(fixed_blk, 0, sizeof fixed_blk); /* fixed class: all-zero coefficients (shortest divisions) */
    ct_target ts[] = {
        {"memcmp_early_exit", 32, prep_cmp, run_cmp_early},
        {"memcmp_constant_time", 32, prep_cmp, run_cmp_ct},
        {"div_q3329_secret", sizeof fixed_blk, prep_div, run_div},
        {"null_both_random", sizeof fixed_blk, prep_null, run_nodiv},
        {"div_free_reciprocal", sizeof fixed_blk, prep_div, run_nodiv},
    };
    for (size_t i = 0; i < sizeof ts / sizeof *ts; i++) {
        ct_result r = ct_run(&ts[i], 4 * n, csv); /* cheap targets: four times the measurements */
        ct_report(&ts[i], &r);
    }
    return 0;
}
