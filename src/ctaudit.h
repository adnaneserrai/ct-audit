/* ctaudit.h - minimal dudect-style fixed-vs-random timing leak test.
 * Method: Reparaz, Balasch, Verbauwhede, "Dude, is my code constant time?" (2016).
 *
 * Per target, measurements are split into two classes (fixed input / random input)
 * and tested with Welch's t-test on: the raw timings, five percentile-cropped
 * versions (to limit the effect of outliers) and a second-order test on the
 * centered square of the timings. This can only fail to detect a leak; it never
 * proves that code is constant time. */
#ifndef CTAUDIT_H
#define CTAUDIT_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <x86intrin.h>

#define NCROP 6          /* raw + 5 percentile crops */
#define NTESTS (NCROP + 1) /* + second order */

typedef struct { double n, mean, m2; } welch_t;

static inline void welch_push(welch_t *w, double x) {
    w->n += 1;
    double d = x - w->mean;
    w->mean += d / w->n;
    w->m2 += d * (x - w->mean);
}
static inline double welch_t_stat(const welch_t *a, const welch_t *b) {
    if (a->n < 2 || b->n < 2) return 0;
    double va = a->m2 / (a->n - 1), vb = b->m2 / (b->n - 1);
    double den = sqrt(va / a->n + vb / b->n);
    return den > 0 ? (a->mean - b->mean) / den : 0;
}

/* xorshift64* PRNG (portable, seedable; no /dev/urandom) */
static uint64_t ct_rng_state = 0x9E3779B97F4A7C15ULL;
static inline uint64_t ct_rand(void) {
    uint64_t x = ct_rng_state;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    ct_rng_state = x;
    return x * 0x2545F4914F6CDD1DULL;
}
static inline void ct_randbytes(uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)ct_rand();
}
static inline void ct_seed(uint64_t s) {
    ct_rng_state ^= (s + 1) * 0xD6E8FEB86659FD93ULL;
    if (!ct_rng_state) ct_rng_state = 1;
    for (int i = 0; i < 16; i++) ct_rand();
}

typedef struct {
    const char *name;
    size_t in_len;
    void (*prepare)(int cls, uint8_t *in); /* class 0 = fixed, class 1 = random */
    void (*run)(const uint8_t *in);        /* operation under test */
} ct_target;

typedef struct {
    uint64_t n;            /* measurements used (both classes, after calibration) */
    double t[NTESTS];      /* t[0] raw, t[1..5] crops, t[6] second order */
    double max_abs;        /* max |t| over all tests */
} ct_result;

static int ct_cmp_u64(const void *x, const void *y) {
    uint64_t a = *(const uint64_t *)x, b = *(const uint64_t *)y;
    return a < b ? -1 : a > b;
}

/* Runs n_meas measurements. If csv is non-NULL, appends rows
 * "target,n,t_raw,t_crop1..5,t_second" every 10 batches. Inputs for a whole batch are
 * prepared before any timing: preparing an input right before its timed call leaves
 * class-dependent state (pending stores, cache lines) inside the timed window. */
static ct_result ct_run(const ct_target *t, uint64_t n_meas, FILE *csv) {
    enum { BATCH = 1000, CAL = 20000 };
    static const double pct[NCROP - 1] = {0.5, 0.75, 0.9, 0.95, 0.99};
    uint8_t *in = malloc(t->in_len * BATCH);
    uint64_t *ticks = malloc(sizeof(uint64_t) * BATCH), *cal = malloc(sizeof(uint64_t) * CAL);
    uint8_t cls[BATCH];
    uint64_t thr[NCROP - 1] = {0}, ncal = 0, done = 0;
    welch_t w[NTESTS][2];
    int calibrated = 0;
    ct_result res; memset(&res, 0, sizeof res); memset(w, 0, sizeof w);
    while (done < n_meas) {
        for (int i = 0; i < BATCH; i++) { cls[i] = ct_rand() & 1; t->prepare(cls[i], in + (size_t)i * t->in_len); }
        for (int i = 0; i < BATCH; i++) {
            _mm_lfence(); uint64_t a = __rdtsc(); _mm_lfence();
            t->run(in + (size_t)i * t->in_len);
            _mm_lfence(); uint64_t e = __rdtsc(); _mm_lfence();
            ticks[i] = e - a;
        }
        done += BATCH;
        for (int i = 0; i < BATCH; i++) {
            if (!calibrated) { /* first CAL samples only set the crop thresholds */
                cal[ncal++] = ticks[i];
                if (ncal == CAL) {
                    qsort(cal, CAL, sizeof *cal, ct_cmp_u64);
                    for (int k = 0; k < NCROP - 1; k++) thr[k] = cal[(uint64_t)(pct[k] * (CAL - 1))];
                    calibrated = 1;
                }
                continue;
            }
            double x = (double)ticks[i];
            int c = cls[i];
            welch_push(&w[0][c], x);
            for (int k = 0; k < NCROP - 1; k++) if (ticks[i] <= thr[k]) welch_push(&w[k + 1][c], x);
            if (w[0][c].n > 100) { /* second order: centered square, using the running class mean */
                double d = x - w[0][c].mean;
                welch_push(&w[NCROP][c], d * d);
            }
            res.n++;
        }
        if (calibrated) {
            for (int k = 0; k < NTESTS; k++) res.t[k] = welch_t_stat(&w[k][0], &w[k][1]);
            if (csv && (done / BATCH) % 10 == 0) {
                fprintf(csv, "%s,%llu", t->name, (unsigned long long)res.n);
                for (int k = 0; k < NTESTS; k++) fprintf(csv, ",%.3f", res.t[k]);
                fprintf(csv, "\n");
            }
        }
    }
    for (int k = 0; k < NTESTS; k++) if (fabs(res.t[k]) > res.max_abs) res.max_abs = fabs(res.t[k]);
    free(in); free(ticks); free(cal);
    return res;
}

/* Machine-readable result line: RESULT name n t_raw t_crop1..5 t_second max_abs */
static void ct_report(const ct_target *t, const ct_result *r) {
    printf("RESULT %s %llu", t->name, (unsigned long long)r->n);
    for (int k = 0; k < NTESTS; k++) printf(" %.3f", r->t[k]);
    printf(" %.3f\n", r->max_abs);
    fflush(stdout);
}

/* Common command line: [n_measurements] [csv_file|-] [seed] */
static FILE *ct_args(int argc, char **argv, uint64_t *n) {
    FILE *csv = NULL;
    *n = argc > 1 ? strtoull(argv[1], 0, 10) : 1000000;
    if (argc > 2 && strcmp(argv[2], "-")) csv = fopen(argv[2], "w");
    ct_seed(argc > 3 ? strtoull(argv[3], 0, 10) : 0);
    return csv;
}
#endif
