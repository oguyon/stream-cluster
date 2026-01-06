#ifndef CLUSTER_CORE_H
#define CLUSTER_CORE_H

#include "cluster_defs.h"

extern volatile sig_atomic_t stop_requested;

void run_clustering(ClusterConfig *config, ClusterState *state);
void run_scandist(ClusterConfig *config, char *out_dir);

int compare_candidates(const void *a, const void *b);
int compare_probs(const void *a, const void *b);
int compare_doubles(const void *a, const void *b);

#endif // CLUSTER_CORE_H
