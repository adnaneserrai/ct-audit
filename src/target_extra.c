/* Additional targets: ML-DSA-65 signing, table-based vs bitsliced AES-128,
 * square-and-multiply modular exponentiation, MAC tag comparison. */
#include "ctaudit.h"
#include "api.h"
#include "aes.h"
#include "fips202.h"
#include "randombytes.h"
#include "aes_table.h"

static volatile uint64_t sink;
int randombytes(uint8_t *out, size_t n) { memset(out, 0x5a, n); return 0; } /* deterministic signing */

/* ---------- ML-DSA-65: fixed message vs random message (message is public; the
 * rejection-sampling loop makes signing time message dependent by design) ---------- */
#define DPK PQCLEAN_MLDSA65_CLEAN_CRYPTO_PUBLICKEYBYTES
#define DSK PQCLEAN_MLDSA65_CLEAN_CRYPTO_SECRETKEYBYTES
#define DSIG PQCLEAN_MLDSA65_CLEAN_CRYPTO_BYTES
static uint8_t dpk[DPK], dsk[DSK], fixed_msg[64];
static void prep_msg(int cls, uint8_t *in) { if (cls == 0) memcpy(in, fixed_msg, 64); else ct_randbytes(in, 64); }
static void run_mldsa(const uint8_t *in) {
    static uint8_t sig[DSIG]; size_t l;
    PQCLEAN_MLDSA65_CLEAN_crypto_sign_signature(sig, &l, in, 64, dsk);
    sink = sig[0];
}

/* ---------- AES-128: S-box table implementation vs PQClean bitsliced ---------- */
static aes128ctx actx;
static uint8_t fixed_pt[16];
static void prep_pt(int cls, uint8_t *in) { if (cls == 0) memcpy(in, fixed_pt, 16); else ct_randbytes(in, 16); }
static void run_aes_table(const uint8_t *in) { uint8_t o[16]; aes_table_enc(o, in); sink = o[0]; }
static void run_aes_bs(const uint8_t *in)    { uint8_t o[16]; aes128_ecb(o, in, 1, &actx); sink = o[0]; }

/* ---------- modular exponentiation, 64-bit modulus ---------- */
typedef unsigned __int128 u128;
static const uint64_t MOD = 0xFFFFFFFFFFFFFFC5ULL; /* 2^64 - 59, prime */
static uint64_t mulm(uint64_t a, uint64_t b) { return (uint64_t)((u128)a * b % MOD); }
__attribute__((noinline)) static uint64_t modexp_branch(uint64_t b, uint64_t e) {
    uint64_t r = 1;
    for (int i = 63; i >= 0; i--) { r = mulm(r, r); if ((e >> i) & 1) r = mulm(r, b); }
    return r;
}
__attribute__((noinline)) static uint64_t modexp_always(uint64_t b, uint64_t e) {
    uint64_t r = 1;
    for (int i = 63; i >= 0; i--) {
        r = mulm(r, r);
        uint64_t t = mulm(r, b), m = -(uint64_t)((e >> i) & 1);
        r = (t & m) | (r & ~m);
    }
    return r;
}

/* Montgomery variant: no division, R = 2^64, MOD = 2^64 - 59 so R mod MOD = 59 */
static uint64_t mod_inv64(void) { uint64_t x = MOD; for (int i = 0; i < 6; i++) x *= 2 - MOD * x; return x; }
static uint64_t MINV;
static inline uint64_t redc(u128 T) {
    uint64_t m = (uint64_t)T * MINV;
    uint64_t hi = (uint64_t)(T >> 64), mh = (uint64_t)(((u128)m * MOD) >> 64);
    uint64_t r = hi - mh, borrow = hi < mh;
    return r + (MOD & -borrow); /* constant-time conditional add */
}
static inline uint64_t montmul(uint64_t a, uint64_t b) { return redc((u128)a * b); }
__attribute__((noinline)) static uint64_t modexp_mont(uint64_t b, uint64_t e) {
    uint64_t bm = montmul(b, 59ULL * 59ULL % MOD); /* b * R mod MOD (R^2 mod MOD = 59^2) */
    uint64_t r = 59; /* 1 in Montgomery form */
    for (int i = 63; i >= 0; i--) {
        r = montmul(r, r);
        uint64_t t = montmul(r, bm), m = -(uint64_t)((e >> i) & 1);
        r = (t & m) | (r & ~m);
    }
    return redc(r);
}
static void run_modexp_mont(const uint8_t *in) { uint64_t e; memcpy(&e, in, 8); sink = modexp_mont(7, e); }
/* the 128-bit modulo alone: small high word (fixed) vs random high word */
static void prep_u128(int cls, uint8_t *in) {
    uint64_t v[2] = { ct_rand(), cls == 0 ? 3 : ct_rand() }; memcpy(in, v, 16);
}
static void run_umod(const uint8_t *in) { u128 x; memcpy(&x, in, 16); sink = (uint64_t)(x % MOD); }
static uint64_t fixed_exp = 0x8000000000000001ULL; /* low Hamming weight vs ~32 for random */
static void prep_exp(int cls, uint8_t *in) {
    uint64_t e = cls == 0 ? fixed_exp : ct_rand(); memcpy(in, &e, 8);
}
static void run_modexp_branch(const uint8_t *in) { uint64_t e; memcpy(&e, in, 8); sink = modexp_branch(7, e); }
static void run_modexp_always(const uint8_t *in) { uint64_t e; memcpy(&e, in, 8); sink = modexp_always(7, e); }

/* ---------- MAC tag verification (SHAKE256(key || msg), 32-byte tag) ---------- */
static uint8_t mkey[32], mmsg[32], good_tag[32];
__attribute__((noinline)) static int tag_early(const uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < 32; i++) { if (a[i] != b[i]) return 0; }
    return 1;
}
__attribute__((noinline)) static int tag_ct(const uint8_t *a, const uint8_t *b) {
    uint8_t d = 0; for (int i = 0; i < 32; i++) d |= a[i] ^ b[i]; return d == 0;
}
static void mac(uint8_t *tag) {
    uint8_t buf[64]; memcpy(buf, mkey, 32); memcpy(buf + 32, mmsg, 32); shake256(tag, 32, buf, 64);
}
static void prep_tag(int cls, uint8_t *in) { if (cls == 0) memcpy(in, good_tag, 32); else ct_randbytes(in, 32); }
static void run_mac_early(const uint8_t *in) { uint8_t t[32]; mac(t); sink = tag_early(in, t); }
static void run_mac_ct(const uint8_t *in)    { uint8_t t[32]; mac(t); sink = tag_ct(in, t); }
static void prep_null(int cls, uint8_t *in) { (void)cls; ct_randbytes(in, 32); }

int main(int argc, char **argv) {
    uint64_t n; FILE *csv = ct_args(argc, argv, &n);
    PQCLEAN_MLDSA65_CLEAN_crypto_sign_keypair(dpk, dsk);
    ct_randbytes(fixed_msg, 64); ct_randbytes(fixed_pt, 16);
    ct_randbytes(mkey, 32); ct_randbytes(mmsg, 32); mac(good_tag);
    uint8_t key[16]; ct_randbytes(key, 16);
    MINV = mod_inv64();
    for (uint64_t e = 0; e < 1000; e++) {
        if (modexp_mont(7, e * 0x9E3779B97F4A7C15ULL) != modexp_branch(7, e * 0x9E3779B97F4A7C15ULL)) { fprintf(stderr, "montgomery mismatch\n"); return 1; }
    }
    init_sbox(); table_keyexp(key); aes128_ecb_keyexp(&actx, key);
    ct_target ts[] = {
        {"mldsa65_sign",        64, prep_msg, run_mldsa},
        {"aes128_sbox_table",   16, prep_pt,  run_aes_table},
        {"aes128_bitsliced",    16, prep_pt,  run_aes_bs},
        {"modexp_branching",     8, prep_exp, run_modexp_branch},
        {"modexp_select_always", 8, prep_exp, run_modexp_always},
        {"modexp_montgomery",    8, prep_exp, run_modexp_mont},
        {"umodti3_alone",       16, prep_u128, run_umod},
        {"mac_verify_early",    32, prep_tag, run_mac_early},
        {"mac_verify_ct",       32, prep_tag, run_mac_ct},
        {"null_mac_random",     32, prep_null, run_mac_ct},
    };
    for (size_t i = 0; i < sizeof ts / sizeof *ts; i++) {
        ct_result r = ct_run(&ts[i], i == 0 ? n / 10 : n, csv); /* ML-DSA signing is slow: a tenth */
        ct_report(&ts[i], &r);
    }
    return 0;
}
