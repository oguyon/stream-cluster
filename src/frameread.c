#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef USE_CFITSIO
#include <fitsio.h>
#endif

#ifdef USE_FFMPEG
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#endif

#ifdef USE_CFITSIO
static fitsfile *fptr = NULL;
#endif

static FILE *ascii_ptr = NULL;
static long *ascii_line_offsets = NULL;
static int is_ascii_mode = 0;

// FFmpeg State
#ifdef USE_FFMPEG
static AVFormatContext *fmt_ctx = NULL;
static AVCodecContext *dec_ctx = NULL;
static int video_stream_idx = -1;
static AVFrame *frame = NULL;
static AVPacket *pkt = NULL;
static struct SwsContext *sws_ctx = NULL;
static int is_mp4_mode = 0;
// Seeking state
static int internal_mp4_index = 0;
#endif

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

#ifdef USE_FFMPEG
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
    const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
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

    // Interleaved RGB
    frame_width = dec_ctx->width * 3;
    frame_height = dec_ctx->height;

    if (fmt_ctx->streams[video_stream_idx]->nb_frames > 0) {
        num_frames = fmt_ctx->streams[video_stream_idx]->nb_frames;
    } else {
        // Fallback estimate
        double duration = (double)fmt_ctx->duration / AV_TIME_BASE;
        double fps = av_q2d(fmt_ctx->streams[video_stream_idx]->avg_frame_rate);
        if (duration > 0 && fps > 0) num_frames = (long)(duration * fps);
        else num_frames = 10000;
        printf("Warning: Could not determine exact frame count. Using estimated %ld\n", num_frames);
    }

    is_mp4_mode = 1;

    // Prepare scaler for RGB24
    sws_ctx = sws_getContext(dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
                             dec_ctx->width, dec_ctx->height, AV_PIX_FMT_RGB24,
                             SWS_BILINEAR, NULL, NULL, NULL);

    return 0;
}
#endif

int init_frameread(char *filename) {
    // Check extension
    char *ext = strrchr(filename, '.');
    if (ext) {
        if (strcmp(ext, ".txt") == 0) return init_ascii(filename);
        if (strcmp(ext, ".mp4") == 0 || strcmp(ext, ".avi") == 0 || strcmp(ext, ".mov") == 0 || strcmp(ext, ".mkv") == 0) {
            #ifdef USE_FFMPEG
            return init_mp4(filename);
            #else
            fprintf(stderr, "Error: FFmpeg support is not compiled in. Cannot read video file.\n");
            return -1;
            #endif
        }
    }

    #ifdef USE_CFITSIO
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
    #else
    fprintf(stderr, "Error: FITS support is not compiled in. Cannot read file %s. ASCII (.txt) supported.\n", filename);
    return -1;
    #endif
}

Frame* getframe() {
    if (current_frame_idx >= num_frames) {
        return NULL;
    }
    return getframe_at(current_frame_idx++);
}

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
    }
    #ifdef USE_FFMPEG
    else if (is_mp4_mode) {
        // Handle seeking if necessary
        if (index != internal_mp4_index) {
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

        uint8_t *rgb_data[4] = {NULL};
        int rgb_linesize[4] = {0};

        rgb_data[0] = (uint8_t*)malloc(dec_ctx->width * dec_ctx->height * 3);
        rgb_linesize[0] = dec_ctx->width * 3;

        sws_scale(sws_ctx, (const uint8_t *const *)frame->data, frame->linesize, 0, dec_ctx->height, rgb_data, rgb_linesize);

        uint8_t *src = rgb_data[0];
        for (long i = 0; i < nelements; i++) {
            frame_struct->data[i] = (double)src[i];
        }

        free(rgb_data[0]);

    }
    #endif
    #ifdef USE_CFITSIO
    else if (fptr) { // Check if fptr is valid, implying FITS mode
        int status = 0;
        long fpixel[3] = {1, 1, index + 1};
        if (fits_read_pix(fptr, TDOUBLE, fpixel, nelements, NULL, frame_struct->data, NULL, &status)) {
            fits_report_error(stderr, status);
            free(frame_struct->data);
            free(frame_struct);
            return NULL;
        }
    }
    #endif
    else {
        // Should not happen if init checked modes correctly
        free(frame_struct->data);
        free(frame_struct);
        return NULL;
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
    }
    #ifdef USE_FFMPEG
    else if (is_mp4_mode) {
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        av_frame_free(&frame);
        av_packet_free(&pkt);
        sws_freeContext(sws_ctx);
        is_mp4_mode = 0;
    }
    #endif
    #ifdef USE_CFITSIO
    else {
        int status = 0;
        if (fptr) {
            fits_close_file(fptr, &status);
            fptr = NULL;
        }
    }
    #endif
}

void reset_frameread() {
    current_frame_idx = 0;
    #ifdef USE_FFMPEG
    if (is_mp4_mode) {
        av_seek_frame(fmt_ctx, video_stream_idx, 0, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(dec_ctx);
        internal_mp4_index = 0;
    }
    #endif
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
