#ifndef COMMON_H
#define COMMON_H

#include <stdlib.h>
#include <stdint.h>
#include <time.h>

typedef struct {
    double *data;
    long width;
    long height;
    int id;
    uint64_t cnt0;
    struct timespec atime;
} Frame;

typedef struct {
    Frame anchor;
    int id;
    double prob;
} Cluster;

typedef struct {
    int assignment;
    int num_dists;
    int *cluster_indices;
    double *distances;
} FrameInfo;

int is_ascii_input_mode();

#endif // COMMON_H
