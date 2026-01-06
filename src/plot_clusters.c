#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <png.h>
#include "simple_font.h"

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

typedef struct {
    unsigned char r, g, b;
} ColorRGB;

typedef struct {
    int width;
    int height;
    unsigned char *data; // RGB buffer
} Canvas;

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

ColorRGB parse_color(const char *hex) {
    ColorRGB c = {0, 0, 0};
    if (hex[0] == '#') hex++;
    if (strcmp(hex, "black") == 0) return c;
    if (strcmp(hex, "white") == 0) { c.r=255; c.g=255; c.b=255; return c; }

    unsigned int r, g, b;
    if (sscanf(hex, "%02x%02x%02x", &r, &g, &b) == 3) {
        c.r = r; c.g = g; c.b = b;
    }
    return c;
}

Canvas* init_canvas(int w, int h) {
    Canvas *c = (Canvas*)malloc(sizeof(Canvas));
    c->width = w;
    c->height = h;
    c->data = (unsigned char*)calloc(w * h * 3, 1);
    // Fill white
    memset(c->data, 255, w * h * 3);
    return c;
}

void free_canvas(Canvas *c) {
    if (c) {
        free(c->data);
        free(c);
    }
}

void set_pixel(Canvas *c, int x, int y, ColorRGB col) {
    if (x < 0 || x >= c->width || y < 0 || y >= c->height) return;
    int idx = (y * c->width + x) * 3;
    // Simple blending (alpha=0.7 implies 30% background)
    // Background assumed white (255)
    // out = 0.7*col + 0.3*255
    c->data[idx]   = (unsigned char)(0.7 * col.r + 0.3 * 255);
    c->data[idx+1] = (unsigned char)(0.7 * col.g + 0.3 * 255);
    c->data[idx+2] = (unsigned char)(0.7 * col.b + 0.3 * 255);
}

void set_pixel_opaque(Canvas *c, int x, int y, ColorRGB col) {
    if (x < 0 || x >= c->width || y < 0 || y >= c->height) return;
    int idx = (y * c->width + x) * 3;
    c->data[idx]   = col.r;
    c->data[idx+1] = col.g;
    c->data[idx+2] = col.b;
}

void draw_line(Canvas *c, int x0, int y0, int x1, int y1, ColorRGB col) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    while (1) {
        set_pixel_opaque(c, x0, y0, col);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void draw_circle(Canvas *c, int cx, int cy, int r, ColorRGB col) {
    int x = r, y = 0;
    int err = 0;
    while (x >= y) {
        set_pixel_opaque(c, cx + x, cy + y, col);
        set_pixel_opaque(c, cx + y, cy + x, col);
        set_pixel_opaque(c, cx - y, cy + x, col);
        set_pixel_opaque(c, cx - x, cy + y, col);
        set_pixel_opaque(c, cx - x, cy - y, col);
        set_pixel_opaque(c, cx - y, cy - x, col);
        set_pixel_opaque(c, cx + y, cy - x, col);
        set_pixel_opaque(c, cx + x, cy - y, col);
        if (err <= 0) {
            y += 1;
            err += 2 * y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2 * x + 1;
        }
    }
}

void draw_filled_circle(Canvas *c, int cx, int cy, int r, ColorRGB col) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x*x + y*y <= r*r) {
                set_pixel(c, cx+x, cy+y, col);
            }
        }
    }
}

void draw_char(Canvas *c, int x, int y, char ch, ColorRGB col) {
    if (ch < 32 || ch > 126) return;
    int idx = ch - 32;
    for (int col_idx = 0; col_idx < 5; col_idx++) {
        unsigned char bits = font5x7[idx][col_idx];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                set_pixel_opaque(c, x + col_idx, y + row, col);
            }
        }
    }
}

void draw_string(Canvas *c, int x, int y, const char *str, ColorRGB col, int align) {
    // align: 0=left, 1=right
    int len = strlen(str);
    int width = len * 6; // 5 + 1 spacing
    int start_x = x;
    if (align == 1) start_x = x - width;

    for (int i = 0; i < len; i++) {
        draw_char(c, start_x + i * 6, y, str[i], col);
    }
}

int save_png(Canvas *c, const char *filename) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) return 1;

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) return 1;

    png_infop info = png_create_info_struct(png);
    if (!info) return 1;

    if (setjmp(png_jmpbuf(png))) return 1;

    png_init_io(png, fp);
    png_set_IHDR(png, info, c->width, c->height, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    png_bytep row_pointers[c->height];
    for (int y = 0; y < c->height; y++) {
        row_pointers[y] = &c->data[y * c->width * 3];
    }

    png_write_image(png, row_pointers);
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <clustered_file> [output] [-png]\n", argv[0]);
        print_args_on_error(argc, argv);
        return 1;
    }

    char *input_filename = argv[1];
    char output_filename[1024] = {0};
    int png_mode = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-png") == 0) {
            png_mode = 1;
        } else {
            strncpy(output_filename, argv[i], sizeof(output_filename)-1);
        }
    }

    if (strlen(output_filename) == 0) {
        strncpy(output_filename, input_filename, sizeof(output_filename) - 5);
        char *ext = strrchr(output_filename, '.');
        if (ext) *ext = '\0';
        strcat(output_filename, png_mode ? ".png" : ".svg");
    }

    FILE *fin = fopen(input_filename, "r");
    if (!fin) {
        perror("Error opening input file");
        print_args_on_error(argc, argv);
        return 1;
    }

    // Init output buffers
    FILE *svg_out = NULL;
    Canvas *canvas = NULL;

    if (png_mode) {
        canvas = init_canvas(SVG_WIDTH, SVG_HEIGHT);
        // Draw grid
        ColorRGB grid_col = {0, 0, 0}; // Black
        int cx = (int)map_x(0);
        int cy = (int)map_y(0);
        draw_line(canvas, 0, cy, SVG_WIDTH, cy, grid_col);
        draw_line(canvas, cx, 0, cx, SVG_HEIGHT, grid_col);

        // Dashed box - simulate with dots or solid gray
        ColorRGB gray = {128, 128, 128};
        int bx1 = (int)map_x(-1), by1 = (int)map_y(1);
        int bx2 = (int)map_x(1), by2 = (int)map_y(-1);
        // Draw rect
        draw_line(canvas, bx1, by1, bx2, by1, gray);
        draw_line(canvas, bx2, by1, bx2, by2, gray);
        draw_line(canvas, bx2, by2, bx1, by2, gray);
        draw_line(canvas, bx1, by2, bx1, by1, gray);
    } else {
        svg_out = fopen(output_filename, "w");
        if (!svg_out) {
            perror("Error opening output file");
            fclose(fin);
            return 1;
        }
        fprintf(svg_out, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n");
        fprintf(svg_out, "<svg width=\"%d\" height=\"%d\" xmlns=\"http://www.w3.org/2000/svg\">\n", SVG_WIDTH, SVG_HEIGHT);
        fprintf(svg_out, "<rect width=\"100%%\" height=\"100%%\" fill=\"white\" />\n");

        double cx = map_x(0);
        double cy = map_y(0);
        fprintf(svg_out, "<line x1=\"0\" y1=\"%.2f\" x2=\"%d\" y2=\"%.2f\" stroke=\"black\" stroke-width=\"1\" />\n", cy, SVG_WIDTH, cy);
        fprintf(svg_out, "<line x1=\"%.2f\" y1=\"0\" x2=\"%.2f\" y2=\"%d\" stroke=\"black\" stroke-width=\"1\" />\n", cx, cx, SVG_HEIGHT);

        double bx1 = map_x(-1);
        double by1 = map_y(1);
        double bx2 = map_x(1);
        double by2 = map_y(-1);
        fprintf(svg_out, "<rect x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" fill=\"none\" stroke=\"gray\" stroke-dasharray=\"5,5\" />\n",
                bx1, by1, bx2 - bx1, by2 - by1);
    }

    // Storage for overlays
    Anchor anchors[1000];
    int num_anchors = 0;

    HeaderLine params[100];
    int num_params = 0;

    HeaderLine stats[100];
    int num_stats = 0;

    int header_mode = 0;
    double rlim = 0.0;

    char line[4096];
    long line_num = 0;
    while (fgets(line, sizeof(line), fin)) {
        line_num++;

        if (line[0] == '#') {
            if (strncmp(line, "# rlim", 6) == 0) {
                sscanf(line + 6, "%lf", &rlim);
            }
            if (strncmp(line, "# NEWCLUSTER", 12) == 0) {
                if (num_anchors < 1000) {
                    int id;
                    long frame_idx;
                    double ax, ay;
                    if (sscanf(line + 12, "%d %ld %lf %lf", &id, &frame_idx, &ax, &ay) >= 4) {
                        anchors[num_anchors].id = id;
                        anchors[num_anchors].x = ax;
                        anchors[num_anchors].y = ay;
                        num_anchors++;
                    }
                }
                continue;
            }
            if (strncmp(line, "# Parameters:", 13) == 0) {
                header_mode = 1;
            } else if (strncmp(line, "# Stats:", 8) == 0) {
                header_mode = 2;
            }

            if (header_mode > 0) {
                HeaderLine *target_arr = (header_mode == 1) ? params : stats;
                int *target_count = (header_mode == 1) ? &num_params : &num_stats;
                int max_count = 100;

                if (*target_count < max_count) {
                    size_t len = strlen(line);
                    if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
                    char *text_start = line + 1;
                    if (*text_start == ' ') text_start++;

                    char display_text[256];
                    char *found = strstr(text_start, "Total Distance Computations");
                    if (found) {
                        size_t prefix_len = found - text_start;
                        strncpy(display_text, text_start, prefix_len);
                        display_text[prefix_len] = '\0';
                        strcat(display_text, "# dist comp");
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

        if (line[0] == '\n') continue;

        long frame_idx;
        int cluster_id;
        double x, y;

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

        double sx = map_x(x);
        double sy = map_y(y);

        const char *hex = colors[abs(cluster_id) % NUM_COLORS];
        if (cluster_id < 0) hex = "#000000";

        if (png_mode) {
            ColorRGB col = parse_color(hex);
            draw_filled_circle(canvas, (int)sx, (int)sy, 3, col);
        } else {
            fprintf(svg_out, "<circle cx=\"%.2f\" cy=\"%.2f\" r=\"3\" fill=\"%s\" opacity=\"0.7\" />\n", sx, sy, hex);
        }
    }

    // Draw Anchors
    double r_px = (rlim / VIEW_RANGE) * SVG_WIDTH;
    for (int i = 0; i < num_anchors; i++) {
        double ax = map_x(anchors[i].x);
        double ay = map_y(anchors[i].y);

        if (png_mode) {
            ColorRGB black = {0,0,0};
            draw_circle(canvas, (int)ax, (int)ay, (int)r_px, black);
            draw_line(canvas, (int)ax-5, (int)ay, (int)ax+5, (int)ay, black);
            draw_line(canvas, (int)ax, (int)ay-5, (int)ax, (int)ay+5, black);
        } else {
            fprintf(svg_out, "<circle cx=\"%.2f\" cy=\"%.2f\" r=\"%.2f\" stroke=\"black\" fill=\"none\" stroke-width=\"1.5\" />\n", ax, ay, r_px);
            fprintf(svg_out, "<line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\" stroke=\"black\" stroke-width=\"2\" />\n",
                    ax - 5, ay, ax + 5, ay);
            fprintf(svg_out, "<line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\" stroke=\"black\" stroke-width=\"2\" />\n",
                    ax, ay - 5, ax, ay + 5);
        }
    }

    // Text Overlays
    int text_y = 20;
    int line_height = 15;

    if (png_mode) {
        ColorRGB black = {0,0,0};
        int text_x = 10;
        for (int i = 0; i < num_params; i++) {
            draw_string(canvas, text_x, text_y, params[i].text, black, 0);
            text_y += line_height;
        }

        text_x = SVG_WIDTH - 10;
        text_y = 20;
        for (int i = 0; i < num_stats; i++) {
            draw_string(canvas, text_x, text_y, stats[i].text, black, 1);
            text_y += line_height;
        }

        save_png(canvas, output_filename);
        free_canvas(canvas);
    } else {
        double text_x = 10;
        fprintf(svg_out, "<g font-family=\"monospace\" font-size=\"12\" text-anchor=\"start\">\n");
        for (int i = 0; i < num_params; i++) {
            fprintf(svg_out, "<text x=\"%.2f\" y=\"%d\">%s</text>\n", text_x, text_y, params[i].text);
            text_y += line_height;
        }
        fprintf(svg_out, "</g>\n");

        text_x = SVG_WIDTH - 10;
        text_y = 20;
        fprintf(svg_out, "<g font-family=\"monospace\" font-size=\"12\" text-anchor=\"end\">\n");
        for (int i = 0; i < num_stats; i++) {
            fprintf(svg_out, "<text x=\"%.2f\" y=\"%d\">%s</text>\n", text_x, text_y, stats[i].text);
            text_y += line_height;
        }
        fprintf(svg_out, "</g>\n");

        fprintf(svg_out, "</svg>\n");
        fclose(svg_out);
    }

    fclose(fin);
    printf("Generated: %s\n", output_filename);

    return 0;
}
