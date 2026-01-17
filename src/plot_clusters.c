#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <unistd.h>
#ifdef USE_PNG
#include <png.h>
#include "simple_font.h"
#endif

#define SVG_WIDTH 1200
#define PLOT_WIDTH 800
#define SVG_HEIGHT 800
#define VIEW_MIN -1.1
#define VIEW_MAX 1.1
#define VIEW_RANGE (VIEW_MAX - VIEW_MIN)

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

#ifdef USE_PNG
typedef struct {
    unsigned char r, g, b;
} ColorRGB;

typedef struct {
    int width;
    int height;
    unsigned char *data; 
} Canvas;

double map_x(double x) {
    return (x - VIEW_MIN) / VIEW_RANGE * PLOT_WIDTH;
}

double map_y(double y) {
    return (VIEW_MAX - y) / VIEW_RANGE * SVG_HEIGHT;
}

ColorRGB parse_color(const char *hex) {
    ColorRGB c = {0, 0, 0};
    if (hex[0] == '#') hex++;
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
    c->data = (unsigned char*)malloc(w * h * 3);
    memset(c->data, 255, w * h * 3);
    return c;
}

void free_canvas(Canvas *c) {
    if (c) { free(c->data); free(c); }
}

void set_pixel_opaque(Canvas *c, int x, int y, ColorRGB col) {
    if (x < 0 || x >= c->width || y < 0 || y >= c->height) return;
    int idx = (y * c->width + x) * 3;
    c->data[idx]   = col.r;
    c->data[idx+1] = col.g;
    c->data[idx+2] = col.b;
}

void draw_filled_rect(Canvas *c, int x, int y, int w, int h, ColorRGB col) {
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            set_pixel_opaque(c, i, j, col);
        }
    }
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
        if (err <= 0) { y += 1; err += 2 * y + 1; }
        if (err > 0) { x -= 1; err -= 2 * x + 1; }
    }
}

void draw_char(Canvas *c, int x, int y, char ch, ColorRGB col, int scale, int bold) {
    if (ch < 32 || ch > 126) return;
    int idx = ch - 32;
    for (int col_idx = 0; col_idx < 5; col_idx++) {
        unsigned char bits = font5x7[idx][col_idx];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                for (int sy=0; sy<scale; sy++) {
                    for (int sx=0; sx<scale; sx++) {
                        set_pixel_opaque(c, x + col_idx*scale + sx, y + row*scale + sy, col);
                        if (bold) set_pixel_opaque(c, x + col_idx*scale + sx + 1, y + row*scale + sy, col);
                    }
                }
            }
        }
    }
}

void draw_string(Canvas *c, int x, int y, const char *str, ColorRGB col, int align, int scale, int bold) {
    int len = strlen(str);
    int total_width = len * 6 * scale;
    int start_x = (align == 1) ? x - total_width : x;
    for (int i = 0; i < len; i++) {
        draw_char(c, start_x + i * 6 * scale, y, str[i], col, scale, bold);
    }
}

void draw_histogram(Canvas *c, int x, int y, int w, int h, long *data, int count) {
    ColorRGB bg = {250, 250, 250}, border = {0, 0, 0}, grid = {200, 200, 200}, bar_col = {100, 100, 255};
    draw_filled_rect(c, x, y, w, h, bg);
    draw_line(c, x, y, x+w, y, border); draw_line(c, x+w, y, x+w, y+h, border);
    draw_line(c, x+w, y+h, x, y+h, border); draw_line(c, x, y+h, x, y, border);
    long max_val = 0; int max_idx = 0;
    for (int i=0; i<count; i++) { if (data[i] > 0) max_idx = i; if (data[i] > max_val) max_val = data[i]; }
    if (max_val == 0) return;
    int display_count = max_idx + 2;
    double log_max = log10((double)max_val); if (log_max < 1.0) log_max = 1.0;
    for (int p = 0; p <= (int)log_max + 1; p++) {
        double val = pow(10, p); if (val > max_val * 2.0) break;
        double norm_h = p / (log_max * 1.1); int y_pos = y + h - 10 - (int)(norm_h * (h - 20));
        if (y_pos >= y && y_pos <= y + h - 10) {
            draw_line(c, x, y_pos, x + w, y_pos, grid);
            char label[32]; snprintf(label, sizeof(label), p == 0 ? "1" : "10^%d", p);
            draw_string(c, x + 2, y_pos - 8, label, border, 0, 1, 0);
        }
    }
    double bar_w = (double)(w - 30) / display_count; if (bar_w < 1.0) bar_w = 1.0;
    for (int i=0; i<display_count; i++) {
        if (data[i] > 0) {
            double lg = log10((double)data[i]); double norm_h = lg / (log_max * 1.1);
            int bar_h = (int)(norm_h * (h - 20)), bar_x = x + 25 + (int)(i * bar_w), bar_y = y + h - 10 - bar_h;
            draw_filled_rect(c, bar_x, bar_y, (int)bar_w + 1, bar_h, bar_col);
            char val_str[32]; snprintf(val_str, sizeof(val_str), "%ld", data[i]);
            draw_string(c, bar_x, bar_y - 10, val_str, border, 0, 1, 0);
            char bin_str[32]; snprintf(bin_str, sizeof(bin_str), "%d", i);
            draw_string(c, bar_x, y + h - 8, bin_str, border, 0, 1, 0);
        }
    }
    draw_string(c, x + w/2 - 60, y + 2, "Samples / Dist Count", border, 0, 1, 1);
}

void draw_cluster_histogram(Canvas *c, int x, int y, int w, int h, long *data, int count) {
    ColorRGB bg = {250, 250, 250}, border = {0, 0, 0}, grid = {200, 200, 200}, bar_col = {100, 200, 100};
    draw_filled_rect(c, x, y, w, h, bg);
    draw_line(c, x, y, x+w, y, border); draw_line(c, x+w, y, x+w, y+h, border);
    draw_line(c, x+w, y+h, x, y+h, border); draw_line(c, x, y+h, x, y, border);
    long max_val = 0; for (int i=0; i<count; i++) if (data[i] > max_val) max_val = data[i];
    if (max_val == 0) return;
    double log_max = log10((double)max_val); if (log_max < 1.0) log_max = 1.0;
    for (int p = 0; p <= (int)log_max + 1; p++) {
        double val = pow(10, p); if (val > max_val * 2.0) break;
        double norm_h = p / (log_max * 1.1); int y_pos = y + h - (int)(norm_h * (h - 10));
        if (y_pos >= y && y_pos <= y + h) {
            draw_line(c, x, y_pos, x + w, y_pos, grid);
            char label[32]; snprintf(label, sizeof(label), p == 0 ? "1" : "10^%d", p);
            draw_string(c, x - 5, y_pos - 8, label, border, 1, 1, 0);
        }
    }
    double bar_w = (double)w / count;
    for (int i=0; i<count; i++) {
        if (data[i] > 0) {
            double lg = log10((double)data[i]); double norm_h = lg / (log_max * 1.1);
            int bar_h = (int)(norm_h * (h - 10)), bar_x = x + (int)(i * bar_w), bar_y = y + h - bar_h;
            int draw_w = (int)bar_w; if (draw_w < 1) draw_w = 1;
            draw_filled_rect(c, bar_x, bar_y, draw_w, bar_h, bar_col);
        }
    }
    draw_string(c, x + w/2 - 50, y + 2, "Samples / Cluster", border, 0, 1, 1);
}

void draw_dcc_matrix(Canvas *c, int x, int y, int w, int h, const char *dcc_file, int num_clusters) {
    if (num_clusters <= 0 || !dcc_file[0]) return;
    double *matrix = (double *)malloc(num_clusters * num_clusters * sizeof(double));
    for(long k=0; k<num_clusters*num_clusters; k++) matrix[k] = -1.0;
    FILE *f = fopen(dcc_file, "r"); double max_dist = 0.0;
    if (f) {
        char line[1024];
        while(fgets(line, sizeof(line), f)) {
            int i, j; double d;
            if (sscanf(line, "%d %d %lf", &i, &j, &d) == 3) {
                if (i>=0 && i<num_clusters && j>=0 && j<num_clusters) {
                    matrix[i*num_clusters + j] = d; matrix[j*num_clusters + i] = d;
                    if (d > max_dist) max_dist = d;
                }
            }
        }
        fclose(f);
    }
    double cell_w = (double)w / num_clusters, cell_h = (double)h / num_clusters;
    for (int i=0; i<num_clusters; i++) {
        for (int j=0; j<num_clusters; j++) {
            double d = matrix[i*num_clusters + j];
            int px = x + (int)(j * cell_w), py = y + h - (int)((i+1) * cell_h);
            int pw = (int)((j+1)*cell_w) - (int)(j*cell_w), ph = (int)((i+1)*cell_h) - (int)(i*cell_h);
            if (pw < 1) pw = 1; if (ph < 1) ph = 1;
            ColorRGB col;
            if (d < 0) { if (i==j) col=(ColorRGB){255,255,255}; else col=(ColorRGB){255,0,0}; }
            else { unsigned char val = (unsigned char)(255.0 * (d / (max_dist > 0 ? max_dist : 1.0))); col=(ColorRGB){val,val,val}; }
            draw_filled_rect(c, px, py, pw, ph, col);
            if (num_clusters < 25 && d >= 0) {
                char txt[32]; snprintf(txt, sizeof(txt), "%.2f", d);
                ColorRGB txt_col = (col.r > 128) ? (ColorRGB){0,0,0} : (ColorRGB){255,255,255};
                draw_string(c, px + pw/2 - 10, py + ph/2 - 3, txt, txt_col, 0, 1, 0);
            }
        }
    }
    ColorRGB black = {0,0,0}; draw_line(c, x, y, x, y+h, black); draw_line(c, x, y+h, x+w, y+h, black);
    draw_string(c, x-15, y+h-5, "0", black, 0, 1, 0); draw_string(c, x, y+h+15, "0", black, 0, 1, 0);
    char n_str[32]; snprintf(n_str, sizeof(n_str), "%d", num_clusters);
    draw_string(c, x-25, y+5, n_str, black, 0, 1, 0); draw_string(c, x+w-10, y+h+15, n_str, black, 0, 1, 0);
    int cb_x = x + w + 10, cb_w = 10;
    for (int j=0; j<h; j++) {
        unsigned char val = (unsigned char)(255.0 * (1.0 - (double)j / h));
        draw_filled_rect(c, cb_x, y+j, cb_w, 1, (ColorRGB){val,val,val});
    }
    draw_string(c, cb_x + 15, y + h, "0", black, 0, 1, 0);
    char max_s[32]; snprintf(max_s, sizeof(max_s), "%.2f", max_dist);
    draw_string(c, cb_x + 15, y + 10, max_s, black, 0, 1, 0);
    free(matrix);
}

void draw_scale(Canvas *c, int x, int y) {
    double units = 0.5; int len_px = (int)(units / VIEW_RANGE * PLOT_WIDTH);
    ColorRGB black = {0,0,0}; draw_line(c, x, y, x + len_px, y, black);
    draw_line(c, x, y-5, x, y+5, black); draw_line(c, x + len_px, y-5, x + len_px, y+5, black);
    draw_string(c, x + len_px/2 - 10, y + 15, "0.5", black, 0, 1, 1);
}

int save_png(Canvas *c, const char *filename) {
    FILE *fp = fopen(filename, "wb"); if (!fp) return 1;
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) return 1;
    png_infop info = png_create_info_struct(png); if (!info) return 1;
    if (setjmp(png_jmpbuf(png))) return 1;
    png_init_io(png, fp);
    png_set_IHDR(png, info, c->width, c->height, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    png_bytep row_pointers[c->height];
    for (int y = 0; y < c->height; y++) row_pointers[y] = &c->data[y * c->width * 3];
    png_write_image(png, row_pointers); png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info); fclose(fp);
    return 0;
}
#endif

void print_help(const char *progname) {
    printf("Usage: gric-plot [options] <points_file> <log_file> [output_file]\n");
    printf("Description:\n");
    printf("  Visualizes clustering results by combining original points with membership info from log.\n");
    printf("Arguments:\n");
    printf("  <points_file>     Original input text file (coordinates).\n");
    printf("  <log_file>        Log file created by gric-cluster (contains stats and output dir).\n");
    printf("  [output_file]     Optional output filename.\n\n");
    printf("Options:\n");
    printf("  -h, --help        Show this help message.\n");
    printf("  -svg              Output SVG image instead of PNG (default: PNG).\n");
    printf("  -fs <size>        Set font size for text labels (default: 18.0).\n");
}

int main(int argc, char *argv[]) {
    char *points_filename = NULL, *log_filename = NULL, output_filename[1024] = {0};
    int png_mode = 1; double font_size = 18.0;
    int arg_idx = 1;
    while (arg_idx < argc) {
        if (strcmp(argv[arg_idx], "-h") == 0 || strcmp(argv[arg_idx], "--help") == 0) { print_help(argv[0]); return 0; }
        else if (strcmp(argv[arg_idx], "-svg") == 0) png_mode = 0;
        else if (strcmp(argv[arg_idx], "-fs") == 0) { if (arg_idx+1 < argc) font_size = atof(argv[++arg_idx]); }
        else if (argv[arg_idx][0] == '-') { fprintf(stderr, "Unknown: %s\n", argv[arg_idx]); return 1; }
        else { if (!points_filename) points_filename = argv[arg_idx]; else if (!log_filename) log_filename = argv[arg_idx]; else strncpy(output_filename, argv[arg_idx], 1023); }
        arg_idx++;
    }
    if (!points_filename || !log_filename) { print_help(argv[0]); return 1; }
    HeaderLine stats[100]; int num_stats = 0; double rlim = 0.0, dprob = 0.01, fmatcha = 2.0, fmatchb = 0.5;
    int maxcl = 1000, gprob = 0, te4 = 0, te5 = 0; long maxim = 100000;
    char output_dir[4096] = {0}, dcc_filename[4096] = {0}; long total_frames = 0, total_clusters = 0, total_dists = 0;
    long *hist_data = (long *)calloc(10000, sizeof(long)); int hist_parsing = 0;
    FILE *flog = fopen(log_filename, "r"); if (!flog) return 1;
    char line[4096];
    while (fgets(line, sizeof(line), flog)) {
        if (hist_parsing) { if (strncmp(line, "STATS_DIST_HIST_END", 19) == 0) hist_parsing = 0; else { int k; long c, p; if (sscanf(line, "%d %ld %ld", &k, &c, &p) >= 2 && k < 10000) hist_data[k] = c; } continue; }
        if (strncmp(line, "OUTPUT_DIR: ", 12) == 0) { strcpy(output_dir, line + 12); if (output_dir[strlen(output_dir)-1] == '\n') output_dir[strlen(output_dir)-1] = '\0'; }
        else if (strncmp(line, "OUTPUT_FILE: ", 13) == 0) { if (strstr(line, "dcc.txt")) { strcpy(dcc_filename, line + 13); if (dcc_filename[strlen(dcc_filename)-1] == '\n') dcc_filename[strlen(dcc_filename)-1] = '\0'; } }
        else if (strncmp(line, "PARAM_RLIM: ", 12) == 0) sscanf(line + 12, "%lf", &rlim);
        else if (strncmp(line, "PARAM_DPROB: ", 13) == 0) sscanf(line + 13, "%lf", &dprob);
        else if (strncmp(line, "PARAM_MAXCL: ", 13) == 0) sscanf(line + 13, "%d", &maxcl);
        else if (strncmp(line, "PARAM_MAXIM: ", 13) == 0) sscanf(line + 13, "%ld", &maxim);
        else if (strncmp(line, "PARAM_GPROB: ", 13) == 0) sscanf(line + 13, "%d", &gprob);
        else if (strncmp(line, "PARAM_FMATCHA: ", 15) == 0) sscanf(line + 15, "%lf", &fmatcha);
        else if (strncmp(line, "PARAM_FMATCHB: ", 15) == 0) sscanf(line + 15, "%lf", &fmatchb);
        else if (strncmp(line, "PARAM_TE4: ", 11) == 0) sscanf(line + 11, "%d", &te4);
        else if (strncmp(line, "PARAM_TE5: ", 11) == 0) sscanf(line + 11, "%d", &te5);
        else if (strncmp(line, "STATS_DIST_HIST_START", 21) == 0) hist_parsing = 1;
        else if (strncmp(line, "STATS_", 6) == 0) {
            char *k = line + 6, *v = strchr(k, ':');
            if (v) { *v = '\0'; v++; if (strcmp(k, "CLUSTERS") == 0) total_clusters = atol(v); else if (strcmp(k, "FRAMES") == 0) total_frames = atol(v); else if (strcmp(k, "DISTS") == 0) total_dists = atol(v); } 
        }
    }
    fclose(flog);
    snprintf(stats[0].text, 255, "%ld fr -> %ld cl (%ld dist)", total_frames, total_clusters, total_dists);
    char p_str[1024]; int po = snprintf(p_str, 1023, "Params: R=%.3f", rlim);
    if (dprob != 0.01) po += snprintf(p_str + po, 1023-po, ", dprob=%.3f", dprob);
    if (gprob) po += snprintf(p_str + po, 1023-po, ", gprob=ON");
    if (te4) po += snprintf(p_str + po, 1023-po, ", te4=ON");
    if (te5) po += snprintf(p_str + po, 1023-po, ", te5=ON");
    strncpy(stats[1].text, p_str, 255); num_stats = 2;
    if (!output_filename[0]) { strncpy(output_filename, points_filename, 1018); char *e = strrchr(output_filename, '.'); if (e) *e = '\0'; strcat(output_filename, png_mode ? ".png" : ".svg"); }
    FILE *f_pts = fopen(points_filename, "r"), *f_memb = fopen(output_dir[0] ? strcat(strcpy(line, output_dir), "/frame_membership.txt") : "frame_membership.txt", "r");
    if (!f_pts || !f_memb) return 1;
    FILE *svg_out = png_mode ? NULL : fopen(output_filename, "w");
    #ifdef USE_PNG
    Canvas *canvas = png_mode ? init_canvas(SVG_WIDTH, SVG_HEIGHT) : NULL;
    #endif
    if (!png_mode && svg_out) {
        fprintf(svg_out, "<svg width=\"%d\" height=\"%d\" xmlns=\"http://www.w3.org/2000/svg\"><rect width=\"100%%\" height=\"100%%\" fill=\"white\" />", SVG_WIDTH, SVG_HEIGHT);
        double cx = map_x(0), cy = map_y(0);
        fprintf(svg_out, "<line x1=\"0\" y1=\"%.2f\" x2=\"%d\" y2=\"%.2f\" stroke=\"black\" /><line x1=\"%.2f\" y1=\"0\" x2=\"%.2f\" y2=\"%d\" stroke=\"black\" />", cy, PLOT_WIDTH, cy, cx, cx, SVG_HEIGHT);
    }
    Anchor anchors[10000]; int num_anchors = 0; char *cluster_seen = calloc(10000, 1); long *samples_per_cluster = calloc(10000, sizeof(long));
    char lp[4096], lm[1024];
    while (fgets(lp, 4095, f_pts) && fgets(lm, 1023, f_memb)) {
        if (lp[0] == '#') continue;
        long idx; int cid; sscanf(lm, "%ld %d", &idx, &cid);
        if (cid >= 0 && cid < 10000) {
            samples_per_cluster[cid]++;
            if (!cluster_seen[cid]) { anchors[num_anchors++] = (Anchor){cid, 0, 0}; sscanf(lp, "%lf %lf", &anchors[num_anchors-1].x, &anchors[num_anchors-1].y); cluster_seen[cid] = 1; }
        }
        double x, y; sscanf(lp, "%lf %lf", &x, &y); double sx = map_x(x), sy = map_y(y);
        const char *hex = colors[abs(cid) % NUM_COLORS]; if (cid < 0) hex = "#000000";
        if (png_mode) { ColorRGB c = parse_color(hex); set_pixel_opaque(canvas, (int)sx, (int)sy, c); }
        else fprintf(svg_out, "<circle cx=\"%.2f\" cy=\"%.2f\" r=\"1\" fill=\"%s\" />", sx, sy, hex);
    }
    double r_px = (rlim / VIEW_RANGE) * PLOT_WIDTH;
    for (int i = 0; i < num_anchors; i++) {
        double ax = map_x(anchors[i].x), ay = map_y(anchors[i].y);
        if (png_mode) { ColorRGB b = {0,0,0}; draw_circle(canvas, (int)ax, (int)ay, (int)r_px, b); }
        else fprintf(svg_out, "<circle cx=\"%.2f\" cy=\"%.2f\" r=\"%.2f\" stroke=\"black\" fill=\"none\" />", ax, ay, r_px);
    }
    if (png_mode) {
        for (int i = 0; i < num_stats; i++) draw_string(canvas, 810, 20 + i * 30, stats[i].text, (ColorRGB){0,0,0}, 0, 1, 0);
        draw_histogram(canvas, 850, 100, 300, 150, hist_data, 10000);
        draw_cluster_histogram(canvas, 850, 300, 300, 130, samples_per_cluster, (int)total_clusters);
        if (dcc_filename[0]) draw_dcc_matrix(canvas, 850, 450, 300, 300, dcc_filename, (int)total_clusters);
        draw_scale(canvas, 50, 750); save_png(canvas, output_filename); free_canvas(canvas);
    } else { fprintf(svg_out, "</svg>"); fclose(svg_out); }
    fclose(f_pts); fclose(f_memb); free(cluster_seen); free(samples_per_cluster); free(hist_data); return 0;
}