#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "cluster_defs.h"
#include "cluster_core.h"
#include "cluster_io.h"
#include "frameread.h"

volatile sig_atomic_t stop_requested = 0;

void handle_sigint(int sig) {
    stop_requested = 1;
}

void print_args_on_error(int argc, char *argv[]) {
    fprintf(stderr, "\nProgram arguments:\n");
    for (int i = 0; i < argc; i++) {
        fprintf(stderr, "  argv[%d] = \"%s\"\n", i, argv[i]);
    }
    fprintf(stderr, "\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        print_args_on_error(argc, argv);
        return 1;
    }

    ClusterConfig config;
    memset(&config, 0, sizeof(ClusterConfig));
    // Set defaults
    config.deltaprob = 0.01;
    config.maxnbclust = 1000;
    config.ncpu = 1;
    config.maxnbfr = 100000;
    config.fmatch_a = 2.0;
    config.fmatch_b = 0.5;
    config.max_gprob_visitors = 1000;
    config.progress_mode = 1;
    config.pred_len = 10;
    config.pred_h = 1000;
    config.pred_n = 2;
    config.maxcl_strategy = MAXCL_STOP;
    config.discard_fraction = 0.5;

    // Output defaults (disabled by default, except membership)
    config.output_dcc = 0;
    config.output_tm = 0;
    config.output_anchors = 0;
    config.output_counts = 0;
    config.output_membership = 1;
    config.output_discarded = 0;
    config.output_clustered = 0;
    config.output_clusters = 0;

    int arg_idx = 1;
    int rlim_set = 0;

    while (arg_idx < argc) {
        if (strcmp(argv[arg_idx], "-dprob") == 0) {
            config.deltaprob = atof(argv[++arg_idx]);
        } else if (strcmp(argv[arg_idx], "-maxcl") == 0) {
            config.maxnbclust = atoi(argv[++arg_idx]);
        } else if (strcmp(argv[arg_idx], "-ncpu") == 0) {
            config.ncpu = atoi(argv[++arg_idx]);
        } else if (strcmp(argv[arg_idx], "-maxim") == 0) {
            config.maxnbfr = atol(argv[++arg_idx]);
        } else if (strcmp(argv[arg_idx], "-avg") == 0) {
            config.average_mode = 1;
        } else if (strcmp(argv[arg_idx], "-distall") == 0) {
            config.distall_mode = 1;
        } else if (strcmp(argv[arg_idx], "-outdir") == 0) {
            config.user_outdir = argv[++arg_idx];
        } else if (strcmp(argv[arg_idx], "-progress") == 0) {
            config.progress_mode = 1;
        } else if (strcmp(argv[arg_idx], "-gprob") == 0) {
            config.gprob_mode = 1;
        } else if (strcmp(argv[arg_idx], "-verbose") == 0) {
            config.verbose_level = 1;
        } else if (strcmp(argv[arg_idx], "-veryverbose") == 0) {
            config.verbose_level = 2;
        } else if (strcmp(argv[arg_idx], "-fitsout") == 0) {
            config.fitsout_mode = 1;
        } else if (strcmp(argv[arg_idx], "-pngout") == 0) {
            config.pngout_mode = 1;
        } else if (strcmp(argv[arg_idx], "-stream") == 0) {
            config.stream_input_mode = 1;
        } else if (strcmp(argv[arg_idx], "-cnt2sync") == 0) {
            config.cnt2sync_mode = 1;
        } else if (strcmp(argv[arg_idx], "-fmatcha") == 0) {
            config.fmatch_a = atof(argv[++arg_idx]);
        } else if (strcmp(argv[arg_idx], "-fmatchb") == 0) {
            config.fmatch_b = atof(argv[++arg_idx]);
        } else if (strcmp(argv[arg_idx], "-maxvis") == 0) {
            config.max_gprob_visitors = atoi(argv[++arg_idx]);
        } else if (strcmp(argv[arg_idx], "-te4") == 0) {
            config.te4_mode = 1;
        } else if (strcmp(argv[arg_idx], "-te5") == 0) {
            config.te5_mode = 1;
        } else if (strcmp(argv[arg_idx], "-tm") == 0) {
            config.tm_mixing_coeff = atof(argv[++arg_idx]);
        } else if (strcmp(argv[arg_idx], "-maxcl_strategy") == 0) {
            char *strategy = argv[++arg_idx];
            if (strcmp(strategy, "stop") == 0) config.maxcl_strategy = MAXCL_STOP;
            else if (strcmp(strategy, "discard") == 0) config.maxcl_strategy = MAXCL_DISCARD;
            else if (strcmp(strategy, "merge") == 0) config.maxcl_strategy = MAXCL_MERGE;
            else {
                fprintf(stderr, "Error: Unknown maxcl_strategy '%s'. Use 'stop', 'discard', or 'merge'.\n", strategy);
                return 1;
            }
        } else if (strcmp(argv[arg_idx], "-discard_frac") == 0) {
            config.discard_fraction = atof(argv[++arg_idx]);
        } else if (strcmp(argv[arg_idx], "-dcc") == 0) {
            config.output_dcc = 1;
        } else if (strcmp(argv[arg_idx], "-tm_out") == 0) {
            config.output_tm = 1;
        } else if (strcmp(argv[arg_idx], "-anchors") == 0) {
            config.output_anchors = 1;
        } else if (strcmp(argv[arg_idx], "-counts") == 0) {
            config.output_counts = 1;
        } else if (strcmp(argv[arg_idx], "-membership") == 0) {
            config.output_membership = 1;
        } else if (strcmp(argv[arg_idx], "-no_membership") == 0) {
            config.output_membership = 0;
        } else if (strcmp(argv[arg_idx], "-discarded") == 0) {
            config.output_discarded = 1;
        } else if (strcmp(argv[arg_idx], "-clustered") == 0) {
            config.output_clustered = 1;
        } else if (strcmp(argv[arg_idx], "-clusters") == 0) {
            config.output_clusters = 1;
        } else if (strncmp(argv[arg_idx], "-pred", 5) == 0) {
            config.pred_mode = 1;
            char *params = argv[arg_idx] + 5;
            if (*params == '[') {
                params++; // Skip [
                char *end = strchr(params, ']');
                if (end) *end = '\0';
                sscanf(params, "%d,%d,%d", &config.pred_len, &config.pred_h, &config.pred_n);
            }
        } else if (strcmp(argv[arg_idx], "-scandist") == 0) {
            config.scandist_mode = 1;
        } else if (argv[arg_idx][0] == '-') {
            fprintf(stderr, "Error: Unknown option: %s\n", argv[arg_idx]);
            print_usage(argv[0]);
            print_args_on_error(argc, argv);
            return 1;
        } else {
            if (!config.scandist_mode && !rlim_set) {
                if (argv[arg_idx][0] == 'a') {
                    char *endptr;
                    config.auto_rlim_factor = strtod(argv[arg_idx] + 1, &endptr);
                    if (*endptr != '\0') {
                         fprintf(stderr, "Error: Invalid format for auto-rlim. Expected 'a<float>', got '%s'\n", argv[arg_idx]);
                         print_args_on_error(argc, argv);
                         return 1;
                    }
                    config.auto_rlim_mode = 1;
                } else {
                    char *endptr;
                    config.rlim = strtod(argv[arg_idx], &endptr);
                    if (*endptr != '\0') {
                         fprintf(stderr, "Error: Invalid rlim value: %s\n", argv[arg_idx]);
                         print_args_on_error(argc, argv);
                         return 1;
                    }
                }
                rlim_set = 1;
            } else {
                if (config.fits_filename != NULL) {
                    fprintf(stderr, "Error: Too many arguments or multiple input files specified (already have '%s', found '%s')\n", config.fits_filename, argv[arg_idx]);
                    print_args_on_error(argc, argv);
                    return 1;
                }
                config.fits_filename = argv[arg_idx];
            }
        }
        arg_idx++;
    }

    if (!config.fits_filename) {
        fprintf(stderr, "Error: Missing input file or stream name.\n");
        if (!config.scandist_mode) print_usage(argv[0]);
        print_args_on_error(argc, argv);
        return 1;
    }

    if (init_frameread(config.fits_filename, config.stream_input_mode, config.cnt2sync_mode) != 0) {
        print_args_on_error(argc, argv);
        return 1;
    }

    // Determine output directory
    char *out_dir = NULL;
    int out_dir_alloc = 0; // Flag to track if out_dir was malloced locally
    if (config.user_outdir) {
        out_dir = strdup(config.user_outdir);
        out_dir_alloc = 1;
    } else {
        out_dir = create_output_dir_name(config.fits_filename);
        out_dir_alloc = 1;
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

    if (!config.user_outdir) {
        config.user_outdir = out_dir;
    } else {
        free(out_dir);
        out_dir = NULL;
        out_dir_alloc = 0;
    }

    ClusterState state;
    memset(&state, 0, sizeof(ClusterState));

    if (config.distall_mode) {
        char out_path[1024];
        if (config.user_outdir)
            snprintf(out_path, sizeof(out_path), "%s/distall.txt", config.user_outdir);
        else {
             char *tmp = create_output_dir_name(config.fits_filename);
             snprintf(out_path, sizeof(out_path), "%s/distall.txt", tmp);
             free(tmp);
        }
        state.distall_out = fopen(out_path, "w");
        if (!state.distall_out) {
            perror("Failed to open distall.txt in output directory");
            return 1;
        }
        // ... (header printing)
    }

    if (!config.scandist_mode) {
        signal(SIGINT, handle_sigint);
        printf("CTRL+C to stop clustering and write results\n");
    }

    if (config.scandist_mode || config.auto_rlim_mode) {
        run_scandist(&config, config.user_outdir);
        if (config.scandist_mode) {
             if (state.distall_out) fclose(state.distall_out);
             close_frameread();
             if (config.user_outdir && out_dir_alloc) free(config.user_outdir);
             return 0;
        }
        reset_frameread();
    }

    // Allocate State
    state.clusters = (Cluster *)malloc(config.maxnbclust * sizeof(Cluster));
    state.dccarray = (double *)malloc(config.maxnbclust * config.maxnbclust * sizeof(double));
    for (int i = 0; i < config.maxnbclust * config.maxnbclust; i++) state.dccarray[i] = -1.0;

    state.current_gprobs = (double *)malloc(config.maxnbclust * sizeof(double));
    state.cluster_visitors = (VisitorList *)calloc(config.maxnbclust, sizeof(VisitorList));
    state.probsortedclindex = (int *)malloc(config.maxnbclust * sizeof(int));
    state.clmembflag = (int *)malloc(config.maxnbclust * sizeof(int));

    // Run Clustering
    run_clustering(&config, &state);

    if (state.distall_out) fclose(state.distall_out);

    // Write Results
    write_results(&config, &state);

    // Cleanup
    for (int i = 0; i < state.num_clusters; i++) {
        if (state.clusters[i].anchor.data) free(state.clusters[i].anchor.data);
    }
    free(state.clusters);

    for (long i = 0; i < state.total_frames_processed; i++) {
        if (state.frame_infos[i].cluster_indices) free(state.frame_infos[i].cluster_indices);
        if (state.frame_infos[i].distances) free(state.frame_infos[i].distances);
    }
    free(state.frame_infos);

    for (int i = 0; i < config.maxnbclust; i++) {
        if (state.cluster_visitors[i].frames) free(state.cluster_visitors[i].frames);
    }
    free(state.cluster_visitors);
    free(state.current_gprobs);

    free(state.dccarray);
    free(state.probsortedclindex);
    free(state.clmembflag);
    free(state.assignments);

    if (state.pruned_fraction_sum) free(state.pruned_fraction_sum);
    if (state.step_counts) free(state.step_counts);
    if (state.transition_matrix) free(state.transition_matrix);
    if (state.mixed_probs) free(state.mixed_probs);

    if (config.user_outdir && out_dir_alloc) free(config.user_outdir);

    close_frameread();

    return 0;
}
