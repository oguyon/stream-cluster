#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <ctype.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Generator types
typedef enum {
    GEN_RANDOM,
    GEN_CIRCLE,
    GEN_WALK,
    GEN_SPIRAL,
    GEN_SPHERE
} GenType;

typedef struct {
    GenType type;
    int dim;
    double param; // generic parameter (period, step, loops)
} GeneratorConfig;

// Random double in [0, 1]
double rand_double() {
    return (double)rand() / (double)RAND_MAX;
}

// Generate random point
void gen_random_point(double *out, int dim) {
    if (dim == 3) {
        double r = cbrt(rand_double());
        double costheta = 1.0 - 2.0 * rand_double();
        double phi = 2.0 * M_PI * rand_double();
        double sintheta = sqrt(1.0 - costheta * costheta);
        out[0] = r * sintheta * cos(phi);
        out[1] = r * sintheta * sin(phi);
        out[2] = r * costheta;
    } else if (dim == 2) {
        double r = sqrt(rand_double());
        double theta = 2.0 * M_PI * rand_double();
        out[0] = r * cos(theta);
        out[1] = r * sin(theta);
    } else {
        for (int d = 0; d < dim; d++) {
            out[d] = 2.0 * rand_double() - 1.0;
        }
    }
}

// Generate random point ON unit sphere
void gen_sphere_point(double *out, int dim) {
    if (dim == 3) {
        double costheta = 1.0 - 2.0 * rand_double();
        double phi = 2.0 * M_PI * rand_double();
        double sintheta = sqrt(1.0 - costheta * costheta);
        out[0] = sintheta * cos(phi);
        out[1] = sintheta * sin(phi);
        out[2] = costheta;
    } else if (dim == 2) {
        double theta = 2.0 * M_PI * rand_double();
        out[0] = cos(theta);
        out[1] = sin(theta);
    } else {
        double sum_sq = 0.0;
        for (int d = 0; d < dim; d++) {
            double u1 = rand_double();
            double u2 = rand_double();
            double z = sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
            out[d] = z;
            sum_sq += z * z;
        }
        double norm = sqrt(sum_sq);
        for (int d = 0; d < dim; d++) {
            out[d] /= norm;
        }
    }
}

void gen_circle_point(double *out, long index, double period, int dim) {
    if (period <= 0.0) period = 1.0;
    double theta = 2.0 * M_PI * index / period;
    out[0] = cos(theta);
    out[1] = sin(theta);
    for (int d = 2; d < dim; d++) out[d] = 0.0;
}

void gen_spiral_point(double *out, long index, long total_points, double loops, int dim) {
    double t = (double)index / (double)total_points;
    if (dim == 3) {
        // 3D Spiral (Conical)
        out[0] = t * cos(2.0 * M_PI * loops * t);
        out[1] = t * sin(2.0 * M_PI * loops * t);
        out[2] = 2.0 * t - 1.0; // z from -1 to 1
        for (int d = 3; d < dim; d++) out[d] = 0.0;
    } else {
        double r = t;
        double theta = 2.0 * M_PI * loops * t;
        out[0] = r * cos(theta);
        out[1] = r * sin(theta);
        for (int d = 2; d < dim; d++) out[d] = 0.0;
    }
}

void gen_walk_point(double *current, double step_size, int dim) {
    double *next = (double *)malloc(dim * sizeof(double));
    int attempts = 0;

    while (1) {
        if (dim == 3) {
            double costheta = 1.0 - 2.0 * rand_double();
            double phi = 2.0 * M_PI * rand_double();
            double sintheta = sqrt(1.0 - costheta * costheta);
            double dx = step_size * sintheta * cos(phi);
            double dy = step_size * sintheta * sin(phi);
            double dz = step_size * costheta;
            next[0] = current[0] + dx;
            next[1] = current[1] + dy;
            next[2] = current[2] + dz;
        } else if (dim == 2) {
            double angle = 2.0 * M_PI * rand_double();
            next[0] = current[0] + step_size * cos(angle);
            next[1] = current[1] + step_size * sin(angle);
        } else {
            // Random direction in N-dim
            double sum_sq = 0.0;
            for(int d=0; d<dim; d++) {
                double u1 = rand_double();
                double u2 = rand_double();
                double z = sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
                next[d] = z;
                sum_sq += z*z;
            }
            double norm = sqrt(sum_sq);
            for(int d=0; d<dim; d++) {
                next[d] = current[d] + (next[d] / norm) * step_size;
            }
        }

        // Check bounds (unit sphere)
        double r2 = 0.0;
        for (int d = 0; d < dim; d++) r2 += next[d] * next[d];

        if (r2 <= 1.0) break;

        attempts++;
        if (attempts > 100) {
            memcpy(next, current, dim * sizeof(double));
            break;
        }
    }

    memcpy(current, next, dim * sizeof(double));
    free(next);
}

void print_args_on_error(int argc, char *argv[]) {
    fprintf(stderr, "\nProgram arguments:\n");
    for (int i = 0; i < argc; i++) {
        fprintf(stderr, "  argv[%d] = \"%s\"\n", i, argv[i]);
    }
    fprintf(stderr, "\n");
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <N> <filename> [pattern] [options]\n", argv[0]);
        printf("Patterns:\n");
        printf("  [ND]random      Uniform random in unit hypercube/sphere (default 2D)\n");
        printf("  [ND]sphere      Random points on unit hypersphere surface\n");
        printf("  [ND]walk[S]     Random walk. S = step size (default 0.1)\n");
        printf("  [ND]spiral[L]   Spiral. L = loops (default 3.0)\n");
        printf("  [ND]circle[P]   Circle. P = period\n");
        printf("Options:\n");
        printf("  -repeat <M>     Repeat the pattern M times\n");
        printf("  -noise <R>      Add random noise with radius R to each point\n");
        printf("  -shuffle        Shuffle the order of generated points\n");
        print_args_on_error(argc, argv);
        return 1;
    }

    long n_points = atol(argv[1]);
    char *filename = argv[2];
    char *pattern_str = "2Drandom";

    long repeats = 1;
    double noise_radius = 0.0;
    int shuffle = 0;

    // Parse arguments
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-repeat") == 0) {
            repeats = atol(argv[++i]);
        } else if (strcmp(argv[i], "-noise") == 0) {
            noise_radius = atof(argv[++i]);
        } else if (strcmp(argv[i], "-shuffle") == 0) {
            shuffle = 1;
        } else if (argv[i][0] == '-') {
             fprintf(stderr, "Error: Unknown option: %s\n", argv[i]);
             return 1;
        } else {
            pattern_str = argv[i];
        }
    }

    GeneratorConfig config;
    config.type = GEN_RANDOM;
    config.dim = 2;
    config.param = 0.0;

    // Parse dimension [N]D
    char *dim_end = strchr(pattern_str, 'D');
    if (dim_end) {
        *dim_end = '\0';
        config.dim = atoi(pattern_str);
        pattern_str = dim_end + 1; // Advance past 'D'
    } else if (strncmp(pattern_str, "2D", 2) == 0) {
        config.dim = 2;
        pattern_str += 2;
    } else if (strncmp(pattern_str, "3D", 2) == 0) {
        config.dim = 3;
        pattern_str += 2;
    }

    if (config.dim < 1) config.dim = 2;

    // Parse pattern name
    if (strncmp(pattern_str, "random", 6) == 0) {
        config.type = GEN_RANDOM;
    } else if (strncmp(pattern_str, "sphere", 6) == 0) {
        config.type = GEN_SPHERE;
    } else if (strncmp(pattern_str, "walk", 4) == 0) {
        config.type = GEN_WALK;
        char *p = pattern_str + 4;
        if (*p) config.param = atof(p);
        else config.param = 0.1;
    } else if (strncmp(pattern_str, "circle", 6) == 0) {
        config.type = GEN_CIRCLE;
        char *p = pattern_str + 6;
        if (*p) config.param = atof(p);
        else config.param = (double)n_points;
    } else if (strncmp(pattern_str, "spiral", 6) == 0) {
        config.type = GEN_SPIRAL;
        char *p = pattern_str + 6;
        if (*p) config.param = atof(p);
        else config.param = 3.0;
    }

    FILE *f = fopen(filename, "w");
    if (!f) {
        perror("Failed to open output file");
        return 1;
    }

    srand(time(NULL));

    long total_points = n_points * repeats;

    double *base_buffer = (double *)malloc(n_points * config.dim * sizeof(double));
    double *current_walk = (double *)calloc(config.dim, sizeof(double));

    for (long i = 0; i < n_points; i++) {
        double *pt = &base_buffer[i * config.dim];
        switch (config.type) {
            case GEN_WALK:
                gen_walk_point(current_walk, config.param, config.dim);
                memcpy(pt, current_walk, config.dim * sizeof(double));
                break;
            case GEN_CIRCLE:
                gen_circle_point(pt, i, config.param, config.dim);
                break;
            case GEN_SPIRAL:
                gen_spiral_point(pt, i, n_points, config.param, config.dim);
                break;
            case GEN_SPHERE:
                gen_sphere_point(pt, config.dim);
                break;
            case GEN_RANDOM:
            default:
                gen_random_point(pt, config.dim);
                break;
        }
    }
    free(current_walk);

    double *final_buffer = (double *)malloc(total_points * config.dim * sizeof(double));

    for (long r = 0; r < repeats; r++) {
        for (long i = 0; i < n_points; i++) {
            long dest_idx = r * n_points + i;
            for (int d = 0; d < config.dim; d++) {
                double val = base_buffer[i * config.dim + d];
                if (noise_radius > 0.0) {
                    val += (2.0 * rand_double() - 1.0) * noise_radius; // Uniform noise
                }
                final_buffer[dest_idx * config.dim + d] = val;
            }
        }
    }
    free(base_buffer);

    if (shuffle) {
        for (long i = total_points - 1; i > 0; i--) {
            long j = (long)(rand_double() * (i + 1));
            for (int d = 0; d < config.dim; d++) {
                double temp = final_buffer[i * config.dim + d];
                final_buffer[i * config.dim + d] = final_buffer[j * config.dim + d];
                final_buffer[j * config.dim + d] = temp;
            }
        }
    }

    for (long i = 0; i < total_points; i++) {
        for (int d = 0; d < config.dim; d++) {
            fprintf(f, "%.6f%s", final_buffer[i * config.dim + d], (d == config.dim - 1) ? "" : " ");
        }
        fprintf(f, "\n");
    }

    free(final_buffer);
    fclose(f);
    return 0;
}
