#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>

#ifdef USE_IMAGESTREAMIO
#include <ImageStreamIO/ImageStruct.h>
#include <ImageStreamIO/ImageStreamIO.h>
#endif

void clamp(int *val) {
    if (*val < 0) *val = 0;
    if (*val > 255) *val = 255;
}

void print_help(const char *progname) {
    printf("Usage: %s [options] <pixel_size> <alpha> <input.txt> <output> [noise_level] [max_frames]\n\n", progname);
    printf("Description:\n");
    printf("  Converts a coordinate text file into an MP4 video sequence or ImageStreamIO stream.\n");
    printf("  Each line in the input file corresponds to one video frame.\n\n");
    printf("  The input text files can be generated using the 'image-cluster-mktxtseq' program.\n");
    printf("  Lines starting with '#' or empty lines are ignored.\n\n");
    printf("  Input Format:\n");
    printf("    x y [z]\n");
    printf("    Coordinates are expected in the range [-1.5, 1.5].\n");
    printf("    2D Mode: If only x and y are provided, z defaults to 1.0.\n");
    printf("    3D Mode: If z is provided, it scales the spot size.\n\n");
    printf("  Spot Generation:\n");
    printf("    For each frame, a Gaussian spot is drawn at the specified (x, y) coordinates.\n");
    printf("    The coordinate system is mapped to the output pixel grid:\n");
    printf("      (-1.5, -1.5) -> Bottom-Left\n");
    printf("      ( 1.5,  1.5) -> Top-Right\n");
    printf("    The size of the spot is determined by 'alpha' and the optional 'z' coordinate:\n");
    printf("      diameter = pixel_size * alpha * (z + 1.5)\n");
    printf("    The spot intensity follows a Gaussian profile with the calculated diameter.\n\n");
    printf("Arguments:\n");
    printf("  <pixel_size>   Width and height of the square output video/stream in pixels.\n");
    printf("  <alpha>        Scaling factor for the Gaussian spot size relative to the frame size.\n");
    printf("  <input.txt>    Input text file containing coordinates (x y [z]).\n");
    printf("  <output>       Output filename (MP4) or stream name (if -isio is used).\n");
    printf("  [noise_level]  (Optional) Amplitude of random noise (0-255). Default: 0.0\n");
    printf("  [max_frames]   (Optional) Max frames to process.\n\n");
    printf("Options:\n");
    printf("  -h, --help     Show this help message.\n");
    printf("  -isio          Write to an ImageStreamIO stream instead of MP4.\n");
    printf("  -fps <val>     Set frame rate (frames per second). Controls wait time in stream mode.\n");
    printf("  -cnt2sync      Enable PROCESSINFO_TRIGGERMODE_CNT2 synchronization (wait for cnt0 < cnt2).\n");
}

int main(int argc, char *argv[]) {
    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_help(argv[0]);
        return 0;
    }

    int size = 0;
    double alpha = 0.0;
    char *input_file = NULL;
    char *output_file = NULL;
    double noise_level = 0.0;
    int max_frames = -1;
    
    int isio_mode = 0;
    double fps = 0.0;
    int cnt2sync = 0;

    // Parse Arguments
    int positional_idx = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-isio") == 0) {
            isio_mode = 1;
        } else if (strcmp(argv[i], "-cnt2sync") == 0) {
            cnt2sync = 1;
        } else if (strcmp(argv[i], "-fps") == 0) {
            if (i + 1 < argc) {
                fps = atof(argv[++i]);
            } else {
                fprintf(stderr, "Error: -fps requires an argument.\n");
                return 1;
            }
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_help(argv[0]);
            return 1;
        } else {
            // Positional
            switch (positional_idx) {
                case 0: size = atoi(argv[i]); break;
                case 1: alpha = atof(argv[i]); break;
                case 2: input_file = argv[i]; break;
                case 3: output_file = argv[i]; break;
                case 4: noise_level = atof(argv[i]); break;
                case 5: max_frames = atoi(argv[i]); break;
                default: break; // Ignore extra args
            }
            positional_idx++;
        }
    }

    if (positional_idx < 4) {
        fprintf(stderr, "Error: Missing required arguments.\n");
        print_help(argv[0]);
        return 1;
    }

    if (size <= 0) {
        fprintf(stderr, "Error: pixel_size must be positive.\n");
        return 1;
    }

    FILE *fin = fopen(input_file, "r");
    if (!fin) {
        fprintf(stderr, "Error: Cannot open input file %s\n", input_file);
        return 1;
    }

    FILE *pipe = NULL;
    
    #ifdef USE_IMAGESTREAMIO
    IMAGE stream_image;
    float *stream_buffer = NULL; // Assuming float buffer for stream
    #endif

    unsigned char *frame_rgb = NULL; // For MP4

    if (isio_mode) {
        #ifdef USE_IMAGESTREAMIO
        // Create Stream
        // 2D Stream of floats
        uint32_t dims[2] = {(uint32_t)size, (uint32_t)size};
        
        // Use 2 dims for circular buffer
        if (ImageStreamIO_createIm(&stream_image, output_file, 2, dims, _DATATYPE_FLOAT, 1, 1, 1) != 0) {
            fprintf(stderr, "Error: Failed to create ImageStreamIO stream %s\n", output_file);
            fclose(fin);
            return 1;
        }
        fprintf(stdout, "Writing to stream: %s\n", output_file);
        stream_buffer = (float *)malloc(size * size * sizeof(float));
        if (!stream_buffer) {
             perror("Malloc failed");
             fclose(fin);
             return 1;
        }
        #else
        fprintf(stderr, "Error: ImageStreamIO support not compiled in.\n");
        fclose(fin);
        return 1;
        #endif
    } else {
        // Init FFmpeg
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), 
            "ffmpeg -y -f rawvideo -vcodec rawvideo -pix_fmt rgb24 -s %dx%d -r 30 -i - -c:v libx264 -pix_fmt yuv420p -crf 10 -preset slow \"%s\"", 
            size, size, output_file);

        fprintf(stderr, "Running: %s\n", cmd);
        pipe = popen(cmd, "w");
        if (!pipe) {
            fprintf(stderr, "Error: Cannot open pipe to ffmpeg.\n");
            fclose(fin);
            return 1;
        }
        frame_rgb = (unsigned char *)malloc(size * size * 3);
        if (!frame_rgb) {
            fprintf(stderr, "Error: Memory allocation failed.\n");
            fclose(fin);
            pclose(pipe);
            return 1;
        }
    }

    srand(time(NULL));
    char line[1024];
    int frame_count = 0;
    long long us_per_frame = (fps > 0) ? (long long)(1000000.0 / fps) : 0;
    struct timespec last_time, now;
    clock_gettime(CLOCK_MONOTONIC, &last_time);

    while (fgets(line, sizeof(line), fin)) {
        if (max_frames > 0 && frame_count >= max_frames) break;
        if (line[0] == '#' || line[0] == '\n' || strlen(line) == 0) continue;

        double v1, v2, v3;
        int parsed = sscanf(line, "%lf %lf %lf", &v1, &v2, &v3);
        if (parsed < 2) continue;
        if (parsed == 2) v3 = 1.0;

        // Render Frame
        // Reuse logic but output to either frame_rgb or stream_buffer
        // Just compute values first?
        
        // Since we have two different buffers and types (uchar RGB vs float),
        // let's compute directly into destination to save copy.

        double cx_rel = (v1 + 1.5) / 3.0;
        double cy_rel = (v2 + 1.5) / 3.0;
        double cx = cx_rel * size;
        double cy = (1.0 - cy_rel) * size;
        double diameter = size * alpha * (v3 + 1.5);
        double sigma = diameter / 2.0;
        double two_sigma2 = 2.0 * sigma * sigma;
        int radius_bound = (int)ceil(4.0 * sigma);
        int min_x = (int)(cx - radius_bound);
        int max_x = (int)(cx + radius_bound);
        int min_y = (int)(cy - radius_bound);
        int max_y = (int)(cy + radius_bound);
        if (min_x < 0) min_x = 0;
        if (max_x >= size) max_x = size - 1;
        if (min_y < 0) min_y = 0;
        if (max_y >= size) max_y = size - 1;

        if (isio_mode) {
            #ifdef USE_IMAGESTREAMIO
            // Clear buffer
            memset(stream_buffer, 0, size * size * sizeof(float));
            
            for (int y = min_y; y <= max_y; y++) {
                for (int x = min_x; x <= max_x; x++) {
                    double dx = x - cx;
                    double dy = y - cy;
                    double dist2 = dx*dx + dy*dy;
                    double val_f = 255.0 * exp(-dist2 / two_sigma2);
                    if (val_f > 0) {
                        int idx = y * size + x;
                        stream_buffer[idx] = (float)val_f;
                    }
                }
            }
            if (noise_level > 0.0) {
                for (int i = 0; i < size * size; i++) {
                    double r = ((double)rand() / RAND_MAX) - 0.5;
                    float val = stream_buffer[i] + (float)(r * 2.0 * noise_level);
                    if (val < 0) val = 0;
                    if (val > 255) val = 255;
                    stream_buffer[i] = val;
                }
            }

            // Sync
            if (cnt2sync) {
                while (1) {
                    uint64_t c0 = stream_image.md[0].cnt0;
                    uint64_t c2 = stream_image.md[0].cnt2;
                    if (c0 < c2) break;
                    usleep(10); // Wait 10us
                }
            } else if (us_per_frame > 0) {
                // Rate limit
                clock_gettime(CLOCK_MONOTONIC, &now);
                long long elapsed_ns = (now.tv_sec - last_time.tv_sec) * 1000000000LL + (now.tv_nsec - last_time.tv_nsec);
                long long elapsed_us = elapsed_ns / 1000;
                if (elapsed_us < us_per_frame) {
                    usleep(us_per_frame - elapsed_us);
                }
                clock_gettime(CLOCK_MONOTONIC, &last_time);
            }

            // Write to Stream
            long nelements = size * size;
            float *dest = (float*)stream_image.array.F;
            memcpy(dest, stream_buffer, nelements * sizeof(float));
            
            stream_image.md[0].cnt0++;
            stream_image.md[0].cnt1 = 0;

            ImageStreamIO_sempost(&stream_image, -1);
            fprintf(stdout, "\rProcessed frame %d", frame_count + 1);
            fflush(stdout);
            #endif
        } else {
            // MP4 Mode (RGB)
            memset(frame_rgb, 0, size * size * 3);
            for (int y = min_y; y <= max_y; y++) {
                for (int x = min_x; x <= max_x; x++) {
                    double dx = x - cx;
                    double dy = y - cy;
                    double dist2 = dx*dx + dy*dy;
                    double val_f = 255.0 * exp(-dist2 / two_sigma2);
                    int val = (int)(val_f + 0.5);
                    if (val > 255) val = 255;
                    if (val > 0) {
                        int idx = (y * size + x) * 3;
                        frame_rgb[idx] = (unsigned char)val;
                        frame_rgb[idx+1] = (unsigned char)val;
                        frame_rgb[idx+2] = (unsigned char)val;
                    }
                }
            }
            if (noise_level > 0.0) {
                for (int i = 0; i < size * size * 3; i++) {
                    double r = ((double)rand() / RAND_MAX) - 0.5;
                    int noise_val = (int)(r * 2.0 * noise_level);
                    int val = frame_rgb[i] + noise_val;
                    clamp(&val);
                    frame_rgb[i] = (unsigned char)val;
                }
            }
            fwrite(frame_rgb, 1, size * size * 3, pipe);
        }

        frame_count++;
    }

    fprintf(stdout, "\n");
    fprintf(stderr, "Processed %d frames.\n", frame_count);

    if (isio_mode) {
        #ifdef USE_IMAGESTREAMIO
        if (stream_buffer) free(stream_buffer);
        // ImageStreamIO_closeIm(&stream_image); // Doesn't really exist/needed for shared mem often, but good practice if available?
        // Standard API usually just unmaps.
        // We'll leave it running?
        // "This mode enables... writing frames". 
        // Usually we want the stream to persist? Or assume cleanup on exit?
        // If we exit, the stream remains in SHM.
        #endif
    } else {
        if (frame_rgb) free(frame_rgb);
        if (pipe) pclose(pipe);
    }

    fclose(fin);
    return 0;
}
