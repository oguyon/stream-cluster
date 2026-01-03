#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fitsio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>

#include "common.h"

#define ANSI_COLOR_ORANGE  "\x1b[38;5;208m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_RESET   "\x1b[0m"

// Prototype declarations
double framedist(Frame *a, Frame *b);
int init_frameread(char *filename);
Frame* getframe();
Frame* getframe_at(long index);
void free_frame(Frame *frame);
void close_frameread();
void reset_frameread();
long get_num_frames();
long get_frame_width();
long get_frame_height();

// Global variables for algorithm
double rlim = 0.0;
int auto_rlim_mode = 0;
double auto_rlim_factor = 0.0;
double deltaprob = 0.01;
int maxnbclust = 1000;
long maxnbfr = 100000;
char *fits_filename = NULL;
char *user_outdir = NULL;
int scandist_mode = 0;
int progress_mode = 0;
int average_mode = 0;
volatile sig_atomic_t stop_requested = 0;

Cluster *clusters;
double *dccarray; // 1D array simulating 2D: [i*maxNcl + j]
int *probsortedclindex;
int *clmembflag;
int num_clusters = 0;
long framedist_calls = 0;

// Assignments for output
int *assignments = NULL;
long total_frames_processed = 0;

// Wrapper for framedist to count calls
double get_dist(Frame *a, Frame *b) {
    framedist_calls++;
    return framedist(a, b);
}

// Comparison function for qsort
int compare_probs(const void *a, const void *b) {
    int idx_a = *(const int *)a;
    int idx_b = *(const int *)b;
    if (clusters[idx_a].prob > clusters[idx_b].prob) return -1;
    if (clusters[idx_a].prob < clusters[idx_b].prob) return 1;
    return 0;
}

int compare_doubles(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

void handle_sigint(int sig) {
    stop_requested = 1;
}

// Helper to create output directory name based on input filename
char* create_output_dir_name(const char* input_file) {
    // Extract filename from path
    const char *base = strrchr(input_file, '/');
    if (base) {
        base++; // Skip slash
    } else {
        base = input_file;
    }

    // Duplicate base so we can modify it
    char *name = strdup(base);
    if (!name) return NULL;

    // Check for extensions to remove
    // "filename.fits" -> "filename"
    // "filename.fits.fz" -> "filename"

    // Check for .fits.fz first
    size_t len = strlen(name);
    if (len > 8 && strcmp(name + len - 8, ".fits.fz") == 0) {
        name[len - 8] = '\0';
    } else if (len > 5 && strcmp(name + len - 5, ".fits") == 0) {
        name[len - 5] = '\0';
    }

    // Create final string "basename.clusterdat"
    size_t new_len = strlen(name) + strlen(".clusterdat") + 1;
    char *out_dir = (char *)malloc(new_len);
    if (out_dir) {
        sprintf(out_dir, "%s.clusterdat", name);
    }

    free(name);
    return out_dir;
}

void print_usage(char *progname) {
    printf("Usage: %s <rlim> [options] <fits_file>\n", progname);
    printf("       %s -scandist <fits_file>\n", progname);
    printf("Arguments:\n");
    printf("  <rlim>         Clustering radius limit. Can be:\n");
    printf("                 - A float value (e.g., 10.5)\n");
    printf("                 - Format 'a<val>' (e.g., a1.5) to set rlim = val * median_distance\n");
    printf("Options:\n");
    printf("  -dprob <val>   Delta probability (default 0.01)\n");
    printf("  -maxcl <val>   Max number of clusters (default 1000)\n");
    printf("  -maxim <val>   Max number of frames (default 100000)\n");
    printf("  -avg           Compute average frame per cluster\n");
    printf("  -outdir <name> Specify output directory name\n");
    printf("  -progress      Print real-time progress updates\n");
    printf("  -scandist      Measure distance between consecutive frames\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    // First pass: Detect -scandist to set mode
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-scandist") == 0) {
            scandist_mode = 1;
            break;
        }
    }

    int arg_idx = 1;

    if (!scandist_mode) {
        // Standard Mode: First argument must be rlim or auto-rlim format "a%f"
        if (argv[1][0] == 'a') {
            // Auto rlim mode
            char *endptr;
            auto_rlim_factor = strtod(argv[1] + 1, &endptr);
            if (*endptr != '\0') {
                 fprintf(stderr, "Error: Invalid format for auto-rlim. Expected 'a<float>', got '%s'\n", argv[1]);
                 return 1;
            }
            auto_rlim_mode = 1;
            arg_idx = 2;
        } else if (argv[1][0] == '-') {
            fprintf(stderr, "Error: First argument must be rlim (numerical value) or auto-rlim (a<val>), found option: %s\n", argv[1]);
            print_usage(argv[0]);
            return 1;
        } else {
            char *endptr;
            rlim = strtod(argv[1], &endptr);
            if (*endptr != '\0') {
                 fprintf(stderr, "Error: Invalid rlim value: %s\n", argv[1]);
                 return 1;
            }
            arg_idx = 2;
        }
    } else {
        // Scandist Mode: No fixed positional arg required at start.
        arg_idx = 1;
    }

    // Second pass: Parse options and FITS file
    while (arg_idx < argc) {
        if (strcmp(argv[arg_idx], "-dprob") == 0) {
            if (arg_idx + 1 >= argc) {
                fprintf(stderr, "Error: Missing value for option -dprob\n");
                return 1;
            }
            deltaprob = atof(argv[++arg_idx]);
        } else if (strcmp(argv[arg_idx], "-maxcl") == 0) {
            if (arg_idx + 1 >= argc) {
                fprintf(stderr, "Error: Missing value for option -maxcl\n");
                return 1;
            }
            maxnbclust = atoi(argv[++arg_idx]);
        } else if (strcmp(argv[arg_idx], "-maxim") == 0) {
            if (arg_idx + 1 >= argc) {
                fprintf(stderr, "Error: Missing value for option -maxim\n");
                return 1;
            }
            maxnbfr = atol(argv[++arg_idx]);
        } else if (strcmp(argv[arg_idx], "-avg") == 0) {
            average_mode = 1;
        } else if (strcmp(argv[arg_idx], "-outdir") == 0) {
            if (arg_idx + 1 >= argc) {
                fprintf(stderr, "Error: Missing value for option -outdir\n");
                return 1;
            }
            user_outdir = argv[++arg_idx];
        } else if (strcmp(argv[arg_idx], "-progress") == 0) {
            progress_mode = 1;
        } else if (strcmp(argv[arg_idx], "-scandist") == 0) {
            // Already handled in first pass, just consume
        } else if (argv[arg_idx][0] == '-') {
            fprintf(stderr, "Error: Unknown option: %s\n", argv[arg_idx]);
            print_usage(argv[0]);
            return 1;
        } else {
            // Positional argument: FITS file
            if (fits_filename != NULL) {
                fprintf(stderr, "Error: Too many arguments or multiple FITS files specified (already have '%s', found '%s')\n", fits_filename, argv[arg_idx]);
                return 1;
            }
            fits_filename = argv[arg_idx];
        }
        arg_idx++;
    }

    if (!fits_filename) {
        fprintf(stderr, "Error: Missing input FITS file.\n");
        if (!scandist_mode) print_usage(argv[0]);
        return 1;
    }

    if (init_frameread(fits_filename) != 0) {
        return 1;
    }

    if (!scandist_mode) {
        signal(SIGINT, handle_sigint);
        printf("CTRL+C to stop clustering and write results\n");
    }

    if (scandist_mode || auto_rlim_mode) {
        long nframes = get_num_frames();
        if (nframes < 2) {
            printf("Not enough frames to calculate distances.\n");
            close_frameread();
            return 0;
        }

        // We can limit frames by maxnbfr if needed
        long process_limit = (nframes > maxnbfr) ? maxnbfr : nframes;

        // Array to store distances. n frames have n-1 intervals.
        double *distances = (double *)malloc((process_limit - 1) * sizeof(double));
        if (!distances) {
            perror("Memory allocation failed");
            close_frameread();
            return 1;
        }

        Frame *prev = getframe();
        if (!prev) {
             free(distances);
             close_frameread();
             return 1;
        }

        long count = 0;
        // Loop from 1 to process_limit-1
        for (long i = 1; i < process_limit; i++) {
            Frame *curr = getframe();
            if (!curr) break;

            distances[count++] = get_dist(prev, curr);

            free_frame(prev);
            prev = curr;
        }
        free_frame(prev); // Free the last one

        if (count > 0) {
            qsort(distances, count, sizeof(double), compare_doubles);

            double min_val = distances[0];
            double max_val = distances[count - 1];
            double median_val;
            double p20_val;
            double p80_val;

            // Median
            if (count % 2 == 1) {
                median_val = distances[count / 2];
            } else {
                median_val = (distances[count / 2 - 1] + distances[count / 2]) / 2.0;
            }

            // Percentile 20
            double p20_idx = (count - 1) * 0.2;
            int p20_i = (int)p20_idx;
            double p20_f = p20_idx - p20_i;
            if (p20_i + 1 < count)
                 p20_val = distances[p20_i] * (1.0 - p20_f) + distances[p20_i + 1] * p20_f;
            else
                 p20_val = distances[p20_i];

            // Percentile 80
            double p80_idx = (count - 1) * 0.8;
            int p80_i = (int)p80_idx;
            double p80_f = p80_idx - p80_i;
            if (p80_i + 1 < count)
                 p80_val = distances[p80_i] * (1.0 - p80_f) + distances[p80_i + 1] * p80_f;
            else
                 p80_val = distances[p80_i];

            if (scandist_mode) {
                printf("Distance statistics (%ld intervals):\n", count);
                printf("%-10s %.6f\n", "Min:", min_val);
                printf("%-10s %.6f\n", "20%:", p20_val);
                printf("%-10s %.6f\n", "Median:", median_val);
                printf("%-10s %.6f\n", "80%:", p80_val);
                printf("%-10s %.6f\n", "Max:", max_val);

                free(distances);
                close_frameread();
                return 0;
            } else if (auto_rlim_mode) {
                rlim = auto_rlim_factor * median_val;
                printf("Auto-rlim: Median distance = %.6f, Multiplier = %.6f -> rlim = %.6f\n", median_val, auto_rlim_factor, rlim);
            }
        } else {
            printf("No distances calculated.\n");
            if (scandist_mode) {
                free(distances);
                close_frameread();
                return 0;
            }
        }

        free(distances);

        // Reset for clustering
        if (auto_rlim_mode) {
             reset_frameread();
        }
    }

    // Allocate memory
    clusters = (Cluster *)malloc(maxnbclust * sizeof(Cluster));
    dccarray = (double *)malloc(maxnbclust * maxnbclust * sizeof(double));
    for (int i = 0; i < maxnbclust * maxnbclust; i++) dccarray[i] = -1.0;

    probsortedclindex = (int *)malloc(maxnbclust * sizeof(int));
    clmembflag = (int *)malloc(maxnbclust * sizeof(int));

    // Allocate assignments array based on actual frames (or max limit)
    long actual_frames = get_num_frames();
    if (actual_frames > maxnbfr) actual_frames = maxnbfr;
    assignments = (int *)malloc(actual_frames * sizeof(int));

    // Create output directory
    char *out_dir = NULL;
    if (user_outdir) {
        out_dir = strdup(user_outdir);
    } else {
        out_dir = create_output_dir_name(fits_filename);
    }

    if (!out_dir) {
        perror("Memory allocation failed for output directory name");
        return 1;
    }

    struct stat st = {0};
    if (stat(out_dir, &st) == -1) {
        if (mkdir(out_dir, 0777) != 0) {
            perror("Failed to create output directory");
            free(out_dir);
            return 1;
        }
    }

    char out_path[1024];
    snprintf(out_path, sizeof(out_path), "%s/frame_membership.txt", out_dir);

    FILE *ascii_out = fopen(out_path, "w");
    if (!ascii_out) {
        perror("Failed to open frame_membership.txt in output directory");
        free(out_dir);
        return 1;
    }

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    Frame *current_frame;
    while ((current_frame = getframe()) != NULL) {
        if (stop_requested) {
            printf(ANSI_COLOR_ORANGE "\nStopping clustering on user request (CTRL+C).\n" ANSI_COLOR_RESET);
            free_frame(current_frame); // Current frame not processed
            break;
        }

        if (total_frames_processed >= maxnbfr) {
            free_frame(current_frame);
            break;
        }

        int assigned_cluster = -1;

        if (num_clusters == 0) {
            // Step 0
            clusters[0].anchor = *current_frame; // Copy struct content (shallow copy of pointers, but we need deep copy of data?)
            // We need to keep the anchor data. current_frame->data is malloced.
            // We should NOT free current_frame->data if we assign it to cluster anchor.
            // But getframe allocates new memory for each frame.
            // So we can just transfer ownership.

            // Wait, struct assignment `clusters[0].anchor = *current_frame` copies the pointer `data`.
            // So `clusters[0].anchor.data` points to the same memory.
            // We must NOT call free_frame(current_frame) if we use it as anchor.
            // Instead we should just free the struct wrapper, but `free_frame` frees data too.
            // So we just copy.

            clusters[0].id = 0;
            clusters[0].prob = 1.0;
            num_clusters = 1;
            assigned_cluster = 0;

            // Need to set dccarray[0][0] = 0? Or just leave undefined/0.
            // Distance to self is 0.
            dccarray[0] = 0.0;

            // Since we transfer ownership of data to cluster anchor, do not free it.
            // But wait, `current_frame` is a pointer to a malloced struct.
            free(current_frame); // Only free the struct container, not the data?
            // `free_frame` frees data.
            // Let's modify logic: ownership transfer.
            // If we use current_frame as anchor, we set current_frame->data = NULL before freeing.
        } else {
            // Step 1: Normalize probabilities
            double sum_prob = 0.0;
            for (int i = 0; i < num_clusters; i++) sum_prob += clusters[i].prob;
            if (sum_prob > 0) {
                for (int i = 0; i < num_clusters; i++) clusters[i].prob /= sum_prob;
            }

            // Step 2: Update sorted index
            for (int i = 0; i < num_clusters; i++) probsortedclindex[i] = i;
            qsort(probsortedclindex, num_clusters, sizeof(int), compare_probs);

            // Step 3: Initialize k=0, clmembflag=1
            int k = 0;
            for (int i = 0; i < num_clusters; i++) clmembflag[i] = 1;

            int found = 0;
            while (k < num_clusters) {
                 int cj = probsortedclindex[k];

                 // Step 4: Compute distance
                 double dfc = get_dist(current_frame, &clusters[cj].anchor);

                 if (dfc < rlim) {
                     // Allocate to this cluster
                     assigned_cluster = cj;
                     clusters[cj].prob += deltaprob;
                     found = 1;
                     break;
                 }

                 // Step 5: Prune candidates
                 for (int cl = 0; cl < num_clusters; cl++) {
                     if (clmembflag[cl] == 0) continue;

                     // Get dcc(cj, cl)
                     double dcc = dccarray[cj * maxnbclust + cl];
                     if (dcc < 0) {
                        // Should not happen if we update dcc properly
                        dcc = get_dist(&clusters[cj].anchor, &clusters[cl].anchor);
                        dccarray[cj * maxnbclust + cl] = dcc;
                        dccarray[cl * maxnbclust + cj] = dcc;
                     }

                     if (dcc - dfc > rlim) clmembflag[cl] = 0;
                     if (dfc - dcc > rlim) clmembflag[cl] = 0;
                 }

                 // Step 6: Increment k
                 k++;
                 while (k < num_clusters && clmembflag[probsortedclindex[k]] == 0) {
                     k++;
                 }
            }

            if (!found) {
                // Step 7: New cluster
                if (num_clusters < maxnbclust) {
                    assigned_cluster = num_clusters;
                    clusters[num_clusters].anchor = *current_frame; // Transfer ownership
                    clusters[num_clusters].id = num_clusters;
                    clusters[num_clusters].prob = 1.0;

                    // Update dccarray for new cluster
                    for (int i = 0; i < num_clusters; i++) {
                        double d = get_dist(&clusters[num_clusters].anchor, &clusters[i].anchor);
                        dccarray[num_clusters * maxnbclust + i] = d;
                        dccarray[i * maxnbclust + num_clusters] = d;
                    }
                    dccarray[num_clusters * maxnbclust + num_clusters] = 0.0;

                    num_clusters++;
                    free(current_frame); // Structure only
                } else {
                    // Max clusters reached. Stop program?
                    // "Stop program if/when number of clusters reaches this limit"
                    printf(ANSI_COLOR_ORANGE "Max clusters limit reached.\n" ANSI_COLOR_RESET);
                    printf("Frames clustered: %ld\n", total_frames_processed);
                    free_frame(current_frame);
                    break;
                }
            } else {
                // Frame assigned to existing cluster.
                // We don't need to keep the frame data in memory for the algorithm anymore (only for output later).
                // Since we re-read for output, we can free it.
                free_frame(current_frame);
            }
        }

        // Record assignment
        assignments[total_frames_processed] = assigned_cluster;
        fprintf(ascii_out, "%ld %d\n", total_frames_processed, assigned_cluster);

        total_frames_processed++;

        if (progress_mode && (total_frames_processed % 10 == 0 || total_frames_processed == actual_frames)) {
            double avg_dists = (total_frames_processed > 0) ? (double)framedist_calls / total_frames_processed : 0.0;
            printf("\rProcessing frame %ld / %ld (Clusters: %d, Dists: %ld, Avg Dists/Frame: %.1f)",
                   total_frames_processed, actual_frames, num_clusters, framedist_calls, avg_dists);
            fflush(stdout);
        }
    }

    if (progress_mode) printf("\n");

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;

    if (num_clusters < maxnbclust) {
        printf(ANSI_COLOR_GREEN "All frames clustered.\n" ANSI_COLOR_RESET);
    }

    printf("Analysis complete.\n");
    printf("Total clusters: %d\n", num_clusters);
    printf("Processing time: %.3f ms\n", elapsed_ms);
    printf("Framedist calls: %ld\n", framedist_calls);

    fclose(ascii_out);

    // Write Anchors FITS
    int status = 0;
    fitsfile *afptr;
    snprintf(out_path, sizeof(out_path), "!%s/anchors.fits", out_dir);
    fits_create_file(&afptr, out_path, &status);
    long naxes[3] = { get_frame_width(), get_frame_height(), num_clusters };
    fits_create_img(afptr, DOUBLE_IMG, 3, naxes, &status);

    long nelements = get_frame_width() * get_frame_height();
    for (int i = 0; i < num_clusters; i++) {
        long fpixel[3] = {1, 1, i + 1};
        fits_write_pix(afptr, TDOUBLE, fpixel, nelements, clusters[i].anchor.data, &status);
    }
    fits_close_file(afptr, &status);

    // Write Cluster FITS cubes and optionally compute average
    // We need to group frames by cluster to write them effectively
    // To avoid too many file open/close, we iterate over clusters
    printf("Writing cluster FITS files...\n");

    int *cluster_counts = (int *)calloc(num_clusters, sizeof(int));
    for (long i = 0; i < total_frames_processed; i++) {
        if (assignments[i] >= 0 && assignments[i] < num_clusters)
            cluster_counts[assignments[i]]++;
    }

    // Write cluster counts
    snprintf(out_path, sizeof(out_path), "%s/cluster_counts.txt", out_dir);
    FILE *count_out = fopen(out_path, "w");
    if (count_out) {
        for (int c = 0; c < num_clusters; c++) {
            fprintf(count_out, "Cluster %d: %d frames\n", c, cluster_counts[c]);
        }
        fclose(count_out);
    }

    fitsfile *avg_ptr = NULL;
    double *avg_buffer = NULL;
    if (average_mode) {
        snprintf(out_path, sizeof(out_path), "!%s/average.fits", out_dir);
        fits_create_file(&avg_ptr, out_path, &status);
        long anaxes[3] = { get_frame_width(), get_frame_height(), num_clusters };
        fits_create_img(avg_ptr, DOUBLE_IMG, 3, anaxes, &status);
        avg_buffer = (double *)calloc(nelements, sizeof(double));
    }

    for (int c = 0; c < num_clusters; c++) {
        if (cluster_counts[c] == 0) {
             if (average_mode) {
                 // Write zeros if empty cluster (unlikely here)
                 for (long k=0; k<nelements; k++) avg_buffer[k] = 0.0;
                 long fpixel[3] = {1, 1, c + 1};
                 fits_write_pix(avg_ptr, TDOUBLE, fpixel, nelements, avg_buffer, &status);
             }
             continue;
        }

        char fname[1024];
        snprintf(fname, sizeof(fname), "!%s/cluster_%d.fits", out_dir, c);

        fitsfile *cfptr;
        fits_create_file(&cfptr, fname, &status);
        long cnaxes[3] = { get_frame_width(), get_frame_height(), cluster_counts[c] };
        fits_create_img(cfptr, DOUBLE_IMG, 3, cnaxes, &status);

        if (average_mode) {
             for (long k=0; k<nelements; k++) avg_buffer[k] = 0.0;
        }

        // Find frames belonging to this cluster
        int frame_count_in_cluster = 0;
        for (long f = 0; f < total_frames_processed; f++) {
            if (assignments[f] == c) {
                Frame *fr = getframe_at(f);
                if (fr) {
                    long fpixel[3] = {1, 1, frame_count_in_cluster + 1};
                    fits_write_pix(cfptr, TDOUBLE, fpixel, nelements, fr->data, &status);

                    if (average_mode) {
                        for (long k=0; k<nelements; k++) avg_buffer[k] += fr->data[k];
                    }

                    free_frame(fr);
                    frame_count_in_cluster++;
                }
            }
        }
        fits_close_file(cfptr, &status);

        if (average_mode) {
            for (long k=0; k<nelements; k++) avg_buffer[k] /= cluster_counts[c];
            long fpixel[3] = {1, 1, c + 1};
            fits_write_pix(avg_ptr, TDOUBLE, fpixel, nelements, avg_buffer, &status);
        }
    }

    if (average_mode) {
        free(avg_buffer);
        fits_close_file(avg_ptr, &status);
    }

    free(cluster_counts);

    // Cleanup
    for (int i = 0; i < num_clusters; i++) {
        // Free anchor data
        if (clusters[i].anchor.data) free(clusters[i].anchor.data);
    }
    free(clusters);
    free(dccarray);
    free(probsortedclindex);
    free(clmembflag);
    free(assignments);
    free(out_dir);

    close_frameread();

    return 0;
}
