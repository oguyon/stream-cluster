#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include "cluster_core.h"
#include "frameread.h"

// Forward declaration
double framedist(Frame *a, Frame *b);

#define ANSI_COLOR_ORANGE  "\x1b[38;5;208m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_BG_GREEN      "\x1b[42m"
#define ANSI_COLOR_BLACK   "\x1b[30m"
#define ANSI_COLOR_RESET   "\x1b[0m"

int compare_candidates(const void *a, const void *b) {
    Candidate *ca = (Candidate *)a;
    Candidate *cb = (Candidate *)b;
    if (ca->p < cb->p) return 1;
    if (ca->p > cb->p) return -1;
    return 0;
}

int compare_probs(const void *a, const void *b) {
    // We need access to clusters array, but this is qsort comparator.
    // The indices are passed. But we need the `clusters` array.
    // This is tricky without a global or context.
    // However, probsortedclindex stores indices.
    // We can't easily pass context to qsort comparator in standard C.
    // We might need to make `clusters` available via a thread-local or static global in this file,
    // or use `qsort_r` (GNU extension) or `qsort_s` (C11).
    // Given the constraints and existing code using globals, we might need a workaround.
    // Since we are refactoring to remove globals, this is a blocker for `qsort`.
    // Let's use a static global pointer ONLY for this file and set it before qsort.
    return 0; // Placeholder, see workaround below
}

static Cluster *g_clusters_ptr = NULL;

int compare_probs_wrapper(const void *a, const void *b) {
    int idx_a = *(const int *)a;
    int idx_b = *(const int *)b;
    if (g_clusters_ptr[idx_a].prob > g_clusters_ptr[idx_b].prob) return -1;
    if (g_clusters_ptr[idx_a].prob < g_clusters_ptr[idx_b].prob) return 1;
    return 0;
}

int compare_doubles(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

double fmatch(double dr, double a, double b) {
    if (dr > 2.0) return 0.0;
    return a - (a - b) * dr / 2.0;
}

void add_visitor(VisitorList *list, int frame_idx) {
    if (list->count >= list->capacity) {
        int new_capacity = (list->capacity == 0) ? 16 : list->capacity * 2;
        int *new_frames = (int *)realloc(list->frames, new_capacity * sizeof(int));
        if (new_frames) {
            list->frames = new_frames;
            list->capacity = new_capacity;
        } else {
            perror("Failed to realloc visitor list");
            return;
        }
    }
    list->frames[list->count++] = frame_idx;
}

double get_dist(Frame *a, Frame *b, int cluster_idx, double cluster_prob, double current_gprob, ClusterConfig *config, ClusterState *state) {
    state->framedist_calls++;
    double d = framedist(a, b);
    if (config->distall_mode && state->distall_out) {
        double ratio = (config->rlim > 0.0) ? d / config->rlim : -1.0;
        fprintf(state->distall_out, "%-8d %-8d %-12.6f %-12.6f %-8d %-12.6f %-12.6f\n", a->id, b->id, d, ratio, cluster_idx, cluster_prob, current_gprob);
    }
    if (config->verbose_level >= 2 && cluster_idx >= 0) {
        printf(ANSI_COLOR_BLUE "  [VV] Computed distance: Frame %5d to Cluster %4d = %12.5e\n" ANSI_COLOR_RESET, a->id, cluster_idx, d);
    }
    return d;
}

void run_scandist(ClusterConfig *config, char *out_dir) {
    long nframes = get_num_frames();
    if (nframes < 2) {
        printf("Not enough frames to calculate distances.\n");
        return;
    }

    long process_limit = (nframes > config->maxnbfr) ? config->maxnbfr : nframes;
    double *distances = (double *)malloc((process_limit - 1) * sizeof(double));
    if (!distances) {
        perror("Memory allocation failed");
        return;
    }

    Frame *prev = getframe();
    if (!prev) {
         free(distances);
         return;
    }

    FILE *scan_out = NULL;
    char scan_path[1024];
    if (out_dir) {
        snprintf(scan_path, sizeof(scan_path), "%s/dist-scan.txt", out_dir);
        scan_out = fopen(scan_path, "w");
        if (scan_out) {
             fprintf(scan_out, "# Frame1 Frame2 Distance\n");
        }
    }

    printf("Scanning distances\n");

    long count = 0;
    // Loop from 1 to process_limit-1
    for (long i = 1; i < process_limit; i++) {
        Frame *curr = getframe();
        if (!curr) break;

        // framedist_calls++; // Should we count this? Original code did not use get_dist here so didn't count global calls, but did manual increment.
        // But here we don't have access to global framedist_calls in main loop unless we pass state.
        // Let's just calculate.
        double d = framedist(prev, curr);
        distances[count++] = d;

        if (scan_out) {
            fprintf(scan_out, "%d %d %.6f\n", prev->id, curr->id, d);
        }

        if (config->progress_mode && (i % 10 == 0 || i == process_limit - 1)) {
            printf("\rScanning frame %ld / %ld", i, process_limit);
            fflush(stdout);
        }

        free_frame(prev);
        prev = curr;
    }
    free_frame(prev);
    if (scan_out) fclose(scan_out);
    if (config->progress_mode) printf("\n");

    if (count > 0) {
        qsort(distances, count, sizeof(double), compare_doubles);

        double min_val = distances[0];
        double max_val = distances[count - 1];
        double median_val;
        double p20_val;
        double p80_val;

        if (count % 2 == 1) {
            median_val = distances[count / 2];
        } else {
            median_val = (distances[count / 2 - 1] + distances[count / 2]) / 2.0;
        }

        double p20_idx = (count - 1) * 0.2;
        int p20_i = (int)p20_idx;
        double p20_f = p20_idx - p20_i;
        if (p20_i + 1 < count)
             p20_val = distances[p20_i] * (1.0 - p20_f) + distances[p20_i + 1] * p20_f;
        else
             p20_val = distances[p20_i];

        double p80_idx = (count - 1) * 0.8;
        int p80_i = (int)p80_idx;
        double p80_f = p80_idx - p80_i;
        if (p80_i + 1 < count)
             p80_val = distances[p80_i] * (1.0 - p80_f) + distances[p80_i + 1] * p80_f;
        else
             p80_val = distances[p80_i];

        if (config->scandist_mode) {
            printf("Distance statistics (%ld intervals):\n", count);
            printf("%-10s %.6f\n", "Min:", min_val);
            printf("%-10s %.6f\n", "20%:", p20_val);
            printf("%-10s %.6f\n", "Median:", median_val);
            printf("%-10s %.6f\n", "80%:", p80_val);
            printf("%-10s %.6f\n", "Max:", max_val);
        } else if (config->auto_rlim_mode) {
            config->rlim = config->auto_rlim_factor * median_val;
            printf("Auto-rlim: Median distance = %.6f, Multiplier = %.6f -> rlim = %.6f\n", median_val, config->auto_rlim_factor, config->rlim);
        }
    } else {
        printf("No distances calculated.\n");
    }

    free(distances);
}

void run_clustering(ClusterConfig *config, ClusterState *state) {
    long actual_frames = get_num_frames();
    if (actual_frames > config->maxnbfr) actual_frames = config->maxnbfr;

    // Allocate assignments array
    state->assignments = (int *)malloc(actual_frames * sizeof(int));
    state->frame_infos = (FrameInfo *)calloc(actual_frames, sizeof(FrameInfo));

    int *temp_indices = (int *)malloc(config->maxnbclust * sizeof(int));
    double *temp_dists = (double *)malloc(config->maxnbclust * sizeof(double));
    if (!temp_indices || !temp_dists) {
        perror("Memory allocation failed for temp buffers");
        return;
    }

    Candidate *verbose_candidates = NULL;
    if (config->verbose_level >= 2) {
        verbose_candidates = (Candidate *)malloc(config->maxnbclust * sizeof(Candidate));
    }

    char *out_dir = NULL;
    // We assume out_dir is created in main or we create it here?
    // Use user_outdir or create from filename.
    // Ideally main handles directory creation.
    // Let's assume the directory exists if we are here.
    // We need the path to write `frame_membership.txt`.
    // We can recreate the name or pass it.
    // Let's check config.user_outdir or recreate.
    // But `create_output_dir_name` allocates.
    // Let's assume we can use `config->user_outdir` if set.
    // But if it wasn't set by user, main created it but maybe didn't store it back in config?
    // We should probably have stored the effective output dir in config or state.
    // Let's assume `config->user_outdir` holds the effective output directory path.

    char out_path[1024];
    if (config->user_outdir) {
        snprintf(out_path, sizeof(out_path), "%s/frame_membership.txt", config->user_outdir);
    } else {
        // Fallback?
        snprintf(out_path, sizeof(out_path), "frame_membership.txt");
    }

    FILE *ascii_out = fopen(out_path, "w");
    if (!ascii_out) {
        perror("Failed to open frame_membership.txt");
    }

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    Frame *current_frame;
    while ((current_frame = getframe()) != NULL) {
        if (stop_requested) {
            printf(ANSI_COLOR_ORANGE "\nStopping clustering on user request (CTRL+C).\n" ANSI_COLOR_RESET);
            free_frame(current_frame);
            break;
        }

        if (state->total_frames_processed >= config->maxnbfr) {
            free_frame(current_frame);
            break;
        }

        if (config->verbose_level >= 2) {
            printf("\n  [VV] Processing Frame %5ld (Clusters: %4d)\n", state->total_frames_processed, state->num_clusters);
        }

        int assigned_cluster = -1;
        int temp_count = 0;

        if (state->num_clusters == 0) {
            // Step 0
            state->clusters[0].anchor = *current_frame;
            state->clusters[0].id = 0;
            state->clusters[0].prob = 1.0;
            state->num_clusters = 1;
            assigned_cluster = 0;
            state->dccarray[0] = 0.0;
            free(current_frame); // Struct only, data transferred

            add_visitor(&state->cluster_visitors[0], state->total_frames_processed);

            temp_indices[0] = 0;
            temp_dists[0] = 0.0;
            temp_count = 1;

            if (config->verbose_level >= 2) {
                printf(ANSI_COLOR_ORANGE "  [VV] Frame %5ld created initial Cluster    0\n" ANSI_COLOR_RESET, state->total_frames_processed);
            }
        } else {
            // Step 1
            double sum_prob = 0.0;
            for (int i = 0; i < state->num_clusters; i++) sum_prob += state->clusters[i].prob;
            if (sum_prob > 0) {
                for (int i = 0; i < state->num_clusters; i++) state->clusters[i].prob /= sum_prob;
            }

            for (int i = 0; i < state->num_clusters; i++) state->current_gprobs[i] = 1.0;
            for (int i = 0; i < state->num_clusters; i++) state->clmembflag[i] = 1;

            if (!config->gprob_mode) {
                for (int i = 0; i < state->num_clusters; i++) state->probsortedclindex[i] = i;
                g_clusters_ptr = state->clusters;
                qsort(state->probsortedclindex, state->num_clusters, sizeof(int), compare_probs_wrapper);
            }

            int k = 0;
            int found = 0;

            while (1) {
                if (config->verbose_level >= 2 && verbose_candidates) {
                    int vcount = 0;
                    for (int i = 0; i < state->num_clusters; i++) {
                        if (state->clmembflag[i]) {
                            double p = state->clusters[i].prob;
                            if (config->gprob_mode) {
                                p *= state->current_gprobs[i];
                            }
                            verbose_candidates[vcount].id = i;
                            verbose_candidates[vcount].p = p;
                            vcount++;
                        }
                    }

                    if (vcount > 0) {
                        qsort(verbose_candidates, vcount, sizeof(Candidate), compare_candidates);
                        printf("  [VV] Cluster ranking:");
                        for (int i = 0; i < vcount; i++) {
                            printf(" [%4d %12.5e]", verbose_candidates[i].id, verbose_candidates[i].p);
                            if (i < vcount - 1) printf(" >");
                        }
                        printf("\n");
                    }
                }

                int cj = -1;

                if (!config->gprob_mode) {
                    while (k < state->num_clusters && state->clmembflag[state->probsortedclindex[k]] == 0) k++;
                    if (k >= state->num_clusters) break;
                    cj = state->probsortedclindex[k];
                    k++;
                } else {
                    double max_p = -1.0;
                    cj = -1;
                    for (int i = 0; i < state->num_clusters; i++) {
                        if (state->clmembflag[i]) {
                            double p = state->clusters[i].prob * state->current_gprobs[i];
                            if (p > max_p) {
                                max_p = p;
                                cj = i;
                            }
                        }
                    }
                    if (cj == -1) break;
                }

                double dfc = get_dist(current_frame, &state->clusters[cj].anchor, state->clusters[cj].id, state->clusters[cj].prob, state->current_gprobs[cj], config, state);

                if (temp_count < config->maxnbclust) {
                    temp_indices[temp_count] = cj;
                    temp_dists[temp_count] = dfc;
                    temp_count++;
                }

                add_visitor(&state->cluster_visitors[cj], state->total_frames_processed);

                if (dfc < config->rlim) {
                    assigned_cluster = cj;
                    state->clusters[cj].prob += config->deltaprob;
                    found = 1;
                    if (config->verbose_level >= 2) {
                        printf(ANSI_COLOR_GREEN "  [VV] Frame %ld assigned to Cluster %d\n" ANSI_COLOR_RESET, state->total_frames_processed, assigned_cluster);
                    }
                    break;
                }

                for (int cl = 0; cl < state->num_clusters; cl++) {
                    if (state->clmembflag[cl] == 0) continue;

                    double dcc = state->dccarray[cj * config->maxnbclust + cl];
                    if (dcc < 0) {
                        dcc = get_dist(&state->clusters[cj].anchor, &state->clusters[cl].anchor, -1, -1.0, -1.0, config, state);
                        state->dccarray[cj * config->maxnbclust + cl] = dcc;
                        state->dccarray[cl * config->maxnbclust + cj] = dcc;
                    }

                    if (dcc - dfc > config->rlim) { state->clmembflag[cl] = 0; state->clusters_pruned++; }
                    if (dfc - dcc > config->rlim) { state->clmembflag[cl] = 0; state->clusters_pruned++; }
                }

                if (state->clmembflag[cj]) {
                    state->clmembflag[cj] = 0;
                }

                int active_cluster_count = 0;
                for (int i = 0; i < state->num_clusters; i++) {
                    if (state->clmembflag[i]) active_cluster_count++;
                }

                if ((config->gprob_mode || (config->distall_mode && state->distall_out) || config->verbose_level >= 2) && active_cluster_count > 1) {
                    int match_count = state->cluster_visitors[cj].count;
                    if (match_count > 0) match_count--;

                    if (config->verbose_level >= 2) {
                        printf("  [VV] Distance > rlim. Found %d matches in distinfo for Cluster %4d (Frame %5d).\n", match_count, cj, state->clusters[cj].anchor.id);
                    }

                    for (int i = 0; i < state->cluster_visitors[cj].count; i++) {
                        int k_idx = state->cluster_visitors[cj].frames[i];
                        if (k_idx == state->total_frames_processed) continue;

                        int target_cl = state->frame_infos[k_idx].assignment;
                        int is_active = state->clmembflag[target_cl];

                        if (config->verbose_level >= 2) {
                            if (is_active) {
                                printf(ANSI_BG_GREEN ANSI_COLOR_BLACK "  [VV]   Frame %5d also had distance measurement to Cluster %4d (Anchor Frame %5d). Frame %5d cluster membership is %4d. " ANSI_COLOR_RESET "\n",
                                       k_idx, cj, state->clusters[cj].anchor.id, k_idx, target_cl);
                            } else {
                                printf("  [VV]   Frame %5d also had distance measurement to Cluster %4d (Anchor Frame %5d). Frame %5d cluster membership is %4d.\n",
                                       k_idx, cj, state->clusters[cj].anchor.id, k_idx, target_cl);
                            }
                        }

                        if (!is_active) continue;

                        double dist_k = -1.0;
                        for (int d_idx = 0; d_idx < state->frame_infos[k_idx].num_dists; d_idx++) {
                            if (state->frame_infos[k_idx].cluster_indices[d_idx] == cj) {
                                dist_k = state->frame_infos[k_idx].distances[d_idx];
                                break;
                            }
                        }

                        if (dist_k >= 0) {
                            double dr = fabs(dfc - dist_k) / config->rlim;
                            double val = fmatch(dr, config->fmatch_a, config->fmatch_b);

                            if (config->verbose_level >= 2) {
                                printf("    dist %5ld-%-5d = %12.5e  dist %5d-%-5d = %12.5e, fmatch=%12.5e, updating GProb(Cluster %4d) from %12.5e to %12.5e\n",
                                       state->total_frames_processed, state->clusters[cj].anchor.id, dfc,
                                       k_idx, state->clusters[cj].anchor.id, dist_k,
                                       val,
                                       target_cl,
                                       state->current_gprobs[target_cl],
                                       state->current_gprobs[target_cl] * val);
                            }

                            state->current_gprobs[target_cl] *= val;
                        }
                    }
                }
            }

            if (!found) {
                if (state->num_clusters < config->maxnbclust) {
                    assigned_cluster = state->num_clusters;
                    state->clusters[state->num_clusters].anchor = *current_frame;
                    state->clusters[state->num_clusters].id = state->num_clusters;
                    state->clusters[state->num_clusters].prob = 1.0;

                    for (int i = 0; i < state->num_clusters; i++) {
                        double d = get_dist(&state->clusters[state->num_clusters].anchor, &state->clusters[i].anchor, -1, -1.0, -1.0, config, state);
                        state->dccarray[state->num_clusters * config->maxnbclust + i] = d;
                        state->dccarray[i * config->maxnbclust + state->num_clusters] = d;
                    }
                    state->dccarray[state->num_clusters * config->maxnbclust + state->num_clusters] = 0.0;

                    if (config->verbose_level >= 2) {
                         printf(ANSI_COLOR_GREEN "  [VV] Frame %5ld assigned to Cluster %4d\n" ANSI_COLOR_RESET, state->total_frames_processed, assigned_cluster);
                        printf(ANSI_COLOR_ORANGE "  [VV] Frame %5ld created new Cluster %4d\n" ANSI_COLOR_RESET, state->total_frames_processed, state->num_clusters);
                    }

                    add_visitor(&state->cluster_visitors[state->num_clusters], state->total_frames_processed);

                    if (temp_count < config->maxnbclust) {
                        temp_indices[temp_count] = state->num_clusters;
                        temp_dists[temp_count] = 0.0;
                        temp_count++;
                    }

                    state->num_clusters++;
                    free(current_frame);
                } else {
                    printf(ANSI_COLOR_ORANGE "Max clusters limit reached.\n" ANSI_COLOR_RESET);
                    printf("Frames clustered: %ld\n", state->total_frames_processed);
                    free_frame(current_frame);
                    break;
                }
            } else {
                free_frame(current_frame);
            }
        }

        state->assignments[state->total_frames_processed] = assigned_cluster;
        if (ascii_out) fprintf(ascii_out, "%ld %d\n", state->total_frames_processed, assigned_cluster);

        state->frame_infos[state->total_frames_processed].assignment = assigned_cluster;
        state->frame_infos[state->total_frames_processed].num_dists = temp_count;
        if (temp_count > 0) {
            state->frame_infos[state->total_frames_processed].cluster_indices = (int *)malloc(temp_count * sizeof(int));
            state->frame_infos[state->total_frames_processed].distances = (double *)malloc(temp_count * sizeof(double));
            if (state->frame_infos[state->total_frames_processed].cluster_indices && state->frame_infos[state->total_frames_processed].distances) {
                memcpy(state->frame_infos[state->total_frames_processed].cluster_indices, temp_indices, temp_count * sizeof(int));
                memcpy(state->frame_infos[state->total_frames_processed].distances, temp_dists, temp_count * sizeof(double));
            }
        } else {
            state->frame_infos[state->total_frames_processed].cluster_indices = NULL;
            state->frame_infos[state->total_frames_processed].distances = NULL;
        }

        state->total_frames_processed++;

        if (config->progress_mode && (state->total_frames_processed % 10 == 0 || state->total_frames_processed == actual_frames)) {
            double avg_dists = (state->total_frames_processed > 0) ? (double)state->framedist_calls / state->total_frames_processed : 0.0;
            printf("\rProcessing frame %ld / %ld (Clusters: %d, Dists: %ld, Avg Dists/Frame: %.1f, Pruned: %ld)",
                   state->total_frames_processed, actual_frames, state->num_clusters, state->framedist_calls, avg_dists, state->clusters_pruned);
            fflush(stdout);
        }
    }

    if (config->progress_mode) printf("\n");

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;

    if (state->num_clusters < config->maxnbclust) {
        printf(ANSI_COLOR_GREEN "All frames clustered.\n" ANSI_COLOR_RESET);
    }

    printf("Analysis complete.\n");
    printf("Total clusters: %d\n", state->num_clusters);
    printf("Processing time: %.3f ms\n", elapsed_ms);
    printf("Framedist calls: %ld\n", state->framedist_calls);

    if (ascii_out) fclose(ascii_out);
    free(temp_indices);
    free(temp_dists);
    if (verbose_candidates) free(verbose_candidates);
}
