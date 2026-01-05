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
    GEN_SPIRAL,
    GEN_SPHERE
} GenType;

typedef struct {
    GenType type;
    int dim;      // 2 or 3
    double param; // generic parameter (period, step, loops)
} GeneratorConfig;

// Random double in [0, 1]
double rand_double() {
    return (double)rand() / (double)RAND_MAX;
}

// Generate random point in unit circle/sphere
void gen_random(double *x, double *y, double *z, int dim) {
    if (dim == 3) {
        double r = cbrt(rand_double());
        double costheta = 1.0 - 2.0 * rand_double();
        double phi = 2.0 * M_PI * rand_double();
        double sintheta = sqrt(1.0 - costheta * costheta);
        *x = r * sintheta * cos(phi);
        *y = r * sintheta * sin(phi);
        *z = r * costheta;
    } else {
        double r = sqrt(rand_double());
        double theta = 2.0 * M_PI * rand_double();
        *x = r * cos(theta);
        *y = r * sin(theta);
        *z = 0.0;
    }
}

// Generate random point ON unit circle/sphere
void gen_sphere(double *x, double *y, double *z, int dim) {
    if (dim == 3) {
        double costheta = 1.0 - 2.0 * rand_double();
        double phi = 2.0 * M_PI * rand_double();
        double sintheta = sqrt(1.0 - costheta * costheta);
        *x = sintheta * cos(phi);
        *y = sintheta * sin(phi);
        *z = costheta;
    } else {
        // 2D sphere is just a circle edge
        double theta = 2.0 * M_PI * rand_double();
        *x = cos(theta);
        *y = sin(theta);
        *z = 0.0;
    }
}

// Generate circle point (2D path)
void gen_circle(double *x, double *y, double *z, long index, double period, int dim) {
    if (period <= 0.0) period = 1.0;
    double theta = 2.0 * M_PI * index / period;
    *x = cos(theta);
    *y = sin(theta);
    *z = 0.0;
    if (dim == 3) {
        // For 3D circle, maybe a helix or just a circle in xy plane?
        // "3Dcircle" usually implies a circle in 3D. Let's keep z=0 for now or implement helix if requested.
        // Prompt asked for "3Dsphere" as comparison. Assuming 2Dcircle means the 2D pattern.
        // If 3D is requested for circle, maybe we map it?
        // Let's keep z=0.
    }
}

// Generate random walk point
void gen_walk(double *x, double *y, double *z, double step_size, int dim) {
    int attempts = 0;
    double nx, ny, nz;
    do {
        if (dim == 3) {
            // Random direction in 3D
            double costheta = 1.0 - 2.0 * rand_double();
            double phi = 2.0 * M_PI * rand_double();
            double sintheta = sqrt(1.0 - costheta * costheta);
            double dx = step_size * sintheta * cos(phi);
            double dy = step_size * sintheta * sin(phi);
            double dz = step_size * costheta;

            nx = *x + dx;
            ny = *y + dy;
            nz = *z + dz;

            if (nx*nx + ny*ny + nz*nz > 1.0) {
                attempts++;
                if (attempts > 100) { nx=*x; ny=*y; nz=*z; break; }
            } else {
                break;
            }
        } else {
            double angle = 2.0 * M_PI * rand_double();
            nx = *x + step_size * cos(angle);
            ny = *y + step_size * sin(angle);
            nz = 0.0;

            if (nx*nx + ny*ny > 1.0) {
                attempts++;
                if (attempts > 100) { nx=*x; ny=*y; break; }
            } else {
                break;
            }
        }
    } while (1);

    *x = nx;
    *y = ny;
    *z = nz;
}

// Generate spiral
void gen_spiral(double *x, double *y, double *z, long index, long total_points, double loops, int dim) {
    double t = (double)index / (double)total_points;
    if (dim == 3) {
        // 3D Spiral (Helix on sphere or cylinder?)
        // Let's do a spherical spiral
        double r = t;
        double theta = 2.0 * M_PI * loops * t; // Azimuthal
        double phi = M_PI * t; // Polar angle 0 to PI

        // Mapping to sphere surface spiral? Or volume?
        // Standard "spiral" in 2D was r=t.
        // Let's do cylindrical helix z=t, r=1? Or conical r=t, z=t?
        // User didn't specify. Let's do a conical spiral.
        *x = t * cos(2.0 * M_PI * loops * t);
        *y = t * sin(2.0 * M_PI * loops * t);
        *z = 2.0 * t - 1.0; // z from -1 to 1
    } else {
        double r = t;
        double theta = 2.0 * M_PI * loops * t;
        *x = r * cos(theta);
        *y = r * sin(theta);
        *z = 0.0;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <N> <filename> [pattern] [options]\n", argv[0]);
        printf("Patterns:\n");
        printf("  [2D|3D]random   Uniform random in unit circle/sphere (default 2D)\n");
        printf("  [2D]circle[P]   Circle radius 1. P = period (default N)\n");
        printf("  [2D|3D]walk[S]  Random walk in unit circle/sphere. S = step size (default 0.1)\n");
        printf("  [2D|3D]spiral[L] Spiral. L = loops (default 3)\n");
        printf("  3Dsphere        Random points on unit sphere surface\n");
        printf("Options:\n");
        printf("  -repeat <M>     Repeat the pattern M times\n");
        printf("  -noise <R>      Add random noise with radius R to each point\n");
        return 1;
    }

    long n_points = atol(argv[1]);
    char *filename = argv[2];
    char *pattern_str = "2Drandom";

    long repeats = 1;
    double noise_radius = 0.0;

    // Parse arguments
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-repeat") == 0) {
            if (i + 1 < argc) {
                repeats = atol(argv[++i]);
            }
        } else if (strcmp(argv[i], "-noise") == 0) {
            if (i + 1 < argc) {
                noise_radius = atof(argv[++i]);
            }
        } else {
            pattern_str = argv[i];
        }
    }

    GeneratorConfig config;
    config.type = GEN_RANDOM;
    config.dim = 2;
    config.param = 0.0;

    // Detect Dimension
    char *name_start = pattern_str;
    if (strncmp(pattern_str, "2D", 2) == 0) {
        config.dim = 2;
        name_start += 2;
    } else if (strncmp(pattern_str, "3D", 2) == 0) {
        config.dim = 3;
        name_start += 2;
    }

    // Parse pattern name
    if (strncmp(name_start, "random", 6) == 0) {
        config.type = GEN_RANDOM;
    } else if (strncmp(name_start, "circle", 6) == 0) {
        config.type = GEN_CIRCLE;
        char *p = name_start + 6;
        if (*p) config.param = atof(p);
        else config.param = (double)n_points;
    } else if (strncmp(name_start, "walk", 4) == 0) {
        config.type = GEN_WALK;
        char *p = name_start + 4;
        if (*p) config.param = atof(p);
        else config.param = 0.1;
    } else if (strncmp(name_start, "spiral", 6) == 0) {
        config.type = GEN_SPIRAL;
        char *p = name_start + 6;
        if (*p) config.param = atof(p);
        else config.param = 3.0;
    } else if (strncmp(name_start, "sphere", 6) == 0) {
        config.type = GEN_SPHERE;
    } else {
        // Fallback for aliases or legacy
        if (strncmp(pattern_str, "circle", 6) == 0) {
             config.dim = 2;
             config.type = GEN_CIRCLE;
             char *p = pattern_str + 6;
             if (*p) config.param = atof(p);
             else config.param = (double)n_points;
        } else if (strncmp(pattern_str, "walk", 4) == 0) {
             config.dim = 2;
             config.type = GEN_WALK;
             char *p = pattern_str + 4;
             if (*p) config.param = atof(p);
             else config.param = 0.1;
        } else if (strncmp(pattern_str, "spiral", 6) == 0) {
             config.dim = 2;
             config.type = GEN_SPIRAL;
             char *p = pattern_str + 6;
             if (*p) config.param = atof(p);
             else config.param = 3.0;
        }
    }

    FILE *f = fopen(filename, "w");
    if (!f) {
        perror("Failed to open output file");
        return 1;
    }

    srand(time(NULL));

    for (long r = 0; r < repeats; r++) {
        double x = 0.0, y = 0.0, z = 0.0; // Reset state for walk each repeat

        for (long i = 0; i < n_points; i++) {
            double out_x, out_y, out_z;

            switch (config.type) {
                case GEN_CIRCLE:
                    gen_circle(&out_x, &out_y, &out_z, i, config.param, config.dim);
                    break;
                case GEN_WALK:
                    gen_walk(&x, &y, &z, config.param, config.dim); // Updates state
                    out_x = x;
                    out_y = y;
                    out_z = z;
                    break;
                case GEN_SPIRAL:
                    gen_spiral(&out_x, &out_y, &out_z, i, n_points, config.param, config.dim);
                    break;
                case GEN_SPHERE:
                    gen_sphere(&out_x, &out_y, &out_z, config.dim);
                    break;
                case GEN_RANDOM:
                default:
                    gen_random(&out_x, &out_y, &out_z, config.dim);
                    break;
            }

            if (noise_radius > 0.0) {
                double nx, ny, nz;
                gen_random(&nx, &ny, &nz, config.dim);
                // Scale unit random vector by noise_radius
                // gen_random returns point inside unit sphere/circle (radius <= 1)
                // So nx, ny, nz are within unit radius.
                // We multiply by noise_radius.
                out_x += nx * noise_radius;
                out_y += ny * noise_radius;
                out_z += nz * noise_radius;
            }

            if (config.dim == 3) {
                fprintf(f, "%f %f %f\n", out_x, out_y, out_z);
            } else {
                fprintf(f, "%f %f\n", out_x, out_y);
            }
        }
    }

    fclose(f);
    return 0;
}
