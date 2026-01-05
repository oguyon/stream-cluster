#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Generator types
typedef enum {
    GEN_RANDOM,
    GEN_CIRCLE,
    GEN_WALK,
    GEN_SPIRAL
} GenType;

typedef struct {
    GenType type;
    double param; // generic parameter (period, step, loops)
} GeneratorConfig;

// Random double in [0, 1]
double rand_double() {
    return (double)rand() / (double)RAND_MAX;
}

// Generate random point in unit circle
void gen_random(double *x, double *y) {
    // Rejection sampling or polar coordinates
    // Polar: r = sqrt(u), theta = 2*pi*v ensures uniform area
    double r = sqrt(rand_double());
    double theta = 2.0 * M_PI * rand_double();
    *x = r * cos(theta);
    *y = r * sin(theta);
}

// Generate circle point
void gen_circle(double *x, double *y, long index, double period) {
    if (period <= 0.0) period = 1.0;
    double theta = 2.0 * M_PI * index / period;
    *x = cos(theta);
    *y = sin(theta);
}

// Generate random walk point
void gen_walk(double *x, double *y, double step_size) {
    // Current pos is passed in x, y
    // Try to take a step
    // If out of bounds, retry (or reflect? retry preserves distribution better near boundary for simple walk)
    // "bounded within radius 1"

    // Safety break
    int attempts = 0;
    double nx, ny;
    do {
        double angle = 2.0 * M_PI * rand_double();
        double r = step_size; // constant step size? "random walk" usually implies constant or gaussian. User didn't specify. Assuming constant step length.

        nx = *x + r * cos(angle);
        ny = *y + r * sin(angle);

        attempts++;
        if (attempts > 100) {
            // If stuck (unlikely unless step size is huge), stay put
            nx = *x;
            ny = *y;
            break;
        }
    } while (nx*nx + ny*ny > 1.0);

    *x = nx;
    *y = ny;
}

// Generate spiral
void gen_spiral(double *x, double *y, long index, long total_points, double loops) {
    double t = (double)index / (double)total_points;
    double r = t; // Radius grows linearly
    double theta = 2.0 * M_PI * loops * t;
    *x = r * cos(theta);
    *y = r * sin(theta);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <N> <filename> [pattern]\n", argv[0]);
        printf("Patterns:\n");
        printf("  random        Uniform random in unit circle (default)\n");
        printf("  circle[P]     Circle radius 1. P = period (default N)\n");
        printf("  walk[S]       Random walk in unit circle. S = step size (default 0.1)\n");
        printf("  spiral[L]     Spiral. L = loops (default 3)\n");
        return 1;
    }

    long n_points = atol(argv[1]);
    char *filename = argv[2];
    char *pattern_str = (argc > 3) ? argv[3] : "random";

    GeneratorConfig config;
    config.type = GEN_RANDOM;
    config.param = 0.0;

    // Parse pattern
    if (strncmp(pattern_str, "circle", 6) == 0) {
        config.type = GEN_CIRCLE;
        char *p = pattern_str + 6;
        if (*p) config.param = atof(p);
        else config.param = (double)n_points;
    } else if (strncmp(pattern_str, "walk", 4) == 0) {
        config.type = GEN_WALK;
        char *p = pattern_str + 4;
        if (*p) config.param = atof(p);
        else config.param = 0.1;
    } else if (strncmp(pattern_str, "spiral", 6) == 0) {
        config.type = GEN_SPIRAL;
        char *p = pattern_str + 6;
        if (*p) config.param = atof(p);
        else config.param = 3.0;
    } else {
        // Default random
        config.type = GEN_RANDOM;
    }

    FILE *f = fopen(filename, "w");
    if (!f) {
        perror("Failed to open output file");
        return 1;
    }

    srand(time(NULL));

    double x = 0.0, y = 0.0; // State for walk

    for (long i = 0; i < n_points; i++) {
        double out_x, out_y;

        switch (config.type) {
            case GEN_CIRCLE:
                gen_circle(&out_x, &out_y, i, config.param);
                break;
            case GEN_WALK:
                gen_walk(&x, &y, config.param); // Updates x, y in place
                out_x = x;
                out_y = y;
                break;
            case GEN_SPIRAL:
                gen_spiral(&out_x, &out_y, i, n_points, config.param);
                break;
            case GEN_RANDOM:
            default:
                gen_random(&out_x, &out_y);
                break;
        }

        fprintf(f, "%f %f\n", out_x, out_y);
    }

    fclose(f);
    return 0;
}
