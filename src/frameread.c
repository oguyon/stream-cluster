#include "common.h"
#include <fitsio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static fitsfile *fptr = NULL;
static FILE *ascii_ptr = NULL;
static long *ascii_line_offsets = NULL;
static int is_ascii_mode = 0;

static long num_frames = 0;
static long frame_width = 0;
static long frame_height = 0;
static int current_frame_idx = 0;

Frame* getframe_at(long index);

int is_ascii_input_mode() {
    return is_ascii_mode;
}

static int init_ascii(char *filename) {
    ascii_ptr = fopen(filename, "r");
    if (!ascii_ptr) {
        perror("Failed to open ASCII file");
        return -1;
    }

    is_ascii_mode = 1;
    num_frames = 0;

    // Initial buffer for line offsets
    size_t capacity = 1024;
    ascii_line_offsets = (long *)malloc(capacity * sizeof(long));
    if (!ascii_line_offsets) {
        perror("Memory allocation failed");
        return -1;
    }

    char *line = NULL;
    size_t len = 0;
    long offset = ftell(ascii_ptr);

    int first_line = 1;

    while (getline(&line, &len, ascii_ptr) != -1) {
        // Check if line is empty or just whitespace?
        // For simplicity assuming valid data lines.
        // Trim trailing newline?

        if (num_frames >= capacity) {
            capacity *= 2;
            long *new_offsets = (long *)realloc(ascii_line_offsets, capacity * sizeof(long));
            if (!new_offsets) {
                perror("Memory reallocation failed");
                free(line);
                return -1;
            }
            ascii_line_offsets = new_offsets;
        }
        ascii_line_offsets[num_frames] = offset;
        num_frames++;

        if (first_line) {
            // Count columns
            int cols = 0;
            char *p = line;
            int in_num = 0;
            while (*p) {
                if (!isspace((unsigned char)*p)) {
                    if (!in_num) {
                        cols++;
                        in_num = 1;
                    }
                } else {
                    in_num = 0;
                }
                p++;
            }
            frame_width = cols;
            frame_height = 1; // Treat as 1D vector
            first_line = 0;
        }

        offset = ftell(ascii_ptr);
    }

    free(line);

    if (num_frames == 0) {
        fprintf(stderr, "Error: Empty ASCII file.\n");
        return -1;
    }

    rewind(ascii_ptr);
    return 0;
}

int init_frameread(char *filename) {
    // Check extension
    char *ext = strrchr(filename, '.');
    if (ext && strcmp(ext, ".txt") == 0) {
        return init_ascii(filename);
    }

    // Default to FITS
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

    if (is_ascii_mode) {
        if (fseek(ascii_ptr, ascii_line_offsets[index], SEEK_SET) != 0) {
            perror("fseek failed");
            free(frame->data);
            free(frame);
            return NULL;
        }

        // Read line
        // We know the number of columns (frame_width), so we can just loop fscanf
        for (long i = 0; i < nelements; i++) {
            if (fscanf(ascii_ptr, "%lf", &frame->data[i]) != 1) {
                // If parsing fails mid-line, fill with 0 or handle error
                // For now, simple error handling
                fprintf(stderr, "Error parsing ASCII data at frame %ld, element %ld\n", index, i);
                free(frame->data);
                free(frame);
                return NULL;
            }
        }
    } else {
        int status = 0;
        long fpixel[3] = {1, 1, index + 1};
        if (fits_read_pix(fptr, TDOUBLE, fpixel, nelements, NULL, frame->data, NULL, &status)) {
            fits_report_error(stderr, status);
            free(frame->data);
            free(frame);
            return NULL;
        }
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
    if (is_ascii_mode) {
        if (ascii_ptr) fclose(ascii_ptr);
        if (ascii_line_offsets) free(ascii_line_offsets);
        ascii_ptr = NULL;
        ascii_line_offsets = NULL;
        is_ascii_mode = 0;
    } else {
        int status = 0;
        if (fptr) {
            fits_close_file(fptr, &status);
            fptr = NULL;
        }
    }
}

void reset_frameread() {
    current_frame_idx = 0;
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
