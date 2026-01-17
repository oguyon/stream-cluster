#ifndef CLUSTER_DEFS_H
#define CLUSTER_DEFS_H

#include <stdio.h>
#include <signal.h>
#include "common.h"

// Max Cluster Strategy Enum
typedef enum {
    MAXCL_STOP = 0,
    MAXCL_DISCARD = 1,
    MAXCL_MERGE = 2
} MaxClustStrategy;

// Configuration structure
typedef struct {
    double rlim;
    int auto_rlim_mode;
    double auto_rlim_factor;
    double deltaprob;
    int maxnbclust;
    int ncpu; // Number of CPUs/threads
    long maxnbfr;
    char *fits_filename;
    char *user_outdir;
    int scandist_mode;
    int progress_mode;
    int average_mode;
    int distall_mode;
    int gprob_mode;
    int verbose_level;
    int fitsout_mode;
    int pngout_mode;
    int stream_input_mode; // Added for ImageStreamIO
    int cnt2sync_mode; // Added for cnt2 sync
    double fmatch_a;
    double fmatch_b;
    int max_gprob_visitors;
    int pred_mode;
    int pred_len;
    int pred_h;
    int pred_n;
    int te4_mode;
    int te5_mode;
    double tm_mixing_coeff;
    MaxClustStrategy maxcl_strategy;
    double discard_fraction;
    
    // Output control flags
    int output_dcc;
    int output_tm;
    int output_anchors;
    int output_counts;
    int output_membership;
    int output_discarded;
    int output_clustered;
    int output_clusters; // Controls cluster_X files
} ClusterConfig;

// VisitorList structure
typedef struct {
    int *frames;
    int count;
    int capacity;
} VisitorList;

// State structure
typedef struct {
    Cluster *clusters;
    VisitorList *cluster_visitors;
    double *current_gprobs;
    double *dccarray; // 1D array simulating 2D: [i*maxNcl + j]
    int *probsortedclindex;
    int *clmembflag;
    int num_clusters;
    long framedist_calls;
    long clusters_pruned;
    int *assignments;
    FrameInfo *frame_infos;
    long total_frames_processed;
    long total_missed_frames; // Added for streaming stats
    FILE *distall_out;
    double *pruned_fraction_sum;
    long *step_counts;
    int max_steps_recorded;
    long *transition_matrix;
    double *mixed_probs;
    long *dist_counts; // Histogram of distance counts
    long *pruned_counts_by_dist; // Histogram of pruned counts
} ClusterState;

// Candidate structure for sorting
typedef struct {
    int id;
    double p;
} Candidate;

#endif // CLUSTER_DEFS_H
