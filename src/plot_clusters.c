#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SVG_WIDTH 800
#define SVG_HEIGHT 800
#define VIEW_MIN -1.1
#define VIEW_MAX 1.1
#define VIEW_RANGE (VIEW_MAX - VIEW_MIN)

// Palette of colors for clusters
const char *colors[] = {
    "#e6194b", "#3cb44b", "#ffe119", "#4363d8", "#f58231",
    "#911eb4", "#46f0f0", "#f032e6", "#bcf60c", "#fabebe",
    "#008080", "#e6beff", "#9a6324", "#fffac8", "#800000",
    "#aaffc3", "#808000", "#ffd8b1", "#000075", "#808080",
    "#ffffff", "#000000"
};
#define NUM_COLORS (sizeof(colors) / sizeof(colors[0]))

typedef struct {
    int id;
    double x;
    double y;
} Anchor;

typedef struct {
    char text[256];
} HeaderLine;

double map_x(double x) {
    return (x - VIEW_MIN) / VIEW_RANGE * SVG_WIDTH;
}

double map_y(double y) {
    return (VIEW_MAX - y) / VIEW_RANGE * SVG_HEIGHT; // SVG y is down
}

void print_args_on_error(int argc, char *argv[]) {
    fprintf(stderr, "\nProgram arguments:\n");
    for (int i = 0; i < argc; i++) {
        fprintf(stderr, "  argv[%d] = \"%s\"\n", i, argv[i]);
    }
    fprintf(stderr, "\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <clustered_file> [output_svg]\n", argv[0]);
        print_args_on_error(argc, argv);
        return 1;
    }

    char *input_filename = argv[1];
    char output_filename[1024];

    if (argc >= 3) {
        strncpy(output_filename, argv[2], sizeof(output_filename) - 1);
    } else {
        // Derive output filename
        strncpy(output_filename, input_filename, sizeof(output_filename) - 5);
        char *ext = strrchr(output_filename, '.');
        if (ext) *ext = '\0';
        strcat(output_filename, ".svg");
    }

    FILE *fin = fopen(input_filename, "r");
    if (!fin) {
        perror("Error opening input file");
        print_args_on_error(argc, argv);
        return 1;
    }

    FILE *fout = fopen(output_filename, "w");
    if (!fout) {
        perror("Error opening output file");
        fclose(fin);
        print_args_on_error(argc, argv);
        return 1;
    }

    // Storage for overlays
    Anchor anchors[1000];
    int num_anchors = 0;

    HeaderLine params[100];
    int num_params = 0;

    HeaderLine stats[100];
    int num_stats = 0;

    int header_mode = 0; // 0: None, 1: Parameters, 2: Stats

    double rlim = 0.0;

    // Write SVG Header
    fprintf(fout, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n");
    fprintf(fout, "<svg width=\"%d\" height=\"%d\" xmlns=\"http://www.w3.org/2000/svg\">\n", SVG_WIDTH, SVG_HEIGHT);
    fprintf(fout, "<rect width=\"100%%\" height=\"100%%\" fill=\"white\" />\n");

    // Draw grid lines (axes)
    double cx = map_x(0);
    double cy = map_y(0);
    fprintf(fout, "<line x1=\"0\" y1=\"%.2f\" x2=\"%d\" y2=\"%.2f\" stroke=\"black\" stroke-width=\"1\" />\n", cy, SVG_WIDTH, cy);
    fprintf(fout, "<line x1=\"%.2f\" y1=\"0\" x2=\"%.2f\" y2=\"%d\" stroke=\"black\" stroke-width=\"1\" />\n", cx, cx, SVG_HEIGHT);

    // Draw unit box (-1 to 1)
    double bx1 = map_x(-1);
    double by1 = map_y(1);
    double bx2 = map_x(1);
    double by2 = map_y(-1);
    fprintf(fout, "<rect x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" fill=\"none\" stroke=\"gray\" stroke-dasharray=\"5,5\" />\n",
            bx1, by1, bx2 - bx1, by2 - by1);

    char line[4096];
    long line_num = 0;
    while (fgets(line, sizeof(line), fin)) {
        line_num++;

        // Handle comments / headers
        if (line[0] == '#') {
            // Check for rlim
            if (strncmp(line, "# rlim", 6) == 0) {
                sscanf(line + 6, "%lf", &rlim);
            }

            // Check for NEWCLUSTER
            if (strncmp(line, "# NEWCLUSTER", 12) == 0) {
                if (num_anchors < 1000) {
                    int id;
                    long frame_idx;
                    double ax, ay;
                    // Format: # NEWCLUSTER <ID> <FrameIdx> <X> <Y>
                    // Use sscanf carefully
                    if (sscanf(line + 12, "%d %ld %lf %lf", &id, &frame_idx, &ax, &ay) >= 4) {
                        anchors[num_anchors].id = id;
                        anchors[num_anchors].x = ax;
                        anchors[num_anchors].y = ay;
                        num_anchors++;
                    }
                }
                continue; // Skip processing NEWCLUSTER as header text
            }

            // Check for Section Headers
            if (strncmp(line, "# Parameters:", 13) == 0) {
                header_mode = 1;
            } else if (strncmp(line, "# Stats:", 8) == 0) {
                header_mode = 2;
            }

            // Collect header lines for display
            if (header_mode > 0) {
                // Determine target buffer
                HeaderLine *target_arr = (header_mode == 1) ? params : stats;
                int *target_count = (header_mode == 1) ? &num_params : &num_stats;
                int max_count = 100;

                if (*target_count < max_count) {
                    // strip newline
                    size_t len = strlen(line);
                    if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';

                    // Strip the leading "# "
                    char *text_start = line + 1;
                    if (*text_start == ' ') text_start++;

                    // Replace "Total Distance Computations" with "# dist comp"
                    char display_text[256];
                    char *found = strstr(text_start, "Total Distance Computations");
                    if (found) {
                        // Construct new string
                        // Copy prefix
                        size_t prefix_len = found - text_start;
                        strncpy(display_text, text_start, prefix_len);
                        display_text[prefix_len] = '\0';
                        // Append replacement
                        strcat(display_text, "# dist comp");
                        // Append suffix (skip original phrase)
                        strcat(display_text, found + strlen("Total Distance Computations"));
                    } else {
                        strncpy(display_text, text_start, sizeof(display_text)-1);
                        display_text[sizeof(display_text)-1] = '\0';
                    }

                    strncpy(target_arr[*target_count].text, display_text, sizeof(target_arr[0].text)-1);
                    (*target_count)++;
                }
            }
            continue;
        }

        // Skip empty lines
        if (line[0] == '\n') continue;

        long frame_idx;
        int cluster_id;
        double x, y;

        // Parse: frame_idx cluster_id x y ...
        char *token = strtok(line, " \t\n");
        if (!token) continue;
        frame_idx = atol(token);

        token = strtok(NULL, " \t\n");
        if (!token) continue;
        cluster_id = atoi(token);

        token = strtok(NULL, " \t\n");
        if (!token) continue;
        x = atof(token);

        token = strtok(NULL, " \t\n");
        if (!token) continue;
        y = atof(token);

        // Map coords
        double sx = map_x(x);
        double sy = map_y(y);

        const char *color = colors[abs(cluster_id) % NUM_COLORS];

        if (cluster_id < 0) color = "black";

        fprintf(fout, "<circle cx=\"%.2f\" cy=\"%.2f\" r=\"3\" fill=\"%s\" opacity=\"0.7\" />\n", sx, sy, color);
    }

    // Draw Anchors
    double r_px = (rlim / VIEW_RANGE) * SVG_WIDTH;
    for (int i = 0; i < num_anchors; i++) {
        double ax = map_x(anchors[i].x);
        double ay = map_y(anchors[i].y);

        // Draw Circle
        fprintf(fout, "<circle cx=\"%.2f\" cy=\"%.2f\" r=\"%.2f\" stroke=\"black\" fill=\"none\" stroke-width=\"1.5\" />\n", ax, ay, r_px);

        // Draw Cross
        double cross_size = 5.0;
        fprintf(fout, "<line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\" stroke=\"black\" stroke-width=\"2\" />\n",
                ax - cross_size, ay, ax + cross_size, ay);
        fprintf(fout, "<line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\" stroke=\"black\" stroke-width=\"2\" />\n",
                ax, ay - cross_size, ax, ay + cross_size);
    }

    double line_height = 15;

    // Draw Parameters (Top Left)
    double text_x = 10;
    double text_y = 20;

    fprintf(fout, "<g font-family=\"monospace\" font-size=\"12\" text-anchor=\"start\">\n");
    for (int i = 0; i < num_params; i++) {
        fprintf(fout, "<text x=\"%.2f\" y=\"%.2f\">%s</text>\n", text_x, text_y, params[i].text);
        text_y += line_height;
    }
    fprintf(fout, "</g>\n");

    // Draw Stats (Top Right)
    text_x = SVG_WIDTH - 10;
    text_y = 20;

    fprintf(fout, "<g font-family=\"monospace\" font-size=\"12\" text-anchor=\"end\">\n");
    for (int i = 0; i < num_stats; i++) {
        fprintf(fout, "<text x=\"%.2f\" y=\"%.2f\">%s</text>\n", text_x, text_y, stats[i].text);
        text_y += line_height;
    }
    fprintf(fout, "</g>\n");

    fprintf(fout, "</svg>\n");

    fclose(fin);
    fclose(fout);

    printf("Generated SVG: %s\n", output_filename);

    return 0;
}
