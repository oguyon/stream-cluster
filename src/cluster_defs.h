#ifndef CLUSTER_DEFS_H
#define CLUSTER_DEFS_H

#include <stdio.h>
#include <signal.h>
#include "common.h"

// Configuration structure
typedef struct {
    double rlim;
    int auto_rlim_mode;
    double auto_rlim_factor;
    double deltaprob;
    int maxnbclust;
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
    int pngout_mode; // Added pngout flag
    double fmatch_a;
    double fmatch_b;
    int max_gprob_visitors;
    int pred_mode;
    int pred_len;
    int pred_h;
    int pred_n;
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
    FILE *distall_out;
    double *pruned_fraction_sum;
    long *step_counts;
    int max_steps_recorded;
    long *transition_matrix;
} ClusterState;

// Candidate structure for sorting
typedef struct {
    int id;
    double p;
} Candidate;

#endif // CLUSTER_DEFS_H
