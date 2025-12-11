#include <stdlib.h>
#include <math.h>
#include "image.h"

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
