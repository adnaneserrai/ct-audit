/* ctgrind-style check: the secret parts of the ML-KEM-768 secret key are marked as uninitialised for
 * valgrind's memcheck; any branch or memory index that depends on it is reported. */
#include <stdio.h>
#include <string.h>
#include <valgrind/memcheck.h>
#include "api.h"
#include "randombytes.h"

int randombytes(uint8_t *out, size_t n) { memset(out, 0x42, n); return 0; }

int main(void) {
    static uint8_t pk[PQCLEAN_MLKEM768_CLEAN_CRYPTO_PUBLICKEYBYTES], sk[PQCLEAN_MLKEM768_CLEAN_CRYPTO_SECRETKEYBYTES];
    static uint8_t ct[PQCLEAN_MLKEM768_CLEAN_CRYPTO_CIPHERTEXTBYTES];
    uint8_t ss[32], ss2[32];
    PQCLEAN_MLKEM768_CLEAN_crypto_kem_keypair(pk, sk);
    PQCLEAN_MLKEM768_CLEAN_crypto_kem_enc(ct, ss, pk);
    /* sk = indcpa secret (1152 B) || pk (1184 B) || H(pk) (32 B) || z (32 B).
     * Only the secret parts are marked: the embedded public key is public. */
    VALGRIND_MAKE_MEM_UNDEFINED(sk, 1152);
    VALGRIND_MAKE_MEM_UNDEFINED(sk + sizeof sk - 32, 32);
    PQCLEAN_MLKEM768_CLEAN_crypto_kem_dec(ss2, ct, sk);
    VALGRIND_MAKE_MEM_DEFINED(sk, sizeof sk);
    VALGRIND_MAKE_MEM_DEFINED(ss2, sizeof ss2);
    printf("decapsulation done, shared secrets %s\n", memcmp(ss, ss2, 32) ? "differ" : "match");
    return 0;
}
