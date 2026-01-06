#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <sys/stat.h>
#include "cluster_defs.h"

// Forward decl
void write_png(const char *filename, int width, int height, double *data);

void write_png_frame(const char *filename, double *data, int width, int height) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("Failed to open PNG output file");
        return;
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) { fclose(fp); return; }

    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_write_struct(&png, NULL); fclose(fp); return; }

    if (setjmp(png_jmpbuf(png))) { png_destroy_write_struct(&png, &info); fclose(fp); return; }

    png_init_io(png, fp);

    // Assuming RGB24 if width is 3x or Gray if 1x?
    // Based on frameread.c: width = w*3 if video.
    // If width % 3 == 0, is it RGB? Not necessarily (could be wide gray).
    // Let's assume Grayscale for simplicity unless we detect RGB structure.
    // Wait, if input was MP4 RGB, we flattened it.
    // But how to reconstruct?
    // We treat width as the image width (in pixels).
    // But data size is width * height.
    // If the input was RGB, frame_width = actual_width * 3.
    // So we can check.

    // For now, let's treat as Grayscale image of size (width x height)
    // where 'width' is the number of double elements per row.
    // This will produce a wide grayscale image for RGB data, which is acceptable for visual inspection of "vectors".

    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_GRAY,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    png_bytep row_pointers[height];
    unsigned char *row_data = (unsigned char*)malloc(width);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            double val = data[y * width + x];
            // Normalize? Assume 0-255?
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            row_data[x] = (unsigned char)val;
        }
        row_pointers[y] = row_data;
        png_write_row(png, row_pointers[y]);
    }

    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    free(row_data);
    fclose(fp);
}
