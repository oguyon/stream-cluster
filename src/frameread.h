#ifndef FRAMEREAD_H
#define FRAMEREAD_H

#include "common.h"

int init_frameread(char *filename, int stream_mode);
Frame* getframe();
Frame* getframe_at(long index);
void free_frame(Frame *frame);
void close_frameread();
void reset_frameread();
long get_num_frames();
long get_missed_frames();
long get_stream_read_slice();
long get_stream_write_slice();
long get_stream_lag();
int is_3d_stream_mode();
double get_stream_wait_time();
long get_frame_width();
long get_frame_height();
int is_ascii_input_mode();

#endif // FRAMEREAD_H
