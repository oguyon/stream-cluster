#ifndef FRAMEREAD_H
#define FRAMEREAD_H

#include "common.h"

int init_frameread(char *filename);
Frame* getframe();
Frame* getframe_at(long index);
void free_frame(Frame *frame);
void close_frameread();
void reset_frameread();
long get_num_frames();
long get_frame_width();
long get_frame_height();
int is_ascii_input_mode();

#endif // FRAMEREAD_H
