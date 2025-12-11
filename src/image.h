#ifndef IMAGE_H
#define IMAGE_H

#define IMG_SIZE 10

typedef struct {
    unsigned char data[IMG_SIZE][IMG_SIZE];
} Image;

double calculate_distance(Image *a, Image *b);
void generate_random_image(Image *img);

#endif
