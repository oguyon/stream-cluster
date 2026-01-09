#include "common.h"
#include <math.h>
#include <stddef.h>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif

double framedist(Frame *a, Frame *b) {
    if (a->width != b->width || a->height != b->height) {
        return -1.0;
    }

    double sum = 0.0;
    long size = a->width * a->height;

    const double *restrict da = a->data;
    const double *restrict db = b->data;

    long i = 0;

    // Use AVX2 if supported and on x86
    #if defined(__AVX__) && (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86))
    // Process 4 doubles at a time
    if (size >= 4) {
        __m256d sum_vec = _mm256_setzero_pd();
        for (; i <= size - 4; i += 4) {
            __m256d va = _mm256_loadu_pd(&da[i]);
            __m256d vb = _mm256_loadu_pd(&db[i]);
            __m256d diff = _mm256_sub_pd(va, vb);
            #ifdef __FMA__
            sum_vec = _mm256_fmadd_pd(diff, diff, sum_vec);
            #else
            sum_vec = _mm256_add_pd(sum_vec, _mm256_mul_pd(diff, diff));
            #endif
        }
        // Horizontal add
        __m256d hsum = _mm256_hadd_pd(sum_vec, sum_vec);
        sum += ((double*)&hsum)[0] + ((double*)&hsum)[2];
    }
    #endif

    // Handle tail or scalar fallback
    for (; i < size; i++) {
        double diff = da[i] - db[i];
        sum += diff * diff;
    }

    return sqrt(sum);
}
