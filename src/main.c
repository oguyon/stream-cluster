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
    config.maxnbfr = 100000;
    config.fmatch_a = 2.0;
    config.fmatch_b = 0.5;
    config.progress_mode = 1;

    // First pass: Detect -scandist
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-scandist") == 0) {
            config.scandist_mode = 1;
            break;
        }
    }

    int arg_idx = 1;

    if (!config.scandist_mode) {
        if (argv[1][0] == 'a') {
            char *endptr;
            config.auto_rlim_factor = strtod(argv[1] + 1, &endptr);
            if (*endptr != '\0') {
                 fprintf(stderr, "Error: Invalid format for auto-rlim. Expected 'a<float>', got '%s'\n", argv[1]);
                 print_args_on_error(argc, argv);
                 return 1;
            }
            config.auto_rlim_mode = 1;
            arg_idx = 2;
        } else if (argv[1][0] == '-') {
            fprintf(stderr, "Error: First argument must be rlim (numerical value) or auto-rlim (a<val>), found option: %s\n", argv[1]);
            print_usage(argv[0]);
            print_args_on_error(argc, argv);
            return 1;
        } else {
            char *endptr;
            config.rlim = strtod(argv[1], &endptr);
            if (*endptr != '\0') {
                 fprintf(stderr, "Error: Invalid rlim value: %s\n", argv[1]);
                 print_args_on_error(argc, argv);
                 return 1;
            }
            arg_idx = 2;
        }
    } else {
        arg_idx = 1;
    }

    while (arg_idx < argc) {
        if (strcmp(argv[arg_idx], "-dprob") == 0) {
            config.deltaprob = atof(argv[++arg_idx]);
        } else if (strcmp(argv[arg_idx], "-maxcl") == 0) {
            config.maxnbclust = atoi(argv[++arg_idx]);
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
        } else if (strcmp(argv[arg_idx], "-fmatcha") == 0) {
            config.fmatch_a = atof(argv[++arg_idx]);
        } else if (strcmp(argv[arg_idx], "-fmatchb") == 0) {
            config.fmatch_b = atof(argv[++arg_idx]);
        } else if (strcmp(argv[arg_idx], "-scandist") == 0) {
            // Already handled
        } else if (argv[arg_idx][0] == '-') {
            fprintf(stderr, "Error: Unknown option: %s\n", argv[arg_idx]);
            print_usage(argv[0]);
            print_args_on_error(argc, argv);
            return 1;
        } else {
            if (config.fits_filename != NULL) {
                fprintf(stderr, "Error: Too many arguments or multiple input files specified (already have '%s', found '%s')\n", config.fits_filename, argv[arg_idx]);
                print_args_on_error(argc, argv);
                return 1;
            }
            config.fits_filename = argv[arg_idx];
        }
        arg_idx++;
    }

    if (!config.fits_filename) {
        fprintf(stderr, "Error: Missing input file.\n");
        if (!config.scandist_mode) print_usage(argv[0]);
        print_args_on_error(argc, argv);
        return 1;
    }

    if (init_frameread(config.fits_filename) != 0) {
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

    if (config.user_outdir && out_dir_alloc) free(config.user_outdir);

    close_frameread();

    return 0;
}
