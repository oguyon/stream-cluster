#include "common.h"
#include <fitsio.h>
#include <stdio.h>
#include <stdlib.h>

static fitsfile *fptr = NULL;
static long num_frames = 0;
static long frame_width = 0;
static long frame_height = 0;
static int current_frame_idx = 0;

Frame* getframe_at(long index);

int init_frameread(char *filename) {
    int status = 0;
    if (fits_open_file(&fptr, filename, READONLY, &status)) {
        fits_report_error(stderr, status);
        return -1;
    }

    int naxis;
    long naxes[3];
    if (fits_get_img_dim(fptr, &naxis, &status) || fits_get_img_size(fptr, 3, naxes, &status)) {
        fits_report_error(stderr, status);
        return -1;
    }

    if (naxis == 3) {
        frame_width = naxes[0];
        frame_height = naxes[1];
        num_frames = naxes[2];
    } else if (naxis == 2) {
        // Single frame or something else?
        // Assuming 3D cube as per instructions for multiple frames
        frame_width = naxes[0];
        frame_height = naxes[1];
        num_frames = 1;
    } else {
        fprintf(stderr, "Error: Input FITS must be 2D or 3D.\n");
        return -1;
    }

    current_frame_idx = 0;
    return 0;
}

Frame* getframe() {
    if (current_frame_idx >= num_frames) {
        return NULL;
    }

    return getframe_at(current_frame_idx++);
}

Frame* getframe_at(long index) {
     if (index >= num_frames || index < 0) {
        return NULL;
    }

    int status = 0;
    long fpixel[3] = {1, 1, index + 1};
    long nelements = frame_width * frame_height;

    Frame *frame = (Frame *)malloc(sizeof(Frame));
    if (!frame) return NULL;

    frame->width = frame_width;
    frame->height = frame_height;
    frame->id = index;
    frame->data = (double *)malloc(nelements * sizeof(double));

    if (!frame->data) {
        free(frame);
        return NULL;
    }

    if (fits_read_pix(fptr, TDOUBLE, fpixel, nelements, NULL, frame->data, NULL, &status)) {
        fits_report_error(stderr, status);
        free(frame->data);
        free(frame);
        return NULL;
    }

    return frame;
}

void free_frame(Frame *frame) {
    if (frame) {
        if (frame->data) free(frame->data);
        free(frame);
    }
}

void close_frameread() {
    int status = 0;
    if (fptr) {
        fits_close_file(fptr, &status);
        fptr = NULL;
    }
}

long get_num_frames() {
    return num_frames;
}

long get_frame_width() {
    return frame_width;
}

long get_frame_height() {
    return frame_height;
}
