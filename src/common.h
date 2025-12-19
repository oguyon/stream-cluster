#ifndef COMMON_H
#define COMMON_H

#include <stdlib.h>

typedef struct {
    double *data;
    long width;
    long height;
    int id;
} Frame;

typedef struct {
    Frame anchor;
    int id;
    double prob;
} Cluster;

#endif // COMMON_H
