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

double calc_min_dist_4pt(double d14, double d24, double d12, double d13, double d23) {
    if (d12 < 1e-9) return fabs(d14 - d13);

    double x3 = (d13*d13 + d12*d12 - d23*d23) / (2.0 * d12);
    double y3_sq = d13*d13 - x3*x3;
    double y3 = (y3_sq > 0.0) ? sqrt(y3_sq) : 0.0;

    double x4 = (d14*d14 + d12*d12 - d24*d24) / (2.0 * d12);
    double y4_sq = d14*d14 - x4*x4;
    double y4 = (y4_sq > 0.0) ? sqrt(y4_sq) : 0.0;

    return sqrt((x3 - x4)*(x3 - x4) + (y3 - y4)*(y3 - y4));
}

int get_prediction_candidates(ClusterState *state, ClusterConfig *config, int *candidates, int max_candidates) {
    long total = state->total_frames_processed;
    int len = config->pred_len;
    int h = config->pred_h;

    if (total < len) return 0;

    long search_limit = total - len;
    long search_start = (total > h) ? total - h : 0;
    if (search_start > search_limit) search_start = search_limit;

    int *pattern = &state->assignments[total - len];

    int *counts = (int *)calloc(state->num_clusters, sizeof(int));
    if (!counts) return 0;

    for (long i = search_start; i < search_limit; i++) {
        if (state->assignments[i] == pattern[0]) {
            if (memcmp(&state->assignments[i], pattern, len * sizeof(int)) == 0) {
                int next_cluster = state->assignments[i + len];
                if (next_cluster >= 0 && next_cluster < state->num_clusters) {
                    counts[next_cluster]++;
                }
            }
        }
    }

    int count_non_zero = 0;
    for(int i=0; i<state->num_clusters; i++) if (counts[i] > 0) count_non_zero++;

    if (count_non_zero == 0) {
        free(counts);
        return 0;
    }

    Candidate *cand_list = (Candidate *)malloc(count_non_zero * sizeof(Candidate));
    int idx = 0;
    for(int i=0; i<state->num_clusters; i++) {
        if (counts[i] > 0) {
            cand_list[idx].id = i;
            cand_list[idx].p = (double)counts[i];
            idx++;
        }
    }

    qsort(cand_list, count_non_zero, sizeof(Candidate), compare_candidates);

    int n_out = (count_non_zero < max_candidates) ? count_non_zero : max_candidates;
    for(int i=0; i<n_out; i++) {
        candidates[i] = cand_list[i].id;
    }

    free(cand_list);
    free(counts);
    return n_out;
}


/**
 * Calculates the minimum distance d34 between Point 3 and Point 4.
 * @param d12 Distance between Point 1 and Point 2 (The shared base)
 * @param d13 Distance between Point 1 and Point 3
 * @param d14 Distance between Point 1 and Point 4
 * @param d23 Distance between Point 2 and Point 3
 * @param d24 Distance between Point 2 and Point 4
 * @return The minimum distance between Point 3 and Point 4. Returns -1.0 if the configuration is invalid.
 */
double get_min_distance_d34(double d12, double d13, double d14, double d23, double d24) {
    // 1. Calculate squared distances for efficiency
    double d12_sq = d12 * d12;
    double d13_sq = d13 * d13;
    double d14_sq = d14 * d14;
    double d23_sq = d23 * d23;
    double d24_sq = d24 * d24;

    // 2. Calculate the "Cosine Numerator" terms (K)
    // K represents 2 * d12 * side * cos(theta) derived from Law of Cosines
    double K_123 = d12_sq + d13_sq - d23_sq;
    double K_124 = d12_sq + d14_sq - d24_sq;

    // 3. Calculate the Heron's Polynomial terms (H)
    // H represents (2 * Area * 2)^2. If H < 0, the triangle is impossible.
    double H_123 = 4.0 * d12_sq * d13_sq - (K_123 * K_123);
    double H_124 = 4.0 * d12_sq * d14_sq - (K_124 * K_124);

    // Validate geometry: H terms must be non-negative
    if (H_123 < 0 || H_124 < 0) {
        printf("Error: Invalid triangle geometry provided.\n");
        return -1.0;
    }

    // 4. Calculate the squared distance x^2
    // To find the MINIMUM distance (points on same side), we ADD the sqrt(H*H) term inside the bracket
    // because the standard formula is d13^2 + d14^2 - 2*d13*d14*cos(a-b).
    // The expansion of cos(a-b) is cos*cos + sin*sin.
    double cross_term = (K_123 * K_124) + sqrt(H_123 * H_124);

    double x_sq = d13_sq + d14_sq - (cross_term / (2.0 * d12_sq));

    // Handle floating point epsilon errors slightly below zero
    if (x_sq < 0 && x_sq > -1e-9) x_sq = 0.0;

    if (x_sq < 0) {
        printf("Error: Calculated squared distance is negative (impossible).\n");
        return -1.0;
    }

    return sqrt(x_sq);
}






void run_clustering(ClusterConfig *config, ClusterState *state) {
    long actual_frames = get_num_frames();
    if (actual_frames > config->maxnbfr) actual_frames = config->maxnbfr;

    // Allocate assignments array
    state->assignments = (int *)malloc(actual_frames * sizeof(int));
    state->frame_infos = (FrameInfo *)calloc(actual_frames, sizeof(FrameInfo));

    state->max_steps_recorded = config->maxnbclust;
    state->pruned_fraction_sum = (double *)calloc(state->max_steps_recorded, sizeof(double));
    state->step_counts = (long *)calloc(state->max_steps_recorded, sizeof(long));
    state->transition_matrix = (long *)calloc(config->maxnbclust * config->maxnbclust, sizeof(long));

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

    int prev_assigned_cluster = -1;
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

            if (config->pred_mode && state->total_frames_processed >= config->pred_len) {
                int *pred_candidates = (int*)malloc(config->pred_n * sizeof(int));
                if (pred_candidates) {
                    int num_preds = get_prediction_candidates(state, config, pred_candidates, config->pred_n);

                    for (int p = 0; p < num_preds; p++) {
                        int cj = pred_candidates[p];
                        if (!state->clmembflag[cj]) continue;

                        if (temp_count < state->max_steps_recorded && state->num_clusters > 0) {
                            int pruned_cnt = 0;
                            for(int pc=0; pc<state->num_clusters; pc++) if(state->clmembflag[pc] == 0) pruned_cnt++;
                            state->pruned_fraction_sum[temp_count] += (double)pruned_cnt / state->num_clusters;
                            state->step_counts[temp_count]++;
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
                                printf(ANSI_COLOR_GREEN "  [VV] Frame %ld assigned to Cluster %d (Prediction)\n" ANSI_COLOR_RESET, state->total_frames_processed, assigned_cluster);
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

                            if (dcc - dfc > config->rlim) {
                                state->clmembflag[cl] = 0;
                                state->clusters_pruned++;
                            }
                            if (dfc - dcc > config->rlim) {
                                state->clmembflag[cl] = 0;
                                state->clusters_pruned++;
                            }
                        }

                        // TE4 Pruning
                        if (config->te4_mode && temp_count > 1) {
                            for (int p = 0; p < temp_count - 1; p++) {
                                int cprev = temp_indices[p];
                                double d_m_cprev = temp_dists[p];
                                double d_ci_cprev = state->dccarray[cj * config->maxnbclust + cprev];

                                if (d_ci_cprev < 0) {
                                     d_ci_cprev = get_dist(&state->clusters[cj].anchor, &state->clusters[cprev].anchor, -1, -1.0, -1.0, config, state);
                                     state->dccarray[cj * config->maxnbclust + cprev] = d_ci_cprev;
                                     state->dccarray[cprev * config->maxnbclust + cj] = d_ci_cprev;
                                }

                                for (int k = 0; k < state->num_clusters; k++) {
                                    if (!state->clmembflag[k]) continue;
                                    if (k == cj || k == cprev) continue;

                                    double d_ci_ck = state->dccarray[cj * config->maxnbclust + k];
                                    if (d_ci_ck < 0) {
                                         d_ci_ck = get_dist(&state->clusters[cj].anchor, &state->clusters[k].anchor, -1, -1.0, -1.0, config, state);
                                         state->dccarray[cj * config->maxnbclust + k] = d_ci_ck;
                                         state->dccarray[k * config->maxnbclust + cj] = d_ci_ck;
                                    }

                                    double d_cprev_ck = state->dccarray[cprev * config->maxnbclust + k];
                                    if (d_cprev_ck < 0) {
                                         d_cprev_ck = get_dist(&state->clusters[cprev].anchor, &state->clusters[k].anchor, -1, -1.0, -1.0, config, state);
                                         state->dccarray[cprev * config->maxnbclust + k] = d_cprev_ck;
                                         state->dccarray[k * config->maxnbclust + cprev] = d_cprev_ck;
                                    }

                                    double min_d = calc_min_dist_4pt(dfc, d_m_cprev, d_ci_cprev, d_ci_ck, d_cprev_ck);
                                    if (min_d > config->rlim) {
                                        state->clmembflag[k] = 0;
                                        state->clusters_pruned++;
                                    }
                                }
                            }
                        }

                        state->clmembflag[cj] = 0;
                    }
                    free(pred_candidates);
                }
            }

            while (!found) {
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

                // Track pruning stats
                if (temp_count < state->max_steps_recorded && state->num_clusters > 0) {
                    int pruned_cnt = 0;
                    for(int pc=0; pc<state->num_clusters; pc++) {
                        if(state->clmembflag[pc] == 0) pruned_cnt++;
                    }
                    state->pruned_fraction_sum[temp_count] += (double)pruned_cnt / state->num_clusters;
                    state->step_counts[temp_count]++;
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

                    if (dcc - dfc > config->rlim) {
                        state->clmembflag[cl] = 0;
                        state->clusters_pruned++;
                    }
                    if (dfc - dcc > config->rlim) {
                        state->clmembflag[cl] = 0;
                        state->clusters_pruned++;
                    }
                }

                // TE4 Pruning
                if (config->te4_mode && temp_count > 1) {
                    for (int p = 0; p < temp_count - 1; p++) {
                        int cprev = temp_indices[p];
                        double d_m_cprev = temp_dists[p];
                        double d_ci_cprev = state->dccarray[cj * config->maxnbclust + cprev];

                        if (d_ci_cprev < 0) {
                             d_ci_cprev = get_dist(&state->clusters[cj].anchor, &state->clusters[cprev].anchor, -1, -1.0, -1.0, config, state);
                             state->dccarray[cj * config->maxnbclust + cprev] = d_ci_cprev;
                             state->dccarray[cprev * config->maxnbclust + cj] = d_ci_cprev;
                        }

                        for (int k = 0; k < state->num_clusters; k++) {
                            if (!state->clmembflag[k]) continue;
                            if (k == cj || k == cprev) continue;

                            double d_ci_ck = state->dccarray[cj * config->maxnbclust + k];
                            if (d_ci_ck < 0) {
                                 d_ci_ck = get_dist(&state->clusters[cj].anchor, &state->clusters[k].anchor, -1, -1.0, -1.0, config, state);
                                 state->dccarray[cj * config->maxnbclust + k] = d_ci_ck;
                                 state->dccarray[k * config->maxnbclust + cj] = d_ci_ck;
                            }

                            double d_cprev_ck = state->dccarray[cprev * config->maxnbclust + k];
                            if (d_cprev_ck < 0) {
                                 d_cprev_ck = get_dist(&state->clusters[cprev].anchor, &state->clusters[k].anchor, -1, -1.0, -1.0, config, state);
                                 state->dccarray[cprev * config->maxnbclust + k] = d_cprev_ck;
                                 state->dccarray[k * config->maxnbclust + cprev] = d_cprev_ck;
                            }

                            double min_d = calc_min_dist_4pt(dfc, d_m_cprev, d_ci_cprev, d_ci_ck, d_cprev_ck);
                            if (min_d > config->rlim) {
                                state->clmembflag[k] = 0;
                                state->clusters_pruned++;
                            }
                        }
                    }
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

                    int start_idx = 0;
                    if (state->cluster_visitors[cj].count > config->max_gprob_visitors) {
                        start_idx = state->cluster_visitors[cj].count - config->max_gprob_visitors;
                    }
                    for (int i = start_idx; i < state->cluster_visitors[cj].count; i++) {
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

        // Update transition matrix
        if (state->total_frames_processed > 0 && prev_assigned_cluster != -1) {
            state->transition_matrix[prev_assigned_cluster * config->maxnbclust + assigned_cluster]++;
        }
        prev_assigned_cluster = assigned_cluster;

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

    if (state->num_clusters < config->maxnbclust && !stop_requested) {
        printf(ANSI_COLOR_GREEN "All frames clustered.\n" ANSI_COLOR_RESET);
    }

    printf("Analysis complete.\n");
    printf("Total clusters: %d\n", state->num_clusters);
    printf("Processing time: %.3f ms\n", elapsed_ms);
    printf("Framedist calls: %ld\n", state->framedist_calls);

    if (ascii_out) fclose(ascii_out);

    // Report pruning stats
    printf("Average fraction of clusters pruned per step:\n");
    for (int k = 0; k < state->max_steps_recorded; k++) {
        if (state->step_counts[k] > 0) {
            printf("  Step %d: %.4f (averaged over %ld frames)\n", k, state->pruned_fraction_sum[k] / state->step_counts[k], state->step_counts[k]);
        } else if (k > 0 && state->step_counts[k] == 0) {
            break;
        }
    }

    free(temp_indices);
    free(temp_dists);
    if (verbose_candidates) free(verbose_candidates);
}
