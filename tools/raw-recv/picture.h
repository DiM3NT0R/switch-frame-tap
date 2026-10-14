/* v0.7.12: picture controls for the live view - brightness, contrast,
 * saturation, gamma and sharpness (reirahul's idea, GBAtemp).
 *
 * In the settings drawer (ui.h; P opens it there): Left / Right or the mouse
 * changes them, Backspace puts one back, 0 resets them all; remembered in
 * viewer.ini.
 * Done on the CPU, in the drawing thread, so it is the same with every
 * renderer (OpenGL, Direct3D, Metal) and in front of the upscalers: a lookup
 * table for the luma (brightness, contrast, gamma) and one for the chroma
 * (saturation), and a light unsharp mask on the luma. Into a buffer of our
 * own - the decoder's frames are its references and stay untouched, and so
 * do recordings. At the defaults nothing runs at all.
 */
#pragma once
#include <math.h>

typedef struct { int bright, contrast, sat, gamma10, sharp; } pic_t;
enum { PIC_BRIGHT, PIC_CONTRAST, PIC_SAT, PIC_GAMMA, PIC_SHARP, PIC_N };
static const pic_t pic_default = { 0, 100, 100, 10, 0 };
static pic_t g_pic = { 0, 100, 100, 10, 0 };

static const struct { const char *name; int min, max, step; } pic_rows[PIC_N] = {
    { "Brightness", -50, 50, 5 },
    { "Contrast",    50, 150, 5 },
    { "Saturation",   0, 200, 10 },
    { "Gamma",        5, 20, 1 },      /* x10 */
    { "Sharpness",    0, 100, 10 },
};

static int *pic_field(int row)
{
    switch (row) {
        case PIC_BRIGHT: return &g_pic.bright;
        case PIC_CONTRAST: return &g_pic.contrast;
        case PIC_SAT: return &g_pic.sat;
        case PIC_GAMMA: return &g_pic.gamma10;
        default: return &g_pic.sharp;
    }
}

static int pic_neutral(void)
{
    return g_pic.bright == 0 && g_pic.contrast == 100 && g_pic.sat == 100 && g_pic.gamma10 == 10 && g_pic.sharp == 0;
}

static void pic_value_text(int row, char *out, size_t cap)
{
    const int v = *pic_field(row);
    if (row == PIC_GAMMA) snprintf(out, cap, "%.1f", v / 10.0);
    else if (row == PIC_BRIGHT) snprintf(out, cap, "%+d", v);
    else snprintf(out, cap, "%d%%", v);
}

/* "0,100,100,10,0" in viewer.ini */
static void pic_parse(const char *s)
{
    pic_t p;
    if (sscanf(s, "%d,%d,%d,%d,%d", &p.bright, &p.contrast, &p.sat, &p.gamma10, &p.sharp) != 5) return;
    g_pic = p;
    for (int r = 0; r < PIC_N; r++) {
        int *f = pic_field(r);
        if (*f < pic_rows[r].min) *f = pic_rows[r].min;
        if (*f > pic_rows[r].max) *f = pic_rows[r].max;
    }
}

/* ---- the processing --------------------------------------------------- */

static struct {
    uint8_t ylut[256], clut[256];
    pic_t built;
    int have_lut;
    uint8_t *buf; size_t cap;
} g_picx;

static void pic_build_luts(void)
{
    if (g_picx.have_lut && memcmp(&g_picx.built, &g_pic, sizeof(g_pic)) == 0) return;
    const double c = g_pic.contrast / 100.0, b = g_pic.bright / 100.0, ig = 10.0 / g_pic.gamma10, s = g_pic.sat / 100.0;
    for (int i = 0; i < 256; i++) {
        /* video range: luma 16-235, chroma 16-240 around 128 */
        double l = (i - 16) / 219.0;
        l = (l - 0.5) * c + 0.5 + b * 0.5;
        if (l < 0) l = 0;
        if (l > 1) l = 1;
        l = pow(l, ig);
        int y = (int)lrint(16 + l * 219.0);
        g_picx.ylut[i] = (uint8_t)(y < 0 ? 0 : y > 255 ? 255 : y);
        int u = (int)lrint(128 + (i - 128) * s);
        g_picx.clut[i] = (uint8_t)(u < 16 ? 16 : u > 240 ? 240 : u);
    }
    g_picx.built = g_pic;
    g_picx.have_lut = 1;
}

#if defined(__GNUC__) && !defined(__clang__)
#define PIC_VEC __attribute__((optimize("tree-vectorize")))   /* gcc before 12 does not at -O2; clang does */
#else
#define PIC_VEC
#endif

/* the frame's planes in, adjusted copies out (data/linesize replaced);
 * nv12: one interleaved chroma plane instead of two. 1 = done. */
static PIC_VEC int pic_apply(uint8_t *data[3], int linesize[3], int W, int H, int nv12)
{
    if (pic_neutral() || W <= 0 || H <= 0) return 0;
    pic_build_luts();
    const int ys = (W + 63) & ~63, cs = nv12 ? ys : ((W / 2 + 63) & ~63);
    const size_t need = (size_t)ys * H + (size_t)cs * (H / 2) * (nv12 ? 1 : 2);
    if (need > g_picx.cap) {
        free(g_picx.buf);
        g_picx.buf = malloc(need);
        g_picx.cap = g_picx.buf ? need : 0;
        if (!g_picx.buf) return 0;
    }
    uint8_t *oy = g_picx.buf, *o1 = oy + (size_t)ys * H, *o2 = o1 + (size_t)cs * (H / 2);
    const uint8_t *yl = g_picx.ylut, *cl = g_picx.clut;
    /* luma: the table, then (sharpness) an unsharp mask on the 4 neighbours */
    for (int y = 0; y < H; y++) {
        const uint8_t *s = data[0] + (size_t)y * linesize[0];
        uint8_t *d = oy + (size_t)y * ys;
        for (int x = 0; x < W; x++) d[x] = yl[s[x]];
    }
    if (g_pic.sharp > 0 && H > 2 && W > 2) {
        static uint8_t *prev = NULL, *cur = NULL; static size_t rcap = 0;
        if ((size_t)W > rcap) { free(prev); free(cur); prev = malloc(W); cur = malloc(W); rcap = (prev && cur) ? (size_t)W : 0; }
        if (rcap) {
            const int k = g_pic.sharp * 256 / 200;   /* 100% = half the detail added once more */
            memcpy(prev, oy, W);
            for (int y = 1; y < H - 1; y++) {
                uint8_t *row = oy + (size_t)y * ys;
                const uint8_t *dn = row + ys;
                memcpy(cur, row, W);
                const uint8_t *restrict cu = cur, *restrict pv = prev;
                uint8_t *restrict ro = row;
                for (int x = 1; x < W - 1; x++) {
                    const int c0 = cu[x];
                    const int hp = 4 * c0 - cu[x - 1] - cu[x + 1] - pv[x] - dn[x];
                    int v = c0 + ((hp * k) >> 8);
                    v = v < 0 ? 0 : v;
                    ro[x] = (uint8_t)(v > 255 ? 255 : v);
                }
                uint8_t *t = prev; prev = cur; cur = t;
            }
        }
    }
    /* chroma: saturation around the middle */
    const int cw = nv12 ? W : W / 2;
    for (int p = 0; p < (nv12 ? 1 : 2); p++) {
        uint8_t *o = p ? o2 : o1;
        for (int y = 0; y < H / 2; y++) {
            const uint8_t *s = data[1 + p] + (size_t)y * linesize[1 + p];
            uint8_t *d = o + (size_t)y * cs;
            if (g_pic.sat == 100) memcpy(d, s, cw);
            else for (int x = 0; x < cw; x++) d[x] = cl[s[x]];
        }
    }
    data[0] = oy; linesize[0] = ys;
    data[1] = o1; linesize[1] = cs;
    if (!nv12) { data[2] = o2; linesize[2] = cs; }
    return 1;
}
