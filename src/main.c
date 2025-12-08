#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#define IMG_SIZE 10
#define MAX_CLUSTERS 1000
#define ITERATIONS 1000

typedef struct {
    unsigned char data[IMG_SIZE][IMG_SIZE];
} Image;

typedef struct {
    Image center;
    int id;
    int count;
} Cluster;

double calculate_distance(Image *a, Image *b) {
    double sum = 0.0;
    for (int i = 0; i < IMG_SIZE; i++) {
        for (int j = 0; j < IMG_SIZE; j++) {
            double diff = (double)a->data[i][j] - (double)b->data[i][j];
            sum += diff * diff;
        }
    }
    return sqrt(sum);
}

void generate_random_image(Image *img) {
    for (int i = 0; i < IMG_SIZE; i++) {
        for (int j = 0; j < IMG_SIZE; j++) {
            img->data[i][j] = (unsigned char)(rand() % 256);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <distance_threshold>\n", argv[0]);
        return 1;
    }

    double threshold = atof(argv[1]);
    if (threshold < 0) {
        printf("Threshold must be non-negative.\n");
        return 1;
    }

    srand((unsigned int)time(NULL));

    Cluster clusters[MAX_CLUSTERS];
    int cluster_count = 0;

    printf("Running clustering for %d images with threshold %.2f...\n", ITERATIONS, threshold);

    for (int i = 0; i < ITERATIONS; i++) {
        Image current_image;
        generate_random_image(&current_image);

        int best_cluster_index = -1;
        double min_distance = -1.0;

        // Find the closest cluster
        for (int c = 0; c < cluster_count; c++) {
            double dist = calculate_distance(&current_image, &clusters[c].center);
            if (best_cluster_index == -1 || dist < min_distance) {
                min_distance = dist;
                best_cluster_index = c;
            }
        }

        // Check if we can assign to the closest cluster
        if (best_cluster_index != -1 && min_distance < threshold) {
            clusters[best_cluster_index].count++;
        } else {
            // Create new cluster
            if (cluster_count < MAX_CLUSTERS) {
                clusters[cluster_count].center = current_image;
                clusters[cluster_count].id = cluster_count;
                clusters[cluster_count].count = 1;
                cluster_count++;
            }
        }
    }

    printf("Analysis complete.\n");
    printf("Total clusters created: %d\n", cluster_count);
    printf("Average images per cluster: %.2f\n", (double)ITERATIONS / cluster_count);

    return 0;
}
