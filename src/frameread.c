#include "common.h"
#include <fitsio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static fitsfile *fptr = NULL;
static FILE *ascii_ptr = NULL;
static long *ascii_line_offsets = NULL;
static int is_ascii_mode = 0;

// FFmpeg State
static AVFormatContext *fmt_ctx = NULL;
static AVCodecContext *dec_ctx = NULL;
static int video_stream_idx = -1;
static AVFrame *frame = NULL;
static AVPacket *pkt = NULL;
static struct SwsContext *sws_ctx = NULL;
static int is_mp4_mode = 0;

static long num_frames = 0;
static long frame_width = 0;
static long frame_height = 0;
static int current_frame_idx = 0;

Frame* getframe_at(long index);

int is_ascii_input_mode() {
    return is_ascii_mode;
}

static int init_ascii(char *filename) {
    ascii_ptr = fopen(filename, "r");
    if (!ascii_ptr) {
        perror("Failed to open ASCII file");
        return -1;
    }

    is_ascii_mode = 1;
    num_frames = 0;

    size_t capacity = 1024;
    ascii_line_offsets = (long *)malloc(capacity * sizeof(long));
    if (!ascii_line_offsets) {
        perror("Memory allocation failed");
        return -1;
    }

    char *line = NULL;
    size_t len = 0;
    long offset = ftell(ascii_ptr);

    int first_line = 1;

    while (getline(&line, &len, ascii_ptr) != -1) {
        if (num_frames >= capacity) {
            capacity *= 2;
            long *new_offsets = (long *)realloc(ascii_line_offsets, capacity * sizeof(long));
            if (!new_offsets) {
                perror("Memory reallocation failed");
                free(line);
                return -1;
            }
            ascii_line_offsets = new_offsets;
        }
        ascii_line_offsets[num_frames] = offset;
        num_frames++;

        if (first_line) {
            int cols = 0;
            char *p = line;
            int in_num = 0;
            while (*p) {
                if (!isspace((unsigned char)*p)) {
                    if (!in_num) {
                        cols++;
                        in_num = 1;
                    }
                } else {
                    in_num = 0;
                }
                p++;
            }
            frame_width = cols;
            frame_height = 1;
            first_line = 0;
        }

        offset = ftell(ascii_ptr);
    }

    free(line);

    if (num_frames == 0) {
        fprintf(stderr, "Error: Empty ASCII file.\n");
        return -1;
    }

    rewind(ascii_ptr);
    return 0;
}

static int init_mp4(char *filename) {
    if (avformat_open_input(&fmt_ctx, filename, NULL, NULL) < 0) {
        fprintf(stderr, "Could not open video file %s\n", filename);
        return -1;
    }

    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        return -1;
    }

    video_stream_idx = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            break;
        }
    }

    if (video_stream_idx == -1) {
        fprintf(stderr, "Could not find video stream\n");
        return -1;
    }

    AVCodecParameters *codecpar = fmt_ctx->streams[video_stream_idx]->codecpar;
    AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        fprintf(stderr, "Codec not found\n");
        return -1;
    }

    dec_ctx = avcodec_alloc_context3(codec);
    if (!dec_ctx) {
        fprintf(stderr, "Could not allocate video codec context\n");
        return -1;
    }

    if (avcodec_parameters_to_context(dec_ctx, codecpar) < 0) {
        fprintf(stderr, "Failed to copy codec parameters to decoder context\n");
        return -1;
    }

    if (avcodec_open2(dec_ctx, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        return -1;
    }

    frame = av_frame_alloc();
    pkt = av_packet_alloc();
    if (!frame || !pkt) {
        fprintf(stderr, "Could not allocate frame or packet\n");
        return -1;
    }

    frame_width = dec_ctx->width;
    // Flattened RGB is width * height * 3
    // But internally we store vector. If we want image-like structure, we have width and height.
    // However, distance func treats data as double array of size w*h.
    // If we have 3 channels, effective vector length is w*h*3.
    // frameread interface exposes get_frame_width/height.
    // If we return w*3 as width, or keep height as h.
    // Let's set dimensions to pixel dimensions, but data size will be w*h*3.
    // Wait, getframe allocates width*height doubles.
    // We should set frame_width = w * 3? Or frame_height = h * 3?
    // Let's use frame_height = h, frame_width = w * 3 (interleaved).
    frame_width = dec_ctx->width * 3;
    frame_height = dec_ctx->height;

    // We don't easily know total frame count without scanning.
    // ffmpeg estimates can be wrong.
    // If nb_frames is available in stream
    if (fmt_ctx->streams[video_stream_idx]->nb_frames > 0) {
        num_frames = fmt_ctx->streams[video_stream_idx]->nb_frames;
    } else {
        // Fallback: Estimate from duration and frame rate
        double duration = (double)fmt_ctx->duration / AV_TIME_BASE;
        double fps = av_q2d(fmt_ctx->streams[video_stream_idx]->avg_frame_rate);
        if (duration > 0 && fps > 0) num_frames = (long)(duration * fps);
        else num_frames = 10000; // Arbitrary limit or force scan?
        // Let's just warn.
        printf("Warning: Could not determine exact frame count. Using estimated %ld\n", num_frames);
    }

    is_mp4_mode = 1;

    // Prepare scaler for RGB24
    sws_ctx = sws_getContext(dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
                             dec_ctx->width, dec_ctx->height, AV_PIX_FMT_RGB24,
                             SWS_BILINEAR, NULL, NULL, NULL);

    return 0;
}

int init_frameread(char *filename) {
    // Check extension
    char *ext = strrchr(filename, '.');
    if (ext) {
        if (strcmp(ext, ".txt") == 0) return init_ascii(filename);
        if (strcmp(ext, ".mp4") == 0 || strcmp(ext, ".avi") == 0 || strcmp(ext, ".mov") == 0 || strcmp(ext, ".mkv") == 0) {
            return init_mp4(filename);
        }
    }

    int status = 0;
    if (fits_open_file(&fptr, filename, READONLY, &status)) {
        fits_report_error(stderr, status);
        return -1;
    }

    int naxis;
    long naxes[3];
    if (fits_get_img_dim(fptr, &naxis, &status) || fits_get_img_size(fptr, 3, naxes, &status)) {
        fits_report_error(stderr, status);
        return -1;
    }

    if (naxis == 3) {
        frame_width = naxes[0];
        frame_height = naxes[1];
        num_frames = naxes[2];
    } else if (naxis == 2) {
        frame_width = naxes[0];
        frame_height = naxes[1];
        num_frames = 1;
    } else {
        fprintf(stderr, "Error: Input FITS must be 2D or 3D.\n");
        return -1;
    }

    current_frame_idx = 0;
    return 0;
}

Frame* getframe() {
    if (current_frame_idx >= num_frames) {
        return NULL;
    }
    return getframe_at(current_frame_idx++);
}

// FFmpeg random access is slow (seek).
// We optimize for sequential access.
// If index != current_internal_index, we seek.
static int internal_mp4_index = 0;

Frame* getframe_at(long index) {
     if (index >= num_frames || index < 0) {
        return NULL;
    }

    long nelements = frame_width * frame_height;
    Frame *frame_struct = (Frame *)malloc(sizeof(Frame));
    if (!frame_struct) return NULL;

    frame_struct->width = frame_width;
    frame_struct->height = frame_height;
    frame_struct->id = index;
    frame_struct->data = (double *)malloc(nelements * sizeof(double));

    if (!frame_struct->data) {
        free(frame_struct);
        return NULL;
    }

    if (is_ascii_mode) {
        if (fseek(ascii_ptr, ascii_line_offsets[index], SEEK_SET) != 0) {
            perror("fseek failed");
            free(frame_struct->data);
            free(frame_struct);
            return NULL;
        }
        for (long i = 0; i < nelements; i++) {
            if (fscanf(ascii_ptr, "%lf", &frame_struct->data[i]) != 1) {
                free(frame_struct->data);
                free(frame_struct);
                return NULL;
            }
        }
    } else if (is_mp4_mode) {
        // Handle seeking if necessary
        if (index != internal_mp4_index) {
            // Seek logic here (simplified)
            // av_seek_frame(fmt_ctx, video_stream_idx, timestamp, flags);
            // Re-flush buffers
            // This is complex to get frame-accurate.
            // For clustering (sequential access usually), we assume sequential.
            // If random access requested (e.g. during output writing), we might need to re-read or cache.
            // Given constraints, we warn or fail on backward seek?
            // "reset_frameread" handles rewinding (seek to 0).
            if (index < internal_mp4_index) {
                // Rewind
                av_seek_frame(fmt_ctx, video_stream_idx, 0, AVSEEK_FLAG_BACKWARD);
                avcodec_flush_buffers(dec_ctx);
                internal_mp4_index = 0;
            }
            // Fast forward
            while (internal_mp4_index < index) {
                if (av_read_frame(fmt_ctx, pkt) < 0) break;
                if (pkt->stream_index == video_stream_idx) {
                    if (avcodec_send_packet(dec_ctx, pkt) == 0) {
                        while (avcodec_receive_frame(dec_ctx, frame) == 0) {
                            internal_mp4_index++;
                        }
                    }
                }
                av_packet_unref(pkt);
            }
        }

        // Read next frame
        int ret = 0;
        int frame_decoded = 0;
        while (ret >= 0 && !frame_decoded) {
            ret = av_read_frame(fmt_ctx, pkt);
            if (ret < 0) break;
            if (pkt->stream_index == video_stream_idx) {
                if (avcodec_send_packet(dec_ctx, pkt) == 0) {
                    if (avcodec_receive_frame(dec_ctx, frame) == 0) {
                        frame_decoded = 1;
                        internal_mp4_index++;
                    }
                }
            }
            av_packet_unref(pkt);
        }

        if (!frame_decoded) {
            free(frame_struct->data);
            free(frame_struct);
            return NULL;
        }

        // Convert to RGB24 and then to double
        // frame->data is YUV likely.
        // We use swscale to convert to packed RGB in a temp buffer
        uint8_t *rgb_data[4] = {NULL};
        int rgb_linesize[4] = {0};
        av_image_alloc(rgb_data, rgb_linesize, dec_ctx->width, dec_ctx->height, AV_PIX_FMT_RGB24, 1);

        sws_scale(sws_ctx, (const uint8_t *const *)frame->data, frame->linesize, 0, dec_ctx->height, rgb_data, rgb_linesize);

        // Copy to double buffer
        // rgb_data[0] contains W*H*3 bytes
        // nelements = frame_width * frame_height = (W*3) * H
        uint8_t *src = rgb_data[0];
        for (long i = 0; i < nelements; i++) {
            frame_struct->data[i] = (double)src[i];
        }

        av_freep(&rgb_data[0]);

    } else {
        int status = 0;
        long fpixel[3] = {1, 1, index + 1};
        if (fits_read_pix(fptr, TDOUBLE, fpixel, nelements, NULL, frame_struct->data, NULL, &status)) {
            fits_report_error(stderr, status);
            free(frame_struct->data);
            free(frame_struct);
            return NULL;
        }
    }

    return frame_struct;
}

void free_frame(Frame *frame) {
    if (frame) {
        if (frame->data) free(frame->data);
        free(frame);
    }
}

void close_frameread() {
    if (is_ascii_mode) {
        if (ascii_ptr) fclose(ascii_ptr);
        if (ascii_line_offsets) free(ascii_line_offsets);
        ascii_ptr = NULL;
        ascii_line_offsets = NULL;
        is_ascii_mode = 0;
    } else if (is_mp4_mode) {
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        av_frame_free(&frame);
        av_packet_free(&pkt);
        sws_freeContext(sws_ctx);
        is_mp4_mode = 0;
    } else {
        int status = 0;
        if (fptr) {
            fits_close_file(fptr, &status);
            fptr = NULL;
        }
    }
}

void reset_frameread() {
    current_frame_idx = 0;
    if (is_mp4_mode) {
        av_seek_frame(fmt_ctx, video_stream_idx, 0, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(dec_ctx);
        internal_mp4_index = 0;
    }
}

long get_num_frames() {
    return num_frames;
}

long get_frame_width() {
    return frame_width;
}

long get_frame_height() {
    return frame_height;
}
