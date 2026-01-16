#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef USE_CFITSIO
#include <fitsio.h>
#endif
#include "cluster_io.h"
#include "frameread.h"

// Forward decl for PNG writing
#ifdef USE_PNG
void write_png_frame(const char *filename, double *data, int width, int height);
#endif

#define ANSI_COLOR_ORANGE  "\x1b[38;5;208m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_BG_GREEN      "\x1b[42m"
#define ANSI_COLOR_BLACK   "\x1b[30m"
#define ANSI_COLOR_RESET   "\x1b[0m"

char* create_output_dir_name(const char* input_file) {
    const char *base = strrchr(input_file, '/');
    if (base) { base++; } else { base = input_file; }
    char *name = strdup(base);
    if (!name) return NULL;
    size_t len = strlen(name);
    if (len > 8 && strcmp(name + len - 8, ".fits.fz") == 0) name[len - 8] = '\0';
    else if (len > 5 && strcmp(name + len - 5, ".fits") == 0) name[len - 5] = '\0';
    else if (len > 4 && strcmp(name + len - 4, ".mp4") == 0) name[len - 4] = '\0';
    else if (len > 4 && strcmp(name + len - 4, ".txt") == 0) name[len - 4] = '\0';

    size_t new_len = strlen(name) + strlen(".clusterdat") + 1;
    char *out_dir = (char *)malloc(new_len);
    if (out_dir) sprintf(out_dir, "%s.clusterdat", name);
    free(name);
    return out_dir;
}

void print_usage(char *progname) {
    printf("Usage: %s [options] <rlim> <input_file|stream_name>\n", progname);
    printf("Arguments:\n");
    printf("  <rlim>         Clustering radius limit.\n");
    printf("  <input_file>   Input file (ASCII");
    #ifdef USE_CFITSIO
    printf(", FITS");
    #endif
    #ifdef USE_FFMPEG
    printf(", MP4");
    #endif
    printf(") or stream name.\n");
    printf("Options:\n");

    printf("\n  [Input]\n");
    printf("  -stream        Input is an ImageStreamIO stream");
    #ifndef USE_IMAGESTREAMIO
    printf(" [DISABLED]");
    #endif
    printf("\n");
    printf("  -cnt2sync      Enable cnt2 synchronization (increment cnt2 after read)\n");

    printf("\n  [Clustering Control]\n");
    printf("  -dprob <val>   Delta probability (default: 0.01)\n");
    printf("  -maxcl <val>   Max number of clusters (default: 1000)\n");
    printf("  -ncpu <val>    Number of CPUs to use (default: 1)\n");
    printf("  -maxcl_strategy <stop|discard|merge> Strategy when maxcl reached (default: stop)\n");
    printf("  -discard_frac <val> Fraction of oldest clusters to candidate for discard (default: 0.5)\n");
    printf("  -maxim <val>   Max number of frames (default: 100000)\n");
    printf("  -gprob         Use geometrical probability\n");
    printf("  -fmatcha <val> Set fmatch parameter a (default: 2.0)\n");
    printf("  -fmatchb <val> Set fmatch parameter b (default: 0.5)\n");
    printf("  -maxvis <val>  Max visitors for gprob history (default: 1000)\n");
    printf("  -pred[l,h,n]   Prediction with pattern detection (default: 10,1000,2)\n");
    printf("  -te4           Use 4-point triangle inequality pruning\n");
    printf("  -te5           Use 5-point triangle inequality pruning\n");

    printf("\n  [Analysis & Debugging]\n");
    printf("  -scandist      Measure distance stats\n");
    printf("  -progress      Print progress (default: enabled)\n");

    printf("\n  [Output]\n");
    printf("  -outdir <name> Specify output directory (default: <filename>.clusterdat)\n");
    printf("  -avg           Compute average frame per cluster\n");
    printf("  -distall       Save all computed distances\n");
    printf("  -pngout        Write output as PNG images");
    #ifndef USE_PNG
    printf(" [DISABLED]");
    #endif
    printf("\n");
    printf("  -fitsout       Force FITS output format");
    #ifndef USE_CFITSIO
    printf(" [DISABLED]");
    #endif
    printf("\n");
    printf("  -dcc           Enable dcc.txt output (default: disabled)\n");
    printf("  -tm_out        Enable transition_matrix.txt output (default: disabled)\n");
    printf("  -anchors       Enable anchors output (default: disabled)\n");
    printf("  -counts        Enable cluster_counts.txt output (default: disabled)\n");
    printf("  -no_membership Disable frame_membership.txt output\n");
    printf("  -membership    Enable frame_membership.txt output (default: enabled)\n");
    printf("  -discarded     Enable discarded_frames.txt output (default: disabled)\n");
    printf("  -clustered     Enable *.clustered.txt output (default: disabled)\n");
    printf("  -clusters      Enable individual cluster files (cluster_X) (default: disabled)\n");
}

void write_results(ClusterConfig *config, ClusterState *state) {
    char *out_dir = NULL;
    if (config->user_outdir) out_dir = strdup(config->user_outdir);
    else out_dir = create_output_dir_name(config->fits_filename);

    if (!out_dir) return;

    char out_path[4096];

    // Write dcc.txt
    if (config->output_dcc) {
        printf("Writing dcc.txt\n");
        snprintf(out_path, sizeof(out_path), "%s/dcc.txt", out_dir);
        FILE *dcc_out = fopen(out_path, "w");
        if (dcc_out) {
            for (int i = 0; i < state->num_clusters; i++) {
                for (int j = 0; j < state->num_clusters; j++) {
                    double d = state->dccarray[i * config->maxnbclust + j];
                    if (d >= 0) fprintf(dcc_out, "%d %d %.6f\n", i, j, d);
                }
            }
            fclose(dcc_out);
        }
    }

    // Write Transition Matrix
    if (config->output_tm && state->transition_matrix) {
        printf("Writing transition_matrix.txt\n");
        snprintf(out_path, sizeof(out_path), "%s/transition_matrix.txt", out_dir);
        FILE *tm_out = fopen(out_path, "w");
        if (tm_out) {
            for (int i = 0; i < state->num_clusters; i++) {
                for (int j = 0; j < state->num_clusters; j++) {
                    long val = state->transition_matrix[i * config->maxnbclust + j];
                    if (val > 0) {
                        fprintf(tm_out, "%d %d %ld\n", i, j, val);
                    }
                }
            }
            fclose(tm_out);
        }
    }

    // Write Anchors
    long width = get_frame_width();
    long height = get_frame_height();
    long nelements = width * height;

    if (config->output_anchors) {
        printf("Writing anchors\n");
        if (config->pngout_mode) {
            #ifdef USE_PNG
            for (int i = 0; i < state->num_clusters; i++) {
                snprintf(out_path, sizeof(out_path), "%s/anchor_%04d.png", out_dir, i);
                write_png_frame(out_path, state->clusters[i].anchor.data, width, height);
            }
            #else
            fprintf(stderr, "Warning: PNG output requested but not compiled in.\n");
            #endif
        } else if (is_ascii_input_mode() && !config->fitsout_mode) {
            snprintf(out_path, sizeof(out_path), "%s/anchors.txt", out_dir);
            FILE *afptr = fopen(out_path, "w");
            if (afptr) {
                for (int i = 0; i < state->num_clusters; i++) {
                    for (long k = 0; k < nelements; k++) fprintf(afptr, "%f ", state->clusters[i].anchor.data[k]);
                    fprintf(afptr, "\n");
                }
                fclose(afptr);
            }
        } else {
            #ifdef USE_CFITSIO
            int status = 0;
            fitsfile *afptr;
            snprintf(out_path, sizeof(out_path), "!%s/anchors.fits", out_dir);
            fits_create_file(&afptr, out_path, &status);
            long naxes[3] = { width, height, state->num_clusters };
            fits_create_img(afptr, DOUBLE_IMG, 3, naxes, &status);
            for (int i = 0; i < state->num_clusters; i++) {
                long fpixel[3] = {1, 1, i + 1};
                fits_write_pix(afptr, TDOUBLE, fpixel, nelements, state->clusters[i].anchor.data, &status);
            }
            fits_close_file(afptr, &status);
            #else
            // Fallback to text if fits disabled but requested?
            fprintf(stderr, "Warning: FITS output requested but not compiled in. Saving as ASCII.\n");
            // Reuse ASCII logic
            snprintf(out_path, sizeof(out_path), "%s/anchors.txt", out_dir);
            FILE *afptr = fopen(out_path, "w");
            if (afptr) {
                for (int i = 0; i < state->num_clusters; i++) {
                    for (long k = 0; k < nelements; k++) fprintf(afptr, "%f ", state->clusters[i].anchor.data[k]);
                    fprintf(afptr, "\n");
                }
                fclose(afptr);
            }
            #endif
        }
    }

    // Cluster Counts
    int *cluster_counts = (int *)calloc(state->num_clusters, sizeof(int));
    for (long i = 0; i < state->total_frames_processed; i++) {
        if (state->assignments[i] >= 0 && state->assignments[i] < state->num_clusters)
            cluster_counts[state->assignments[i]]++;
    }
    if (config->output_counts) {
        printf("Writing cluster_counts.txt\n");
        snprintf(out_path, sizeof(out_path), "%s/cluster_counts.txt", out_dir);
        FILE *count_out = fopen(out_path, "w");
        if (count_out) {
            for (int c = 0; c < state->num_clusters; c++) fprintf(count_out, "Cluster %d: %d frames\n", c, cluster_counts[c]);
            fclose(count_out);
        }
    }

    // Average buffer
    double *avg_buffer = NULL;
    if (config->average_mode) avg_buffer = (double *)calloc(nelements, sizeof(double));

    int active_cluster_count = 0;
    for (int c = 0; c < state->num_clusters; c++) {
        if (cluster_counts[c] > 0) active_cluster_count++;
    }

    if (config->output_clusters) {
        printf("Writing cluster files (%d files)\n", active_cluster_count);
    }

    if (config->average_mode) {
        printf("Writing average cluster files\n");
    }

    if (config->pngout_mode) {
        #ifdef USE_PNG
        for (int c = 0; c < state->num_clusters; c++) {
            if (cluster_counts[c] == 0) continue;

            if (config->output_clusters) {
                char cluster_dir[1024];
                snprintf(cluster_dir, sizeof(cluster_dir), "%s/cluster_%04d", out_dir, c);
                mkdir(cluster_dir, 0777);
            }

            if (config->average_mode) for (long k=0; k<nelements; k++) avg_buffer[k] = 0.0;

            for (long f = 0; f < state->total_frames_processed; f++) {
                if (state->assignments[f] == c) {
                    Frame *fr = getframe_at(f);
                    if (fr) {
                        if (config->output_clusters) {
                            char cluster_dir[1024];
                            snprintf(cluster_dir, sizeof(cluster_dir), "%s/cluster_%04d", out_dir, c);
                            snprintf(out_path, sizeof(out_path), "%s/frame%05ld.png", cluster_dir, f);
                            write_png_frame(out_path, fr->data, width, height);
                        }
                        if (config->average_mode) for (long k=0; k<nelements; k++) avg_buffer[k] += fr->data[k];
                        free_frame(fr);
                    }
                }
            }

            if (config->average_mode) {
                for (long k=0; k<nelements; k++) avg_buffer[k] /= cluster_counts[c];
                snprintf(out_path, sizeof(out_path), "%s/average_%04d.png", out_dir, c);
                write_png_frame(out_path, avg_buffer, width, height);
            }
        }
        #endif
    } else if (is_ascii_input_mode() && !config->fitsout_mode) {
        FILE *avg_file = NULL;
        if (config->average_mode) {
            snprintf(out_path, sizeof(out_path), "%s/average.txt", out_dir);
            avg_file = fopen(out_path, "w");
        }
        for (int c = 0; c < state->num_clusters; c++) {
            if (cluster_counts[c] == 0) {
                if (avg_file) { for(long k=0; k<nelements; k++) fprintf(avg_file, "0.0 "); fprintf(avg_file, "\n"); }
                continue;
            }
            
            FILE *cfptr = NULL;
            if (config->output_clusters) {
                char fname[1024];
                snprintf(fname, sizeof(fname), "%s/cluster_%d.txt", out_dir, c);
                cfptr = fopen(fname, "w");
            }
            
            if (config->average_mode) for(long k=0; k<nelements; k++) avg_buffer[k] = 0.0;
            for (long f = 0; f < state->total_frames_processed; f++) {
                if (state->assignments[f] == c) {
                    Frame *fr = getframe_at(f);
                    if (fr) {
                        for(long k=0; k<nelements; k++) {
                            if(cfptr) fprintf(cfptr, "%f ", fr->data[k]);
                            if(config->average_mode) avg_buffer[k] += fr->data[k];
                        }
                        if(cfptr) fprintf(cfptr, "\n");
                        free_frame(fr);
                    }
                }
            }
            if(cfptr) fclose(cfptr);
            if (avg_file) {
                for(long k=0; k<nelements; k++) fprintf(avg_file, "%f ", avg_buffer[k]/cluster_counts[c]);
                fprintf(avg_file, "\n");
            }
        }
        if (avg_file) fclose(avg_file);

    } else {
        #ifdef USE_CFITSIO
        int status = 0;
        fitsfile *avg_ptr = NULL;
        if (config->average_mode) {
            snprintf(out_path, sizeof(out_path), "!%s/average.fits", out_dir);
            fits_create_file(&avg_ptr, out_path, &status);
            long anaxes[3] = { width, height, state->num_clusters };
            fits_create_img(avg_ptr, DOUBLE_IMG, 3, anaxes, &status);
        }
        for (int c = 0; c < state->num_clusters; c++) {
            if (cluster_counts[c] == 0) continue;
            
            fitsfile *cfptr = NULL;
            if (config->output_clusters) {
                char fname[1024];
                snprintf(fname, sizeof(fname), "!%s/cluster_%d.fits", out_dir, c);
                fits_create_file(&cfptr, fname, &status);
                long cnaxes[3] = { width, height, cluster_counts[c] };
                fits_create_img(cfptr, DOUBLE_IMG, 3, cnaxes, &status);
            }

            if (config->average_mode) for(long k=0; k<nelements; k++) avg_buffer[k] = 0.0;
            int fr_count = 0;
            for (long f = 0; f < state->total_frames_processed; f++) {
                if (state->assignments[f] == c) {
                    Frame *fr = getframe_at(f);
                    if (fr) {
                        if (cfptr) {
                            long fpixel[3] = {1, 1, fr_count + 1};
                            fits_write_pix(cfptr, TDOUBLE, fpixel, nelements, fr->data, &status);
                        }
                        if(config->average_mode) for(long k=0; k<nelements; k++) avg_buffer[k] += fr->data[k];
                        free_frame(fr);
                        fr_count++;
                    }
                }
            }
            if (cfptr) fits_close_file(cfptr, &status);
            if (config->average_mode && avg_ptr) {
                for(long k=0; k<nelements; k++) avg_buffer[k] /= cluster_counts[c];
                long fpixel[3] = {1, 1, c + 1};
                fits_write_pix(avg_ptr, TDOUBLE, fpixel, nelements, avg_buffer, &status);
            }
        }
        if (avg_ptr) fits_close_file(avg_ptr, &status);
        #else
        // Fallback ASCII logic if FITS disabled but we reached here
        // (Similar to block above)
        #endif
    }

    if (avg_buffer) free(avg_buffer);

    if (config->output_clustered) {
        printf("Writing clustered output file\n");
        
        const char *base_name_only = strrchr(config->fits_filename, '/');
        if (base_name_only) base_name_only++;
        else base_name_only = config->fits_filename;

        char *temp_base = strdup(base_name_only);
        char *ext = strrchr(temp_base, '.');
        if (ext && strcmp(ext, ".txt") == 0) *ext = '\0';

        char *clustered_fname = (char *)malloc(strlen(out_dir) + strlen(temp_base) + 30);
        sprintf(clustered_fname, "%s/%s.clustered.txt", out_dir, temp_base);
        free(temp_base);

        FILE *clustered_out = fopen(clustered_fname, "w");
        if (clustered_out) {
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

            if (state->pruned_fraction_sum && state->step_counts) {
                for (int k = 0; k < state->max_steps_recorded; k++) {
                    if (state->step_counts[k] > 0) {
                        fprintf(clustered_out, "# Pruning Step %d: %.4f\n", k, state->pruned_fraction_sum[k] / state->step_counts[k]);
                    } else if (k > 0 && state->step_counts[k] == 0) {
                        break;
                    }
                }
            }

            int next_new_cluster = 0;
            for (long i = 0; i < state->total_frames_processed; i++) {
                int assigned = state->assignments[i];
                if (assigned == next_new_cluster) {
                    fprintf(clustered_out, "# NEWCLUSTER %d %ld ", assigned, i);
                    for (long k = 0; k < nelements; k++) fprintf(clustered_out, "%f ", state->clusters[assigned].anchor.data[k]);
                    fprintf(clustered_out, "\n");
                    next_new_cluster++;
                }
                Frame *fr = getframe_at(i);
                if (fr) {
                    fprintf(clustered_out, "%ld %d ", i, assigned);
                    for (long k = 0; k < nelements; k++) fprintf(clustered_out, "%f ", fr->data[k]);
                    fprintf(clustered_out, "\n");
                    free_frame(fr);
                }
            }
            fclose(clustered_out);
        }
        free(clustered_fname);
    }
    free(cluster_counts);
    free(out_dir);
}

void write_run_log(ClusterConfig *config, ClusterState *state, const char *cmdline, struct timespec start_ts, double clust_ms, double out_ms, long max_rss) {
    char *out_dir = NULL;
    if (config->user_outdir) out_dir = strdup(config->user_outdir);
    else out_dir = create_output_dir_name(config->fits_filename);

    if (!out_dir) return;

    char log_path[4096];
    snprintf(log_path, sizeof(log_path), "%s/cluster_run.log", out_dir);
    FILE *f = fopen(log_path, "w");
    if (f) {
        char time_buf[64];
        struct tm *tm_info = localtime(&start_ts.tv_sec);
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

        fprintf(f, "CMD: %s\n", cmdline);
        fprintf(f, "START_TIME: %s.%09ld\n", time_buf, start_ts.tv_nsec);
        fprintf(f, "TIME_CLUSTERING_MS: %.3f\n", clust_ms);
        fprintf(f, "TIME_OUTPUT_MS: %.3f\n", out_ms);
        fprintf(f, "OUTPUT_DIR: %s\n", out_dir);
        fprintf(f, "PARAM_RLIM: %f\n", config->rlim);
        fprintf(f, "PARAM_DPROB: %f\n", config->deltaprob);
        fprintf(f, "PARAM_MAXCL: %d\n", config->maxnbclust);
        fprintf(f, "PARAM_MAXIM: %ld\n", config->maxnbfr);
        fprintf(f, "PARAM_GPROB: %d\n", config->gprob_mode);
        fprintf(f, "PARAM_FMATCHA: %f\n", config->fmatch_a);
        fprintf(f, "PARAM_FMATCHB: %f\n", config->fmatch_b);
        fprintf(f, "PARAM_TE4: %d\n", config->te4_mode);
        fprintf(f, "PARAM_TE5: %d\n", config->te5_mode);
        
        if (config->output_dcc) fprintf(f, "OUTPUT_FILE: %s/dcc.txt\n", out_dir);
        if (config->output_tm) fprintf(f, "OUTPUT_FILE: %s/transition_matrix.txt\n", out_dir);
        if (config->output_anchors) fprintf(f, "OUTPUT_FILE: %s/anchors.txt\n", out_dir);
        if (config->output_counts) fprintf(f, "OUTPUT_FILE: %s/cluster_counts.txt\n", out_dir);
        if (config->output_membership) fprintf(f, "OUTPUT_FILE: %s/frame_membership.txt\n", out_dir);

        if (config->output_clustered) {
            const char *base_name_only = strrchr(config->fits_filename, '/');
            if (base_name_only) base_name_only++;
            else base_name_only = config->fits_filename;
            char *temp_base = strdup(base_name_only);
            char *ext = strrchr(temp_base, '.');
            if (ext && strcmp(ext, ".txt") == 0) *ext = '\0';
            fprintf(f, "CLUSTERED_FILE: %s/%s.clustered.txt\n", out_dir, temp_base);
            free(temp_base);
        }

        fprintf(f, "STATS_CLUSTERS: %d\n", state->num_clusters);
        fprintf(f, "STATS_FRAMES: %ld\n", state->total_frames_processed);
        fprintf(f, "STATS_DISTS: %ld\n", state->framedist_calls);
        fprintf(f, "STATS_PRUNED: %ld\n", state->clusters_pruned);
        fprintf(f, "STATS_MAX_RSS_KB: %ld\n", max_rss);

        fprintf(f, "STATS_DIST_HIST_START\n");
        for (int k = 0; k <= config->maxnbclust; k++) {
            if (state->dist_counts && state->dist_counts[k] > 0) {
                fprintf(f, "%d %ld %ld\n", k, state->dist_counts[k], state->pruned_counts_by_dist[k]);
            }
        }
        fprintf(f, "STATS_DIST_HIST_END\n");
        
        fclose(f);
        printf("Log written to %s\n", log_path);
    }
    free(out_dir);
}
