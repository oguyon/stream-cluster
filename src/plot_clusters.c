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

double map_x(double x) {
    return (x - VIEW_MIN) / VIEW_RANGE * SVG_WIDTH;
}

double map_y(double y) {
    return (VIEW_MAX - y) / VIEW_RANGE * SVG_HEIGHT; // SVG y is down
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <clustered_file> [output_svg]\n", argv[0]);
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
        return 1;
    }

    FILE *fout = fopen(output_filename, "w");
    if (!fout) {
        perror("Error opening output file");
        fclose(fin);
        return 1;
    }

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
        // Skip comments or empty lines
        if (line[0] == '#' || line[0] == '\n') continue;

        int cluster_id;
        double x, y;

        // Parse: cluster_id x y ...
        // We handle variable whitespace
        char *token = strtok(line, " \t\n");
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

        // Use Cluster -1 for noise/unassigned if convention exists, typically cluster_id >= 0
        if (cluster_id < 0) color = "black";

        fprintf(fout, "<circle cx=\"%.2f\" cy=\"%.2f\" r=\"3\" fill=\"%s\" opacity=\"0.7\" />\n", sx, sy, color);
    }

    fprintf(fout, "</svg>\n");

    fclose(fin);
    fclose(fout);

    printf("Generated SVG: %s\n", output_filename);

    return 0;
}
