#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

void clamp(int *val) {
    if (*val < 0) *val = 0;
    if (*val > 255) *val = 255;
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <pixel_size> <alpha> <input.txt> <output.mp4> [noise_level] [max_frames]\n", argv[0]);
        return 1;
    }

    int size = atoi(argv[1]);
    if (size <= 0) {
        fprintf(stderr, "Error: pixel_size must be positive.\n");
        return 1;
    }

    double alpha = atof(argv[2]);
    char *input_file = argv[3];
    char *output_file = argv[4];
    double noise_level = 0.0;
    if (argc > 5) {
        noise_level = atof(argv[5]);
    }
    
    int max_frames = -1;
    if (argc > 6) {
        max_frames = atoi(argv[6]);
    }

    FILE *fin = fopen(input_file, "r");
    if (!fin) {
        fprintf(stderr, "Error: Cannot open input file %s\n", input_file);
        return 1;
    }

    // Build ffmpeg command
    // -y: overwrite
    // -f rawvideo: raw input
    // -pix_fmt rgb24: 3 bytes per pixel
    // -s: size
    // -r: framerate (arbitrary 30)
    // -i -: read from stdin
    // -c:v libx264: encode h264
    // -pix_fmt yuv420p: compatible pixel format for players
    // -crf 10: high quality (lower is better, 0 is lossless)
    // -preset slow: better compression/quality trade-off
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), 
        "ffmpeg -y -f rawvideo -vcodec rawvideo -pix_fmt rgb24 -s %dx%d -r 30 -i - -c:v libx264 -pix_fmt yuv420p -crf 10 -preset slow \"%s\"", 
        size, size, output_file);

    fprintf(stderr, "Running: %s\n", cmd);
    FILE *pipe = popen(cmd, "w");
    if (!pipe) {
        fprintf(stderr, "Error: Cannot open pipe to ffmpeg.\n");
        fclose(fin);
        return 1;
    }

    unsigned char *frame = (unsigned char *)malloc(size * size * 3);
    if (!frame) {
        fprintf(stderr, "Error: Memory allocation failed.\n");
        fclose(fin);
        pclose(pipe);
        return 1;
    }

    srand(time(NULL));

    char line[1024];
    int frame_count = 0;

    while (fgets(line, sizeof(line), fin)) {
        if (max_frames > 0 && frame_count >= max_frames) break;
        
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || strlen(line) == 0) continue;

        double v1, v2, v3;
        int parsed = sscanf(line, "%lf %lf %lf", &v1, &v2, &v3);
        if (parsed < 2) {
            continue; // parsing failed
        }
        if (parsed == 2) {
            v3 = 1.0;
        }

        // Clear frame (black)
        memset(frame, 0, size * size * 3);

        // Coordinates
        // x: -1.5 (left) to 1.5 (right)
        // y: -1.5 (bottom) to 1.5 (top)
        
        double cx_rel = (v1 + 1.5) / 3.0; // 0 to 1
        double cy_rel = (v2 + 1.5) / 3.0; // 0 to 1

        double cx = cx_rel * size;
        double cy = (1.0 - cy_rel) * size; // Invert Y for image coords

        double diameter = size * alpha * (v3 + 1.5);
        double sigma = diameter / 2.0;
        double sigma2 = sigma * sigma;
        double two_sigma2 = 2.0 * sigma2;

        // Bounding box for Gaussian (4*sigma captures >99.9%)
        int radius_bound = (int)ceil(4.0 * sigma);
        int min_x = (int)(cx - radius_bound);
        int max_x = (int)(cx + radius_bound);
        int min_y = (int)(cy - radius_bound);
        int max_y = (int)(cy + radius_bound);

        if (min_x < 0) min_x = 0;
        if (max_x >= size) max_x = size - 1;
        if (min_y < 0) min_y = 0;
        if (max_y >= size) max_y = size - 1;

        // Draw Gaussian Spot
        for (int y = min_y; y <= max_y; y++) {
            for (int x = min_x; x <= max_x; x++) {
                double dx = x - cx;
                double dy = y - cy;
                double dist2 = dx*dx + dy*dy;
                
                // Gaussian function: I = I_peak * exp(-dist^2 / (2*sigma^2))
                double val_f = 255.0 * exp(-dist2 / two_sigma2);
                
                int val = (int)(val_f + 0.5);
                if (val > 255) val = 255; // Should not happen with 255 peak, but safety
                
                if (val > 0) {
                    int idx = (y * size + x) * 3;
                    frame[idx] = (unsigned char)val;   // R
                    frame[idx+1] = (unsigned char)val; // G
                    frame[idx+2] = (unsigned char)val; // B
                }
            }
        }

        // Apply Noise
        if (noise_level > 0.0) {
            for (int i = 0; i < size * size * 3; i++) {
                // Random float -0.5 to 0.5
                double r = ((double)rand() / RAND_MAX) - 0.5; 
                // Scale by noise level (assuming noise_level is 0-255 range or 0-1 range?)
                // "amount of random noise". Let's assume it's pixel value delta magnitude.
                // If user passes 50, we add random -50 to +50.
                // But typically noise is sigma.
                // Let's interpret "amount" as the amplitude.
                
                int noise_val = (int)(r * 2.0 * noise_level); // range [-noise, +noise]
                int val = frame[i] + noise_val;
                clamp(&val);
                frame[i] = (unsigned char)val;
            }
        }

        fwrite(frame, 1, size * size * 3, pipe);
        frame_count++;
    }

    fprintf(stderr, "Processed %d frames.\n", frame_count);

    free(frame);
    fclose(fin);
    pclose(pipe);

    return 0;
}
