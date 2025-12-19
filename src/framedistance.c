#include "common.h"
#include <math.h>
#include <stddef.h>

double framedist(Frame *a, Frame *b) {
    if (a->width != b->width || a->height != b->height) {
        // Should handle error, but for now return -1 or a large number
        // Assuming dimensions are consistent as per problem statement
        return -1.0;
    }

    double sum = 0.0;
    long size = a->width * a->height;
    for (long i = 0; i < size; i++) {
        double diff = a->data[i] - b->data[i];
        sum += diff * diff;
    }

    return sqrt(sum);
}
