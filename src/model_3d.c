#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>

#define MAX_CLUSTERS 2000

typedef struct {
    double x, y, z;
} Point3D;

double dist3d(Point3D p1, Point3D p2) {
    return sqrt(pow(p1.x - p2.x, 2) + pow(p1.y - p2.y, 2) + pow(p1.z - p2.z, 2));
}

double rand_double() {
    return (double)rand() / (double)RAND_MAX;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <dcc_file> [output_file]\n", argv[0]);
        return 1;
    }

    char *input_file = argv[1];
    char *output_file = (argc >= 3) ? argv[2] : "clusters_3d_model.txt";

    // Load distance matrix
    // Format: i j dist
    // We need to know N. We can scan file to find max ID.
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
    Point3D *P = (Point3D *)malloc(num_clusters * sizeof(Point3D));
    srand(time(NULL));
    for (int i=0; i<num_clusters; i++) {
        P[i].x = (rand_double() - 0.5) * 20.0; // Init range +/- 10
        P[i].y = (rand_double() - 0.5) * 20.0;
        P[i].z = (rand_double() - 0.5) * 20.0;
    }

    // Simulated Annealing
    double T = 10.0;
    double cooling_rate = 0.995;
    int iterations = 100000;

    // Calculate initial energy
    double E = 0.0;
    int pair_count = 0;
    for (int i=0; i<num_clusters; i++) {
        for (int j=i+1; j<num_clusters; j++) {
            double target = D[i*num_clusters + j];
            if (target >= 0.0) {
                double curr = dist3d(P[i], P[j]);
                E += pow(curr - target, 2);
                pair_count++;
            }
        }
    }

    if (pair_count == 0) {
        fprintf(stderr, "No pairs to optimize\n");
        free(D); free(P);
        return 0;
    }

    printf("Initial Energy: %.6f\n", E);

    for (int k=0; k<iterations; k++) {
        // Perturb random point
        int idx = rand() % num_clusters;
        Point3D old_p = P[idx];

        Point3D new_p = old_p;
        new_p.x += (rand_double() - 0.5) * T;
        new_p.y += (rand_double() - 0.5) * T;
        new_p.z += (rand_double() - 0.5) * T;

        // Calculate delta E (only for pairs involving idx)
        double dE = 0.0;
        for (int j=0; j<num_clusters; j++) {
            if (idx == j) continue;
            double target = D[idx*num_clusters + j];
            if (target >= 0.0) {
                double old_dist = dist3d(old_p, P[j]);
                double new_dist = dist3d(new_p, P[j]);
                dE += pow(new_dist - target, 2) - pow(old_dist - target, 2);
            }
        }

        // Accept?
        if (dE < 0 || exp(-dE / T) > rand_double()) {
            P[idx] = new_p;
            E += dE;
        }

        T *= cooling_rate;
        if (T < 1e-5) break;
    }

    printf("Final Energy: %.6f\n", E);

    // Output
    FILE *fout = fopen(output_file, "w");
    if (!fout) {
        perror("Error opening output file");
        free(D); free(P);
        return 1;
    }

    fprintf(fout, "# ID X Y Z\n");
    for (int i=0; i<num_clusters; i++) {
        fprintf(fout, "%d %.6f %.6f %.6f\n", i, P[i].x, P[i].y, P[i].z);
    }
    fclose(fout);
    printf("Saved 3D model to %s\n", output_file);

    free(D);
    free(P);
    return 0;
}
