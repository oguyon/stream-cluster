#ifndef CLUSTER_IO_H
#define CLUSTER_IO_H

#include "cluster_defs.h"

char* create_output_dir_name(const char* input_file);
void print_usage(char *progname);
void write_results(ClusterConfig *config, ClusterState *state);

#endif // CLUSTER_IO_H
