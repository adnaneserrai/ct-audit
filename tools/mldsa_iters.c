/* Counts rejection-loop iterations of ML-DSA-65 signing (PQClean clean) per message,
 * by wrapping the mask-expansion call that sits at the top of each iteration.
 * Build with: -Wl,--wrap=PQCLEAN_MLDSA65_CLEAN_polyvecl_uniform_gamma1 */
#include "ctaudit.h"
#include "api.h"
#include "randombytes.h"

int randombytes(uint8_t *out, size_t n) { memset(out, 0x5a, n); return 0; }

static unsigned iters;
struct polyvecl;
void __real_PQCLEAN_MLDSA65_CLEAN_polyvecl_uniform_gamma1(struct polyvecl *v, const uint8_t seed[64], uint16_t nonce);
void __wrap_PQCLEAN_MLDSA65_CLEAN_polyvecl_uniform_gamma1(struct polyvecl *v, const uint8_t seed[64], uint16_t nonce) {
    iters++;
    __real_PQCLEAN_MLDSA65_CLEAN_polyvecl_uniform_gamma1(v, seed, nonce);
}

int main(int argc, char **argv) {
    unsigned N = argc > 1 ? (unsigned)atoi(argv[1]) : 20000;
    static uint8_t pk[PQCLEAN_MLDSA65_CLEAN_CRYPTO_PUBLICKEYBYTES], sk[PQCLEAN_MLDSA65_CLEAN_CRYPTO_SECRETKEYBYTES];
    static uint8_t sig[PQCLEAN_MLDSA65_CLEAN_CRYPTO_BYTES];
    size_t sl; uint8_t m[64];
    PQCLEAN_MLDSA65_CLEAN_crypto_sign_keypair(pk, sk);
    static unsigned hist[64]; static double cyc[64]; unsigned maxi = 0;
    for (unsigned i = 0; i < N; i++) {
        ct_randbytes(m, 64); iters = 0;
        uint64_t a = __rdtsc();
        PQCLEAN_MLDSA65_CLEAN_crypto_sign_signature(sig, &sl, m, 64, sk);
        uint64_t e = __rdtsc();
        unsigned k = iters < 63 ? iters : 63;
        hist[k]++; cyc[k] += (double)(e - a); if (k > maxi) maxi = k;
    }
    printf("messages: %u\niterations  share    mean_cycles\n", N);
    for (unsigned k = 1; k <= maxi; k++) if (hist[k])
        printf("%10u  %6.2f%%  %10.0f\n", k, 100.0 * hist[k] / N, cyc[k] / hist[k]);
    return 0;
}
