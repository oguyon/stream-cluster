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

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
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
                 return 1;
            }
            config.auto_rlim_mode = 1;
            arg_idx = 2;
        } else if (argv[1][0] == '-') {
            fprintf(stderr, "Error: First argument must be rlim (numerical value) or auto-rlim (a<val>), found option: %s\n", argv[1]);
            print_usage(argv[0]);
            return 1;
        } else {
            char *endptr;
            config.rlim = strtod(argv[1], &endptr);
            if (*endptr != '\0') {
                 fprintf(stderr, "Error: Invalid rlim value: %s\n", argv[1]);
                 return 1;
            }
            arg_idx = 2;
        }
    } else {
        arg_idx = 1;
    }

    while (arg_idx < argc) {
        if (strcmp(argv[arg_idx], "-dprob") == 0) {
            if (arg_idx + 1 >= argc) {
                fprintf(stderr, "Error: Missing value for option -dprob\n");
                return 1;
            }
            config.deltaprob = atof(argv[++arg_idx]);
        } else if (strcmp(argv[arg_idx], "-maxcl") == 0) {
            if (arg_idx + 1 >= argc) {
                fprintf(stderr, "Error: Missing value for option -maxcl\n");
                return 1;
            }
            config.maxnbclust = atoi(argv[++arg_idx]);
        } else if (strcmp(argv[arg_idx], "-maxim") == 0) {
            if (arg_idx + 1 >= argc) {
                fprintf(stderr, "Error: Missing value for option -maxim\n");
                return 1;
            }
            config.maxnbfr = atol(argv[++arg_idx]);
        } else if (strcmp(argv[arg_idx], "-avg") == 0) {
            config.average_mode = 1;
        } else if (strcmp(argv[arg_idx], "-distall") == 0) {
            config.distall_mode = 1;
        } else if (strcmp(argv[arg_idx], "-outdir") == 0) {
            if (arg_idx + 1 >= argc) {
                fprintf(stderr, "Error: Missing value for option -outdir\n");
                return 1;
            }
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
        } else if (strcmp(argv[arg_idx], "-fmatcha") == 0) {
            if (arg_idx + 1 >= argc) {
                fprintf(stderr, "Error: Missing value for option -fmatcha\n");
                return 1;
            }
            config.fmatch_a = atof(argv[++arg_idx]);
        } else if (strcmp(argv[arg_idx], "-fmatchb") == 0) {
            if (arg_idx + 1 >= argc) {
                fprintf(stderr, "Error: Missing value for option -fmatchb\n");
                return 1;
            }
            config.fmatch_b = atof(argv[++arg_idx]);
        } else if (strcmp(argv[arg_idx], "-scandist") == 0) {
            // Already handled
        } else if (argv[arg_idx][0] == '-') {
            fprintf(stderr, "Error: Unknown option: %s\n", argv[arg_idx]);
            print_usage(argv[0]);
            return 1;
        } else {
            if (config.fits_filename != NULL) {
                fprintf(stderr, "Error: Too many arguments or multiple input files specified (already have '%s', found '%s')\n", config.fits_filename, argv[arg_idx]);
                return 1;
            }
            config.fits_filename = argv[arg_idx];
        }
        arg_idx++;
    }

    if (!config.fits_filename) {
        fprintf(stderr, "Error: Missing input file.\n");
        if (!config.scandist_mode) print_usage(argv[0]);
        return 1;
    }

    if (init_frameread(config.fits_filename) != 0) {
        return 1;
    }

    // Determine output directory
    char *out_dir = NULL;
    if (config.user_outdir) {
        out_dir = strdup(config.user_outdir);
    } else {
        out_dir = create_output_dir_name(config.fits_filename);
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

    // If we didn't have user_outdir set, we might want to store the auto-generated one in config
    // so other functions can use it if they rely on config.user_outdir.
    // However, logic in other files currently regenerates it if user_outdir is NULL.
    // It's safer to set it in config.
    if (!config.user_outdir) {
        config.user_outdir = out_dir; // Assume ownership or simple pointer?
        // `out_dir` was malloced by create_output_dir_name.
        // We can just assign it.
    } else {
        // user_outdir is argv pointer, out_dir is malloced strdup.
        // We can free out_dir here since we just used it to mkdir.
        free(out_dir);
        // But wait, if we used create_output_dir_name, we want to keep it.
    }

    ClusterState state;
    memset(&state, 0, sizeof(ClusterState));

    if (config.distall_mode) {
        char out_path[1024];
        if (config.user_outdir)
            snprintf(out_path, sizeof(out_path), "%s/distall.txt", config.user_outdir);
        else {
             // Should not happen if we handled it above
             char *tmp = create_output_dir_name(config.fits_filename);
             snprintf(out_path, sizeof(out_path), "%s/distall.txt", tmp);
             free(tmp);
        }
        state.distall_out = fopen(out_path, "w");
        if (!state.distall_out) {
            perror("Failed to open distall.txt in output directory");
            return 1;
        }

        fprintf(state.distall_out, "# rlim: %.6f\n", config.rlim);
        fprintf(state.distall_out, "# dprob: %.6f\n", config.deltaprob);
        fprintf(state.distall_out, "# maxcl: %d\n", config.maxnbclust);
        fprintf(state.distall_out, "# maxim: %ld\n", config.maxnbfr);
        fprintf(state.distall_out, "# filename: %s\n", config.fits_filename);
        if (config.user_outdir) fprintf(state.distall_out, "# outdir: %s\n", config.user_outdir);
        fprintf(state.distall_out, "# scandist_mode: %d\n", config.scandist_mode);
        fprintf(state.distall_out, "# auto_rlim_mode: %d\n", config.auto_rlim_mode);
        fprintf(state.distall_out, "# Columns: Frame1_ID Frame2_ID Distance Ratio(D/rlim) Cluster_ID Cluster_Prob GProb\n");
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
             // Clean up if we allocated config.user_outdir
             if (config.user_outdir && config.user_outdir != argv[arg_idx]) {
                 // It's hard to track if it's from argv or malloc without a flag.
                 // But we know if we used create_output_dir_name it was malloced.
                 // Let's just rely on OS cleanup for main exit.
             }
             return 0;
        }
        // If auto_rlim_mode, reset for clustering
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

    // config.user_outdir might need free if we malloced it.
    // If it came from argv, no free.
    // If we want to be clean, we should track it.
    // But for now, we leave it.

    close_frameread();

    return 0;
}
