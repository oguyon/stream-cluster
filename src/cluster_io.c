#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fitsio.h>
#include "cluster_io.h"
#include "frameread.h"

#define ANSI_COLOR_ORANGE  "\x1b[38;5;208m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_BG_GREEN      "\x1b[42m"
#define ANSI_COLOR_BLACK   "\x1b[30m"
#define ANSI_COLOR_RESET   "\x1b[0m"

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
    printf("Usage: %s <rlim> [options] <input_file>\n", progname);
    printf("       %s -scandist <input_file>\n", progname);
    printf("Arguments:\n");
    printf("  <rlim>         Clustering radius limit. Can be:\n");
    printf("                 - A float value (e.g., 10.5)\n");
    printf("                 - Format 'a<val>' (e.g., a1.5) to set rlim = val * median_distance\n");
    printf("  <input_file>   Input file. Can be:\n");
    printf("                 - FITS file (2D image or 3D cube)\n");
    printf("                 - ASCII .txt file (one sample per line, columns = dimensions)\n");
    printf("Options:\n");
    printf("  -dprob <val>   Delta probability (default 0.01)\n");
    printf("  -maxcl <val>   Max number of clusters (default 1000)\n");
    printf("  -maxim <val>   Max number of frames (default 100000)\n");
    printf("  -avg           Compute average frame per cluster\n");
    printf("  -distall       Save all computed distances to distall.txt\n");
    printf("  -outdir <name> Specify output directory name\n");
    printf("  -progress      Print real-time progress updates\n");
    printf("  -scandist      Measure distance between consecutive frames\n");
    printf("  -gprob         Use geometrical probability for cluster ranking\n");
    printf("  -fmatcha <val> Set parameter 'a' for fmatch (default 2.0)\n");
    printf("  -fmatchb <val> Set parameter 'b' for fmatch (default 0.5)\n");
    printf("  -verbose       Enable verbose output\n");
    printf("  -veryverbose   Enable very verbose output (includes fmatch/gprob details)\n");
    printf("  -fitsout       Force FITS output format even for ASCII input\n");
}

void write_results(ClusterConfig *config, ClusterState *state) {
    char *out_dir = NULL;
    if (config->user_outdir) {
        out_dir = strdup(config->user_outdir);
    } else {
        out_dir = create_output_dir_name(config->fits_filename);
    }

    if (!out_dir) return; // Should have been created earlier

    char out_path[1024];

    // Write Cluster-Cluster Distances (dcc)
    snprintf(out_path, sizeof(out_path), "%s/dcc.txt", out_dir);
    FILE *dcc_out = fopen(out_path, "w");
    if (dcc_out) {
        for (int i = 0; i < state->num_clusters; i++) {
            for (int j = 0; j < state->num_clusters; j++) {
                double d = state->dccarray[i * config->maxnbclust + j];
                if (d >= 0) {
                    fprintf(dcc_out, "%d %d %.6f\n", i, j, d);
                }
            }
        }
        fclose(dcc_out);
    }

    // Write Anchors
    long nelements = get_frame_width() * get_frame_height();
    int status = 0;

    if (is_ascii_input_mode() && !config->fitsout_mode) {
        // Write ASCII anchors
        snprintf(out_path, sizeof(out_path), "%s/anchors.txt", out_dir);
        FILE *afptr = fopen(out_path, "w");
        if (afptr) {
            for (int i = 0; i < state->num_clusters; i++) {
                for (long k = 0; k < nelements; k++) {
                    fprintf(afptr, "%f ", state->clusters[i].anchor.data[k]);
                }
                fprintf(afptr, "\n");
            }
            fclose(afptr);
        } else {
            perror("Failed to write anchors.txt");
        }
    } else {
        // Write FITS anchors
        fitsfile *afptr;
        snprintf(out_path, sizeof(out_path), "!%s/anchors.fits", out_dir);
        fits_create_file(&afptr, out_path, &status);
        long naxes[3] = { get_frame_width(), get_frame_height(), state->num_clusters };
        fits_create_img(afptr, DOUBLE_IMG, 3, naxes, &status);

        for (int i = 0; i < state->num_clusters; i++) {
            long fpixel[3] = {1, 1, i + 1};
            fits_write_pix(afptr, TDOUBLE, fpixel, nelements, state->clusters[i].anchor.data, &status);
        }
        fits_close_file(afptr, &status);
    }

    // Write Cluster Counts
    int *cluster_counts = (int *)calloc(state->num_clusters, sizeof(int));
    for (long i = 0; i < state->total_frames_processed; i++) {
        if (state->assignments[i] >= 0 && state->assignments[i] < state->num_clusters)
            cluster_counts[state->assignments[i]]++;
    }

    snprintf(out_path, sizeof(out_path), "%s/cluster_counts.txt", out_dir);
    FILE *count_out = fopen(out_path, "w");
    if (count_out) {
        for (int c = 0; c < state->num_clusters; c++) {
            fprintf(count_out, "Cluster %d: %d frames\n", c, cluster_counts[c]);
        }
        fclose(count_out);
    }

    // Write Cluster Files and Average
    printf("Writing cluster files...\n");

    if (is_ascii_input_mode() && !config->fitsout_mode) {
        // ASCII Mode
        FILE *avg_file = NULL;
        double *avg_buffer = NULL;
        if (config->average_mode) {
            snprintf(out_path, sizeof(out_path), "%s/average.txt", out_dir);
            avg_file = fopen(out_path, "w");
            avg_buffer = (double *)calloc(nelements, sizeof(double));
        }

        for (int c = 0; c < state->num_clusters; c++) {
            if (cluster_counts[c] == 0) {
                if (config->average_mode && avg_file) {
                    for (long k=0; k<nelements; k++) fprintf(avg_file, "0.0 ");
                    fprintf(avg_file, "\n");
                }
                continue;
            }

            char fname[1024];
            snprintf(fname, sizeof(fname), "%s/cluster_%d.txt", out_dir, c);
            FILE *cfptr = fopen(fname, "w");
            if (!cfptr) {
                perror("Failed to create cluster txt file");
                continue;
            }

            if (config->average_mode) {
                for (long k=0; k<nelements; k++) avg_buffer[k] = 0.0;
            }

            for (long f = 0; f < state->total_frames_processed; f++) {
                if (state->assignments[f] == c) {
                    Frame *fr = getframe_at(f);
                    if (fr) {
                        for (long k = 0; k < nelements; k++) {
                            fprintf(cfptr, "%f ", fr->data[k]);
                            if (config->average_mode) avg_buffer[k] += fr->data[k];
                        }
                        fprintf(cfptr, "\n");
                        free_frame(fr);
                    }
                }
            }
            fclose(cfptr);

            if (config->average_mode && avg_file) {
                for (long k=0; k<nelements; k++) {
                    fprintf(avg_file, "%f ", avg_buffer[k] / cluster_counts[c]);
                }
                fprintf(avg_file, "\n");
            }
        }
        if (config->average_mode) {
            if (avg_file) fclose(avg_file);
            free(avg_buffer);
        }

        char *clustered_fname = (char *)malloc(strlen(config->fits_filename) + 20);
        strcpy(clustered_fname, config->fits_filename);
        char *ext = strrchr(clustered_fname, '.');
        if (ext && strcmp(ext, ".txt") == 0) {
            strcpy(ext, ".clustered.txt");
        } else {
            strcat(clustered_fname, ".clustered.txt");
        }

        FILE *clustered_out = fopen(clustered_fname, "w");
        if (clustered_out) {
            // Write Header
            fprintf(clustered_out, "# Parameters:\n");
            fprintf(clustered_out, "# rlim %.6f\n", config->rlim);
            fprintf(clustered_out, "# dprob %.6f\n", config->deltaprob);
            fprintf(clustered_out, "# maxcl %d\n", config->maxnbclust);
            fprintf(clustered_out, "# maxim %ld\n", config->maxnbfr);
            fprintf(clustered_out, "# gprob_mode %d\n", config->gprob_mode);
            fprintf(clustered_out, "# fmatcha %.2f\n", config->fmatch_a);
            fprintf(clustered_out, "# fmatchb %.2f\n", config->fmatch_b);

            fprintf(clustered_out, "# Stats:\n");
            fprintf(clustered_out, "# Total Clusters %d\n", state->num_clusters);
            fprintf(clustered_out, "# Total Distance Computations %ld\n", state->framedist_calls);
            fprintf(clustered_out, "# Clusters Pruned %ld\n", state->clusters_pruned);
            double avg_dist = (state->total_frames_processed > 0) ? (double)state->framedist_calls / state->total_frames_processed : 0.0;
            fprintf(clustered_out, "# Avg Dist/Frame %.2f\n", avg_dist);

            int next_new_cluster = 0;

            for (long i = 0; i < state->total_frames_processed; i++) {
                int assigned = state->assignments[i];
                if (assigned == next_new_cluster) {
                    // This frame created a new cluster
                    // We need anchor coordinates.
                    // Since clusters are created sequentially 0, 1, 2...
                    // The anchor for cluster 'assigned' is stored in state->clusters[assigned].anchor
                    // BUT: state->clusters stores only the ANCHOR frame data.
                    // Is the anchor for cluster X *exactly* the frame that created it? Yes.
                    // So we can use state->clusters[assigned].anchor.data

                    fprintf(clustered_out, "# NEWCLUSTER %d %ld ", assigned, i);
                    for (long k = 0; k < nelements; k++) {
                        fprintf(clustered_out, "%f ", state->clusters[assigned].anchor.data[k]);
                    }
                    fprintf(clustered_out, "\n");
                    next_new_cluster++;
                }

                Frame *fr = getframe_at(i);
                if (fr) {
                    // Format: FrameIndex ClusterID [Data...]
                    fprintf(clustered_out, "%ld %d ", i, assigned);
                    for (long k = 0; k < nelements; k++) {
                        fprintf(clustered_out, "%f ", fr->data[k]);
                    }
                    fprintf(clustered_out, "\n");
                    free_frame(fr);
                }
            }
            fclose(clustered_out);
            printf("Created clustered file: %s\n", clustered_fname);
        } else {
            perror("Failed to create clustered output file");
        }
        free(clustered_fname);

    } else {
        // FITS Mode
        fitsfile *avg_ptr = NULL;
        double *avg_buffer = NULL;
        if (config->average_mode) {
            snprintf(out_path, sizeof(out_path), "!%s/average.fits", out_dir);
            fits_create_file(&avg_ptr, out_path, &status);
            long anaxes[3] = { get_frame_width(), get_frame_height(), state->num_clusters };
            fits_create_img(avg_ptr, DOUBLE_IMG, 3, anaxes, &status);
            avg_buffer = (double *)calloc(nelements, sizeof(double));
        }

        for (int c = 0; c < state->num_clusters; c++) {
            if (cluster_counts[c] == 0) {
                 if (config->average_mode) {
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

            if (config->average_mode) {
                 for (long k=0; k<nelements; k++) avg_buffer[k] = 0.0;
            }

            int frame_count_in_cluster = 0;
            for (long f = 0; f < state->total_frames_processed; f++) {
                if (state->assignments[f] == c) {
                    Frame *fr = getframe_at(f);
                    if (fr) {
                        long fpixel[3] = {1, 1, frame_count_in_cluster + 1};
                        fits_write_pix(cfptr, TDOUBLE, fpixel, nelements, fr->data, &status);

                        if (config->average_mode) {
                            for (long k=0; k<nelements; k++) avg_buffer[k] += fr->data[k];
                        }

                        free_frame(fr);
                        frame_count_in_cluster++;
                    }
                }
            }
            fits_close_file(cfptr, &status);

            if (config->average_mode) {
                for (long k=0; k<nelements; k++) avg_buffer[k] /= cluster_counts[c];
                long fpixel[3] = {1, 1, c + 1};
                fits_write_pix(avg_ptr, TDOUBLE, fpixel, nelements, avg_buffer, &status);
            }
        }

        if (config->average_mode) {
            free(avg_buffer);
            fits_close_file(avg_ptr, &status);
        }
    }

    free(cluster_counts);
    free(out_dir);
}
