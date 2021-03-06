/* multer.c - multiplier utility class */

#include "ntt.h"


void ntt_multer_init(ntt_multer_t* multer, int64_t N) {
    multer->N = N;

    multer->n_plans = 0;
    multer->plans = NULL;

    // maximum word to be handled
    int64_t max_word = 256;

    //printf("N: %lli\n", N);

    int64_t prod_p = 1;

    int64_t min_p = (max_word * max_word) * N;
    //printf("%lli\n", min_p);


    /*
    // current prime
    int64_t p = N * (min_p / N) + 1;
    
    // generate new 'p'
    while (!ntt_isprime(p)) {
        p += N;
    }

    // add to the plans
    multer->plans = realloc(multer->plans, sizeof(*multer->plans) * ++multer->n_plans);
    multer->plans[multer->n_plans - 1] = NTT_PLAN_BFLY_EMPTY;
    ntt_plan_bfly_init(&multer->plans[multer->n_plans - 1], N, p);
    // record product
    prod_p *= p;
    p += N;

    */

    int64_t p = N + 1;

    // TODO: Figure out how to select different primes for transforms
    while (prod_p < min_p) {
        // generate new 'p'
        while (!ntt_isprime(p)) {
            p += N;
        }

        // add to the plans
        multer->plans = realloc(multer->plans, sizeof(*multer->plans) * ++multer->n_plans);
        multer->plans[multer->n_plans - 1] = NTT_PLAN_BFLY_EMPTY;
        ntt_plan_bfly_init(&multer->plans[multer->n_plans - 1], N, p);
        // record product
        prod_p *= p;
        p += N;

    }

    multer->prod_p = prod_p;

    // allocate temporary buffers
    multer->nttA = malloc(sizeof(*multer->nttA) * multer->n_plans);
    multer->nttB = malloc(sizeof(*multer->nttB) * multer->n_plans);
    multer->nttC = malloc(sizeof(*multer->nttC) * multer->n_plans);
    multer->C = malloc(sizeof(*multer->C) * multer->n_plans);
    int64_t i;
    for (i = 0; i < multer->n_plans; ++i) {
        multer->nttA[i] = malloc(sizeof(**multer->nttA) * N);
        multer->nttB[i] = malloc(sizeof(**multer->nttB) * N);
        multer->nttC[i] = malloc(sizeof(**multer->nttC) * N);
        multer->C[i] = malloc(sizeof(**multer->C) * N);
    }

    // now, calculate CRT bits
    multer->CRT_p = malloc(sizeof(*multer->CRT_p) * multer->n_plans);

    for (i = 0; i < multer->n_plans; ++i) {
        // compute the product of all 'p' except for the current
        multer->CRT_p[i].Ni = prod_p / multer->plans[i].p;
        // compute the inverse of Ni (mod p)
        multer->CRT_p[i].d = ntt_modinv(multer->CRT_p[i].Ni, multer->plans[i].p);
    }

}



void ntt_multer_mult(ntt_multer_t* multer, int64_t* A, int64_t* B, int64_t* C) {
    int64_t i;

    // apply forward NTT's on all plans
    #pragma omp parallel for
    for (i = 0; i < multer->n_plans; ++i) {
        ntt_plan_bfly_NTT(&multer->plans[i], A, multer->nttA[i]);
        ntt_plan_bfly_NTT(&multer->plans[i], B, multer->nttB[i]);
    }

    // convolve via pointwise multiplication
    #pragma omp parallel for
    for (i = 0; i < multer->n_plans; ++i) {
        int64_t j;
        for (j = 0; j < multer->N; ++j) {
            multer->nttC[i][j] = (multer->nttA[i][j] * multer->nttB[i][j]) % multer->plans[i].p;
        }
    }

    // inverse NTT to find value in 'C'
    #pragma omp parallel for
    for (i = 0; i < multer->n_plans; ++i) {
        ntt_plan_bfly_INTT(&multer->plans[i], multer->nttC[i], multer->C[i]);
    }

    // now, combine to get the actual 'digits'
    if (multer->n_plans == 1) {
        // just copy it over (no CRT required)
        memcpy(C, multer->C[0], sizeof(*C) * multer->N);
    } else {
        // Now, we have found 'C' modulo all the 'p' from plans, so we must combine via CRT
        for (i = 0; i < multer->N; ++i) {
            int64_t C_i = 0;

            // sum c_i * N_i * d_i
            int64_t j;
            for (j = 0; j < multer->n_plans; ++j) {
                C_i += ntt_modmul(multer->CRT_p[j].Ni, ntt_modmul(multer->CRT_p[j].d, multer->C[j][i], multer->prod_p), multer->prod_p);
                C_i %= multer->prod_p;
            }

            // now, we have the unique solution mod (prod_p), which
            //   should be large enough for any 'digit'
            if (C_i < 0) C_i += multer->prod_p;
            C[i] = C_i;
        }
    }
}

