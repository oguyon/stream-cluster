#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fitsio.h>

int main(int argc, char *argv[]) {
    fitsfile *fptr;
    int status = 0;
    long naxes[3] = {10, 10, 100}; // 100 frames for progress testing
    long nelements = naxes[0] * naxes[1];
    char *filename = "test_cube.fits";

    // Delete if exists
    remove(filename);

    if (fits_create_file(&fptr, filename, &status)) {
        fits_report_error(stderr, status);
        return 1;
    }

    if (fits_create_img(fptr, DOUBLE_IMG, 3, naxes, &status)) {
        fits_report_error(stderr, status);
        return 1;
    }

    double *buffer = (double *)malloc(nelements * sizeof(double));
    for(int i=0; i<nelements; i++) buffer[i] = 0.0;

    // Write 100 frames
    for(int f=0; f<100; f++) {
        long fpixel[3] = {1, 1, f+1};
        // Vary content slightly to create clusters
        for(int i=0; i<nelements; i++) buffer[i] = (double)(f / 20); // 5 clusters
        fits_write_pix(fptr, TDOUBLE, fpixel, nelements, buffer, &status);
    }

    free(buffer);
    fits_close_file(fptr, &status);

    printf("Created %s with 100 frames\n", filename);
    return 0;
}
