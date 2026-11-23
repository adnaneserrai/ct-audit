/* ML-KEM-768 (PQClean, clean implementation): decapsulation and encapsulation. */
#include "ctaudit.h"
#include "api.h"
#include "randombytes.h"

#define PK  PQCLEAN_MLKEM768_CLEAN_CRYPTO_PUBLICKEYBYTES
#define SK  PQCLEAN_MLKEM768_CLEAN_CRYPTO_SECRETKEYBYTES
#define CT  PQCLEAN_MLKEM768_CLEAN_CRYPTO_CIPHERTEXTBYTES
#define SS  PQCLEAN_MLKEM768_CLEAN_CRYPTO_BYTES

/* randombytes() is fed from a buffer so that encapsulation randomness is controlled by the harness */
static uint8_t rnd_buf[64]; static size_t rnd_pos;
int randombytes(uint8_t *out, size_t n) {
    for (size_t i = 0; i < n; i++) out[i] = rnd_buf[(rnd_pos++) % sizeof rnd_buf];
    return 0;
}

static uint8_t pk[PK], sk[SK], fixed_ct[CT], fixed_m[32];
static volatile uint8_t sink;

/* dec: class 0 = one fixed valid ciphertext, class 1 = fresh valid ciphertexts */
static void prep_dec(int cls, uint8_t *in) {
    if (cls == 0) { memcpy(in, fixed_ct, CT); return; }
    uint8_t ss[SS];
    ct_randbytes(rnd_buf, 32); rnd_pos = 0;
    PQCLEAN_MLKEM768_CLEAN_crypto_kem_enc(in, ss, pk);
}
static void run_dec(const uint8_t *in) {
    uint8_t ss[SS];
    PQCLEAN_MLKEM768_CLEAN_crypto_kem_dec(ss, in, sk);
    sink = ss[0];
}

/* enc: class 0 = fixed message m, class 1 = random m (the secret-like input) */
static void prep_enc(int cls, uint8_t *in) {
    if (cls == 0) memcpy(in, fixed_m, 32); else ct_randbytes(in, 32);
}
static void run_enc(const uint8_t *in) {
    uint8_t c[CT], ss[SS];
    memcpy(rnd_buf, in, 32); rnd_pos = 0;
    PQCLEAN_MLKEM768_CLEAN_crypto_kem_enc(c, ss, pk);
    sink = ss[0];
}

int main(int argc, char **argv) {
    uint64_t n; FILE *csv = ct_args(argc, argv, &n);
    for (int i = 0; i < 64; i++) rnd_buf[i] = (uint8_t)ct_rand();
    PQCLEAN_MLKEM768_CLEAN_crypto_kem_keypair(pk, sk);
    ct_randbytes(fixed_m, 32);
    memcpy(rnd_buf, fixed_m, 32); rnd_pos = 0;
    uint8_t ss[SS];
    PQCLEAN_MLKEM768_CLEAN_crypto_kem_enc(fixed_ct, ss, pk);
    ct_target ts[] = {
        {"mlkem768_dec", CT, prep_dec, run_dec},
        {"mlkem768_enc", 32, prep_enc, run_enc},
    };
    for (size_t i = 0; i < sizeof ts / sizeof *ts; i++) {
        ct_result r = ct_run(&ts[i], n / 3, csv); /* ML-KEM is slow: a third of the measurements */
        ct_report(&ts[i], &r);
    }
    return 0;
}
