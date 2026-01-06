#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>

#define MAX_CLUSTERS 2000

typedef struct {
    double *coords;
    int dim;
} PointND;

double dist_nd(PointND p1, PointND p2) {
    double sum = 0.0;
    for (int k = 0; k < p1.dim; k++) {
        double d = p1.coords[k] - p2.coords[k];
        sum += d * d;
    }
    return sqrt(sum);
}

double rand_double() {
    return (double)rand() / (double)RAND_MAX;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <dcc_file> <dimensions> <output_file>\n", argv[0]);
        return 1;
    }

    char *input_file = argv[1];
    int dimensions = atoi(argv[2]);
    char *output_file = argv[3];

    if (dimensions < 1) {
        fprintf(stderr, "Invalid dimensions: %d\n", dimensions);
        return 1;
    }

    // Load distance matrix
    FILE *fin = fopen(input_file, "r");
    if (!fin) {
        perror("Error opening dcc file");
        return 1;
    }

    int max_id = -1;
    char line[1024];
    while (fgets(line, sizeof(line), fin)) {
        int i, j;
        double d;
        if (sscanf(line, "%d %d %lf", &i, &j, &d) == 3) {
            if (i > max_id) max_id = i;
            if (j > max_id) max_id = j;
        }
    }

    int num_clusters = max_id + 1;
    if (num_clusters <= 0) {
        fprintf(stderr, "No valid data in dcc file\n");
        fclose(fin);
        return 1;
    }
    if (num_clusters > MAX_CLUSTERS) {
        fprintf(stderr, "Too many clusters (%d), max is %d\n", num_clusters, MAX_CLUSTERS);
        fclose(fin);
        return 1;
    }

    // Allocate matrix
    double *D = (double *)malloc(num_clusters * num_clusters * sizeof(double));
    for (int i=0; i<num_clusters*num_clusters; i++) D[i] = -1.0;

    rewind(fin);
    while (fgets(line, sizeof(line), fin)) {
        int i, j;
        double d;
        if (sscanf(line, "%d %d %lf", &i, &j, &d) == 3) {
            D[i * num_clusters + j] = d;
            D[j * num_clusters + i] = d;
        }
    }
    fclose(fin);

    // Initialize random positions
    PointND *P = (PointND *)malloc(num_clusters * sizeof(PointND));
    srand(time(NULL));
    for (int i=0; i<num_clusters; i++) {
        P[i].dim = dimensions;
        P[i].coords = (double *)malloc(dimensions * sizeof(double));
        for (int k=0; k<dimensions; k++) {
            P[i].coords[k] = (rand_double() - 0.5) * 20.0;
        }
    }

    // Simulated Annealing
    double T = 10.0;
    double cooling_rate = 0.995;
    int iterations = 100000;

    double E = 0.0;
    int pair_count = 0;
    for (int i=0; i<num_clusters; i++) {
        for (int j=i+1; j<num_clusters; j++) {
            double target = D[i*num_clusters + j];
            if (target >= 0.0) {
                double curr = dist_nd(P[i], P[j]);
                E += pow(curr - target, 2);
                pair_count++;
            }
        }
    }

    if (pair_count == 0) {
        fprintf(stderr, "No pairs to optimize\n");
        // Cleanup
        free(D);
        for(int i=0; i<num_clusters; i++) free(P[i].coords);
        free(P);
        return 0;
    }

    printf("Initial Energy: %.6f\n", E);

    // Temporary point for perturbations
    PointND new_p;
    new_p.dim = dimensions;
    new_p.coords = (double *)malloc(dimensions * sizeof(double));

    for (int k=0; k<iterations; k++) {
        int idx = rand() % num_clusters;

        // Copy current to new
        memcpy(new_p.coords, P[idx].coords, dimensions * sizeof(double));

        // Perturb
        for (int d=0; d<dimensions; d++) {
            new_p.coords[d] += (rand_double() - 0.5) * T;
        }

        // Calculate delta E
        double dE = 0.0;
        for (int j=0; j<num_clusters; j++) {
            if (idx == j) continue;
            double target = D[idx*num_clusters + j];
            if (target >= 0.0) {
                double old_dist = dist_nd(P[idx], P[j]);
                double new_dist = dist_nd(new_p, P[j]);
                dE += pow(new_dist - target, 2) - pow(old_dist - target, 2);
            }
        }

        // Accept?
        if (dE < 0 || exp(-dE / T) > rand_double()) {
            memcpy(P[idx].coords, new_p.coords, dimensions * sizeof(double));
            E += dE;
        }

        T *= cooling_rate;
        if (T < 1e-5) break;
    }

    printf("Final Energy: %.6f\n", E);

    FILE *fout = fopen(output_file, "w");
    if (!fout) {
        perror("Error opening output file");
    } else {
        fprintf(fout, "# ID");
        for(int d=0; d<dimensions; d++) fprintf(fout, " Dim%d", d);
        fprintf(fout, "\n");

        for (int i=0; i<num_clusters; i++) {
            fprintf(fout, "%d", i);
            for(int d=0; d<dimensions; d++) fprintf(fout, " %.6f", P[i].coords[d]);
            fprintf(fout, "\n");
        }
        fclose(fout);
        printf("Saved ND model to %s\n", output_file);
    }

    free(new_p.coords);
    free(D);
    for(int i=0; i<num_clusters; i++) free(P[i].coords);
    free(P);

    return 0;
}
