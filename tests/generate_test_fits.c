#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fitsio.h>

int main(int argc, char *argv[]) {
    fitsfile *fptr;
    int status = 0;
    long naxes[3] = {10, 10, 5};
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

    // Frame 0: All 0
    for(int i=0; i<nelements; i++) buffer[i] = 0.0;
    long fpixel[3] = {1, 1, 1};
    fits_write_pix(fptr, TDOUBLE, fpixel, nelements, buffer, &status);

    // Frame 1: All 1
    for(int i=0; i<nelements; i++) buffer[i] = 1.0;
    fpixel[2] = 2;
    fits_write_pix(fptr, TDOUBLE, fpixel, nelements, buffer, &status);

    // Frame 2: All 2
    for(int i=0; i<nelements; i++) buffer[i] = 2.0;
    fpixel[2] = 3;
    fits_write_pix(fptr, TDOUBLE, fpixel, nelements, buffer, &status);

    // Frame 3: All 4
    for(int i=0; i<nelements; i++) buffer[i] = 4.0;
    fpixel[2] = 4;
    fits_write_pix(fptr, TDOUBLE, fpixel, nelements, buffer, &status);

    // Frame 4: All 7
    for(int i=0; i<nelements; i++) buffer[i] = 7.0;
    fpixel[2] = 5;
    fits_write_pix(fptr, TDOUBLE, fpixel, nelements, buffer, &status);

    free(buffer);
    fits_close_file(fptr, &status);

    printf("Created %s\n", filename);
    return 0;
}
