/*
 * frequency - XSFFE (X-Signature Fingerprint Frequency Estimation)
 *
 * Kullanim:  frequency -w <genislik> -h <yukseklik> -s <maske.raw>
 *                      -o <orientasyon.raw> < girdi.raw > frekans.raw
 *
 * Girdi : standart giristen 8-bit raw parmak izi goruntusu, -s ile
 *         segmentation maskesi, -o ile orientation aracinin ciktisi.
 * Cikti : standart cikisa her piksel icin sirt periyodu (ardisik iki sirt
 *         arasindaki piksel uzakligi; frekansin tersi), 8-bit raw:
 *         deger = round(periyot * 10), arka plan = 0.
 *
 * pyfing.frequencies.Xsffe yonteminin C uyarlamasidir; sabitler pyfing'in
 * varsayilan XsffeParameters degerleridir (yalnizca 500 dpi).
 */
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "fpimg.h"

#define PROG "frequency"

/* pyfing XsffeParameters varsayilanlari */
#define WINDOW_W                 23
#define WINDOW_H                 43
#define STEP                     16
#define BORDER                   15
#define MIN_BACKGROUND_DISTANCE  11
#define PERIOD_MIN               5
#define PERIOD_MAX               20
#define MIN_VALID_DISTANCES      4
#define DIFFUSION_SIZE           21
#define MEDIAN_SIZE              5
#define BLUR_SIZE                3
#define FINAL_BLUR_SIZE          33

/* cvRound: en yakin tamsayi, yarimda cifte */
static int cv_round(double v) { return (int)nearbyint(v); }


/* ====================================================================== */
/* cv::distanceTransform(copyMakeBorder(mask, 1,..., 0), DIST_C, 3)         */
/* Her pikselin en yakin arka plan (veya goruntu disi) pikseline satranc   */
/* tahtasi uzakligi.                                                       */
/* ====================================================================== */
static void chessboard_distance(const unsigned char *mask, int w, int h, int *out)
{
    int pw = w + 2, ph = h + 2, x, y;
    int *d = fp_alloc(PROG, sizeof(int) * (size_t)pw * (size_t)ph);
    const int INF = 1 << 28;

    for (y = 0; y < ph; y++)
        for (x = 0; x < pw; x++) {
            int inside = x > 0 && y > 0 && x < pw - 1 && y < ph - 1 &&
                         mask[(size_t)(y - 1) * w + (x - 1)];
            d[(size_t)y * pw + x] = inside ? INF : 0;
        }
    /* ileri ve geri gecis (agirliklar 1: tam satranc tahtasi uzakligi) */
    for (y = 1; y < ph - 1; y++)
        for (x = 1; x < pw - 1; x++) {
            int *p = &d[(size_t)y * pw + x], v = *p;
            if (p[-1] + 1 < v) v = p[-1] + 1;
            if (p[-pw - 1] + 1 < v) v = p[-pw - 1] + 1;
            if (p[-pw] + 1 < v) v = p[-pw] + 1;
            if (p[-pw + 1] + 1 < v) v = p[-pw + 1] + 1;
            *p = v;
        }
    for (y = ph - 2; y >= 1; y--)
        for (x = pw - 2; x >= 1; x--) {
            int *p = &d[(size_t)y * pw + x], v = *p;
            if (p[1] + 1 < v) v = p[1] + 1;
            if (p[pw + 1] + 1 < v) v = p[pw + 1] + 1;
            if (p[pw] + 1 < v) v = p[pw] + 1;
            if (p[pw - 1] + 1 < v) v = p[pw - 1] + 1;
            *p = v;
        }
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            out[(size_t)y * w + x] = d[(size_t)(y + 1) * pw + (x + 1)];
    free(d);
}

/* ====================================================================== */
/* cv::warpAffine(src, M, (dw, dh), INTER_LINEAR, BORDER_REFLECT), 8-bit   */
/* OpenCV 5 (warp_kernels.simd.hpp) ile ayni hesap: M double'da tersine    */
/* cevrilip float'a donusturulur; kaynak koordinati ve bilineer            */
/* enterpolasyon float ile yapilir. Ilk VEC_COLS sutun SIMD yolunda FMA    */
/* (tek yuvarlama), kalanlar skaler yolda normal carpma/toplama kullanir.  */
/* ====================================================================== */
#define VEC_COLS 16   /* SSE/AVX2: 23 sutunluk pencerede SIMD'nin isledigi kisim */

static void warp_affine_u8(const unsigned char *src, int sw, int sh,
                           unsigned char *dst, int dw, int dh, const double *M0)
{
    double Md[6], D, A11, A22, b1, b2;
    float M[6];
    int x, y, k;

    memcpy(Md, M0, sizeof Md);
    D = Md[0] * Md[4] - Md[1] * Md[3];
    D = D != 0 ? 1. / D : 0;
    A11 = Md[4] * D; A22 = Md[0] * D;
    Md[0] = A11; Md[1] *= -D;
    Md[3] *= -D; Md[4] = A22;
    b1 = -Md[0] * Md[2] - Md[1] * Md[5];
    b2 = -Md[3] * Md[2] - Md[4] * Md[5];
    Md[2] = b1; Md[5] = b2;
    for (k = 0; k < 6; k++) M[k] = (float)Md[k];

    for (y = 0; y < dh; y++) {
        float Mx = (float)y * M[1] + M[2], My = (float)y * M[4] + M[5];
        int vec_end = dw >= VEC_COLS ? (dw / VEC_COLS) * VEC_COLS : 0;
        for (x = 0; x < dw; x++) {
            int vec = x < vec_end, ix, iy;
            float sx, sy, p00, p01, p10, p11, v0, v1;

            if (vec) {
                sx = fmaf(M[0], (float)x, Mx);
                sy = fmaf(M[3], (float)x, My);
            } else {
                sx = (float)x * M[0] + (float)y * M[1] + M[2];
                sy = (float)x * M[3] + (float)y * M[4] + M[5];
            }
            ix = (int)floorf(sx);
            iy = (int)floorf(sy);
            sx -= (float)ix;
            sy -= (float)iy;
            p00 = src[(size_t)fp_reflect(iy, sh) * sw + fp_reflect(ix, sw)];
            p01 = src[(size_t)fp_reflect(iy, sh) * sw + fp_reflect(ix + 1, sw)];
            p10 = src[(size_t)fp_reflect(iy + 1, sh) * sw + fp_reflect(ix, sw)];
            p11 = src[(size_t)fp_reflect(iy + 1, sh) * sw + fp_reflect(ix + 1, sw)];
            if (vec) {
                v0 = fmaf(sx, p01 - p00, p00);
                v1 = fmaf(sx, p11 - p10, p10);
                v0 = fmaf(sy, v1 - v0, v0);
            } else {
                v0 = p00 + sx * (p01 - p00);
                v1 = p10 + sx * (p11 - p10);
                v0 += sy * (v1 - v0);
            }
            v0 = nearbyintf(v0);
            dst[(size_t)y * dw + x] = (unsigned char)(v0 < 0 ? 0 : v0 > 255 ? 255 : v0);
        }
    }
}

/* ====================================================================== */
/* cv::inpaint(src, mask, radius, INPAINT_NS), float goruntu               */
/* OpenCV inpaint.cpp'deki algoritmanin tek kanalli (skaler) uyarlamasi.   */
/* ====================================================================== */

#define KNOWN  0
#define BAND   1
#define INSIDE 2

typedef struct { float t; unsigned order; int i, j; } HeapNode;
typedef struct { HeapNode *a; size_t n, cap; unsigned next_order; } Heap;

static int node_less(const HeapNode *a, const HeapNode *b)
{
    return a->t < b->t || (a->t == b->t && a->order < b->order);
}

static void heap_push(Heap *hp, int i, int j, float t)
{
    size_t pos;
    HeapNode node;
    if (hp->n == hp->cap) {
        HeapNode *tmp;
        hp->cap = hp->cap ? hp->cap * 2 : 256;
        tmp = realloc(hp->a, hp->cap * sizeof(HeapNode));
        if (!tmp) fp_die(PROG, "bellek yetersiz");
        hp->a = tmp;
    }
    node.t = t == 0 ? 0.0f : t;   /* -0 ve +0 ayni anahtar */
    node.order = hp->next_order++;
    node.i = i;
    node.j = j;
    pos = hp->n++;
    while (pos > 0) {
        size_t parent = (pos - 1) / 2;
        if (node_less(&hp->a[parent], &node)) break;
        hp->a[pos] = hp->a[parent];
        pos = parent;
    }
    hp->a[pos] = node;
}

static int heap_pop(Heap *hp, int *i, int *j)
{
    HeapNode last;
    size_t pos = 0;
    if (hp->n == 0) return 0;
    *i = hp->a[0].i;
    *j = hp->a[0].j;
    last = hp->a[--hp->n];
    for (;;) {
        size_t c = 2 * pos + 1;
        if (c >= hp->n) break;
        if (c + 1 < hp->n && node_less(&hp->a[c + 1], &hp->a[c])) c++;
        if (node_less(&last, &hp->a[c])) break;
        hp->a[pos] = hp->a[c];
        pos = c;
    }
    if (hp->n) hp->a[pos] = last;
    return 1;
}

static float fm_solve(const unsigned char *f, const float *t, int cols,
                      int i1, int j1, int i2, int j2)
{
    double sol, a11 = t[i1 * cols + j1], a22 = t[i2 * cols + j2];
    double m12 = a11 < a22 ? a11 : a22;
    if (f[i1 * cols + j1] != INSIDE) {
        if (f[i2 * cols + j2] != INSIDE)
            sol = fabs(a11 - a22) >= 1.0 ? 1 + m12
                : (a11 + a22 + sqrt(2 - (a11 - a22) * (a11 - a22))) * 0.5;
        else
            sol = 1 + a11;
    } else if (f[i2 * cols + j2] != INSIDE) {
        sol = 1 + a22;
    } else {
        sol = 1 + m12;
    }
    return (float)sol;
}

static float fm_min4(const unsigned char *f, const float *t, int cols, int i, int j)
{
    float a = fm_solve(f, t, cols, i - 1, j, i, j - 1);
    float b = fm_solve(f, t, cols, i + 1, j, i, j - 1);
    float c = fm_solve(f, t, cols, i - 1, j, i, j + 1);
    float d = fm_solve(f, t, cols, i + 1, j, i, j + 1);
    a = a < b ? a : b;
    c = c < d ? c : d;
    return a < c ? a : c;
}

static void inpaint_ns(float *out, const unsigned char *inpaint_mask, int w, int h, double radius)
{
    int range = cv_round(radius), erows = h + 2, ecols = w + 2, i, j, ii, jj, q, dk, dl;
    int side, *half_width;
    float *dist_tab, *t;
    unsigned char *f;
    Heap heap = {0, 0, 0, 0};

    if (range < 1) range = 1;
    if (range > 100) range = 100;
    side = 2 * range + 1;

    /* disk tablosu: satir yari genislikleri ve 1/(|r|^4 + 1) agirliklari */
    half_width = fp_alloc(PROG, sizeof(int) * (size_t)side);
    dist_tab = fp_alloc(PROG, sizeof(float) * (size_t)side * (size_t)side);
    for (dk = -range; dk <= range; dk++) {
        int hw = 0;
        while ((hw + 1) * (hw + 1) + dk * dk <= range * range) hw++;
        half_width[dk + range] = hw;
        for (dl = -range; dl <= range; dl++) {
            float ry = (float)(-dk), rx = (float)(-dl), len = rx * rx + ry * ry;
            dist_tab[(dk + range) * side + (dl + range)] = 1 / (len * len + 1);
        }
    }

    /* f: bayraklar (1 piksel cerceveli), t: fast marching uzakligi */
    f = fp_alloc(PROG, (size_t)erows * (size_t)ecols);
    t = fp_alloc(PROG, sizeof(float) * (size_t)erows * (size_t)ecols);
    memset(f, KNOWN, (size_t)erows * (size_t)ecols);
    for (i = 0; i < h; i++)
        for (j = 0; j < w; j++)
            if (inpaint_mask[(size_t)i * w + j]) f[(i + 1) * ecols + (j + 1)] = INSIDE;
    for (i = 0; i < erows * ecols; i++) t[i] = 1.0e6f;

    /* bant: bilinmeyen bir 4-komsusu olan bilinen pikseller (cerceve haric) */
    for (i = 1; i < erows - 1; i++)
        for (j = 1; j < ecols - 1; j++) {
            int p = i * ecols + j;
            if (f[p] != INSIDE &&
                (f[p - 1] == INSIDE || f[p + 1] == INSIDE ||
                 f[p - ecols] == INSIDE || f[p + ecols] == INSIDE)) {
                t[p] = 0;
                heap_push(&heap, i, j, 0);
            }
        }

    while (heap_pop(&heap, &ii, &jj)) {
        f[ii * ecols + jj] = KNOWN;
        for (q = 0; q < 4; q++) {
            int kbeg, kend, k;
            float Ia = 0, s = 1.0e-20f, dist;
            if (q == 0)      { i = ii - 1; j = jj; }
            else if (q == 1) { i = ii;     j = jj - 1; }
            else if (q == 2) { i = ii + 1; j = jj; }
            else             { i = ii;     j = jj + 1; }
            if (i <= 0 || j <= 0 || i > erows - 1 || j > ecols - 1) continue;
            if (f[i * ecols + j] != INSIDE) continue;

            dist = fm_min4(f, t, ecols, i, j);
            t[i * ecols + j] = dist;

            /* NS piksel degeri: komsularin yon agirlikli ortalamasi */
            kbeg = i - range > 1 ? i - range : 1;
            kend = i + range < erows - 2 ? i + range : erows - 2;
            for (k = kbeg; k <= kend; k++) {
                int km = k - 1 + (k == 1), kp = k - 1 - (k == erows - 2);
                int hw = half_width[k - i + range], l;
                int lbeg = j - hw > 1 ? j - hw : 1, lend = j + hw < ecols - 2 ? j + hw : ecols - 2;
                const unsigned char *fk = f + k * ecols, *fkm1 = f + (k - 1) * ecols,
                                    *fkp1 = f + (k + 1) * ecols;
                const float *o_km = out + (size_t)km * w, *o_kp = out + (size_t)kp * w,
                            *o_kp1 = out + (size_t)(kp + 1) * w, *o_km1 = out + (size_t)(km - 1) * w,
                            *o_k1 = out + (size_t)(k - 1) * w;
                for (l = lbeg; l <= lend; l++) {
                    int lm = l - 1 + (l == 1), lp = l - 1 - (l == ecols - 2);
                    float rx, ry, gx, gy, dir, wgt;
                    if (fk[l] == INSIDE) continue;
                    ry = (float)(i - k);
                    rx = (float)(j - l);
                    if (fkp1[l] != INSIDE) {
                        if (fkm1[l] != INSIDE)
                            gx = fabsf(o_kp1[lm] - o_kp[lm]) + fabsf(o_kp[lm] - o_km1[lm]);
                        else
                            gx = fabsf(o_kp1[lm] - o_kp[lm]) * 2.0f;
                    } else {
                        gx = fkm1[l] != INSIDE ? fabsf(o_kp[lm] - o_km1[lm]) * 2.0f : 0;
                    }
                    if (fk[l + 1] != INSIDE) {
                        if (fk[l - 1] != INSIDE)
                            gy = fabsf(o_km[lp + 1] - o_km[lm]) + fabsf(o_km[lm] - o_km[lm - 1]);
                        else
                            gy = fabsf(o_km[lp + 1] - o_km[lm]) * 2.0f;
                    } else {
                        gy = fk[l - 1] != INSIDE ? fabsf(o_km[lm] - o_km[lm - 1]) * 2.0f : 0;
                    }
                    gx = -gx;
                    dir = rx * gx + ry * gy;
                    if (fabs(dir) <= 0.01) {
                        dir = 0.000001f;
                    } else {
                        float lr = rx * rx + ry * ry, lg = gx * gx + gy * gy;
                        dir = (float)fabs(dir / sqrt((double)(lr * lg)));
                    }
                    wgt = dist_tab[(k - i + range) * side + (l - j + range)] * dir;
                    Ia += wgt * o_k1[l - 1];
                    s += wgt;
                }
            }
            out[(size_t)(i - 1) * w + (j - 1)] = (float)((double)Ia / s);

            f[i * ecols + j] = BAND;
            heap_push(&heap, i, j, dist);
        }
    }
    free(heap.a); free(f); free(t); free(half_width); free(dist_tab);
}

/* ====================================================================== */
/* cv::resize(src, (dw, dh), INTER_LINEAR), float goruntu                  */
/* ====================================================================== */
static void resize_linear_f32(const float *src, int sw, int sh, float *dst, int dw, int dh)
{
    double scale_x = 1. / ((double)dw / sw), scale_y = 1. / ((double)dh / sh);
    int *xofs = fp_alloc(PROG, sizeof(int) * (size_t)dw), *two_taps = fp_alloc(PROG, sizeof(int) * (size_t)dw);
    float *ax = fp_alloc(PROG, sizeof(float) * (size_t)dw * 2);
    float *row0 = fp_alloc(PROG, sizeof(float) * (size_t)dw), *row1 = fp_alloc(PROG, sizeof(float) * (size_t)dw);
    int dx, dy, k;

    for (dx = 0; dx < dw; dx++) {
        float fx = (float)((dx + 0.5) * scale_x - 0.5);
        int sx = (int)floorf(fx);
        fx -= (float)sx;
        two_taps[dx] = sx + 1 < sw;
        if (sx < 0) { fx = 0; sx = 0; }
        if (sx >= sw - 1) { fx = 0; sx = sw - 1; }
        xofs[dx] = sx;
        ax[dx * 2] = 1.f - fx;
        ax[dx * 2 + 1] = fx;
    }
    for (dy = 0; dy < dh; dy++) {
        float fy = (float)((dy + 0.5) * scale_y - 0.5), b0, b1;
        int sy = (int)floorf(fy), rows[2];
        fy -= (float)sy;
        b0 = 1.f - fy;
        b1 = fy;
        for (k = 0; k < 2; k++) {
            int r = sy + k;
            rows[k] = r < 0 ? 0 : r > sh - 1 ? sh - 1 : r;
        }
        for (k = 0; k < 2; k++) {
            const float *S = src + (size_t)rows[k] * sw;
            float *D = k ? row1 : row0;
            for (dx = 0; dx < dw; dx++) {
                int sx = xofs[dx];
                D[dx] = two_taps[dx] ? S[sx] * ax[dx * 2] + S[sx + 1] * ax[dx * 2 + 1]
                                     : S[sx] * ax[dx * 2];
            }
        }
        for (dx = 0; dx < dw; dx++)
            dst[(size_t)dy * dw + dx] = row0[dx] * b0 + row1[dx] * b1;
    }
    free(xofs); free(two_taps); free(ax); free(row0); free(row1);
}

/* ====================================================================== */

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

/*
 * Donmus pencerenin x-imzasindan (satir toplamlari) sirt periyodunu
 * hesaplar. Gecerli deger yoksa 0 dondurur.
 */
static float period_from_signature(const long *xs, int n)
{
    int maxima[WINDOW_H], minima[WINDOW_H], dists[2 * WINDOW_H];
    int n_max = 0, n_min = 0, n_d = 0, n_close = 0, i;
    double mv, sum = 0;

    for (i = 1; i < n - 1; i++) {
        if (xs[i] > xs[i - 1] && xs[i] >= xs[i + 1]) maxima[n_max++] = i;   /* vadiler */
        if (xs[i] < xs[i - 1] && xs[i] <= xs[i + 1]) minima[n_min++] = i;   /* sirtlar */
    }
    if (n_max + n_min < 2 + MIN_VALID_DISTANCES) return 0;

    /* ardisik tepe/cukur uzakliklarindan gecerli olanlar */
    for (i = 1; i < n_max; i++) {
        int d = maxima[i] - maxima[i - 1];
        if (d >= PERIOD_MIN && d <= PERIOD_MAX) dists[n_d++] = d;
    }
    for (i = 1; i < n_min; i++) {
        int d = minima[i] - minima[i - 1];
        if (d >= PERIOD_MIN && d <= PERIOD_MAX) dists[n_d++] = d;
    }
    if (n_d < MIN_VALID_DISTANCES) return 0;

    /* medyana en fazla 2 piksel uzak olanlarin ortalamasi */
    {
        int sorted[2 * WINDOW_H];
        memcpy(sorted, dists, sizeof(int) * (size_t)n_d);
        qsort(sorted, (size_t)n_d, sizeof(int), cmp_int);
        mv = n_d % 2 ? sorted[n_d / 2] : (sorted[n_d / 2 - 1] + sorted[n_d / 2]) / 2.0;
    }
    for (i = 0; i < n_d; i++)
        if (fabs(dists[i] - mv) <= 2) { sum += dists[i]; n_close++; }
    return n_close ? (float)(sum / n_close) : 0;
}

int main(int argc, char *argv[])
{
    Args a;
    int w, h, nw, nh, ix, iy, x, y, b1, lb, pw, ph, rw, rh, offset;
    size_t len, i;
    unsigned char *img, *mask, *ori, *tmp, *region, *inp_mask, *out;
    int *mask_dist;
    float *res, *padded, *big, *crop, *blurred;
    const int hw = WINDOW_W / 2, hh = WINDOW_H / 2;

    fp_parse_args(argc, argv, PROG, NEED_MASK | NEED_ORIENTATION, &a);
    fp_binary_stdio();
    w = a.width;
    h = a.height;
    len = (size_t)w * (size_t)h;
    img = fp_read_stdin(PROG, len);
    mask = fp_read_file(PROG, a.mask_path, len);
    ori = fp_read_file(PROG, a.orientation_path, len);
    if (w < BORDER || h < BORDER) fp_die(PROG, "goruntu cok kucuk");

    nw = (w - BORDER) / STEP + 1;
    nh = (h - BORDER) / STEP + 1;

    /* 1. Medyan (5x5) ve Gauss (3x3) on isleme */
    tmp = fp_alloc(PROG, len);
    fp_median_blur_u8(img, tmp, w, h, MEDIAN_SIZE);
    fp_gaussian_blur_u8(tmp, img, w, h, BLUR_SIZE, 0);

    /* 2. Arka plana uzaklik */
    mask_dist = fp_alloc(PROG, sizeof(int) * len);
    chessboard_distance(mask, w, h, mask_dist);

    /* 3. Izgara noktalarinda x-imzasi ile periyot */
    res = calloc((size_t)nw * (size_t)nh, sizeof(float));
    region = fp_alloc(PROG, WINDOW_W * WINDOW_H);
    if (!res) fp_die(PROG, "bellek yetersiz");
    for (iy = 0, y = BORDER; y < h - BORDER; y += STEP, iy++) {
        for (ix = 0, x = BORDER; x < w - BORDER; x += STEP, ix++) {
            long xs[WINDOW_H];
            double M[6], ang, alpha, beta;
            float angle;
            int r, c;
            if (mask_dist[(size_t)y * w + x] < MIN_BACKGROUND_DISTANCE) continue;

            /* angles = 180 - degrees(yon) (numpy'da float32) */
            angle = 180.0f - fp_decode_orientation(ori[(size_t)y * w + x]) *
                             (180.0f / 3.14159265358979323846f);
            /* cv::getRotationMatrix2D((x, y), angle, 1) + pencere kaydirmasi */
            ang = (double)angle * (3.14159265358979323846 / 180);
            alpha = cos(ang);
            beta = sin(ang);
            M[0] = alpha; M[1] = beta;  M[2] = (1 - alpha) * x - beta * y;
            M[3] = -beta; M[4] = alpha; M[5] = beta * x + (1 - alpha) * y;
            M[2] += hw - x;
            M[5] += hh - y;
            warp_affine_u8(img, w, h, region, WINDOW_W, WINDOW_H, M);

            for (r = 0; r < WINDOW_H; r++) {
                xs[r] = 0;
                for (c = 0; c < WINDOW_W; c++) xs[r] += region[r * WINDOW_W + c];
            }
            res[(size_t)iy * nw + ix] = period_from_signature(xs, WINDOW_H);
        }
    }

    /* 4. Kenar ekle, bos noktalari NS inpainting ile doldur */
    b1 = BORDER - 1 - STEP / 2;
    lb = (b1 + STEP - 1) / STEP + 1;
    pw = nw + 2 * lb;
    ph = nh + 2 * lb;
    padded = calloc((size_t)pw * (size_t)ph, sizeof(float));
    inp_mask = fp_alloc(PROG, (size_t)pw * (size_t)ph);
    if (!padded) fp_die(PROG, "bellek yetersiz");
    for (iy = 0; iy < nh; iy++)
        for (ix = 0; ix < nw; ix++)
            padded[(size_t)(iy + lb) * pw + (ix + lb)] = res[(size_t)iy * nw + ix];
    for (i = 0; i < (size_t)pw * (size_t)ph; i++) inp_mask[i] = padded[i] == 0;
    inpaint_ns(padded, inp_mask, pw, ph, (double)DIFFUSION_SIZE / STEP);

    /* 5. Goruntu boyutuna buyut, kirp, yumusat */
    rw = STEP * pw;
    rh = STEP * ph;
    big = fp_alloc(PROG, sizeof(float) * (size_t)rw * (size_t)rh);
    resize_linear_f32(padded, pw, ph, big, rw, rh);
    offset = STEP * lb - b1;
    crop = fp_alloc(PROG, sizeof(float) * len);
    blurred = fp_alloc(PROG, sizeof(float) * len);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int by = y + offset, bx = x + offset;
            crop[(size_t)y * w + x] = (by < rh && bx < rw) ? big[(size_t)by * rw + bx] : 0;
        }
    fp_gaussian_blur_f32(crop, blurred, w, h, FINAL_BLUR_SIZE, 0);

    /* 6. Arka plani sifirla ve 8-bit kodla */
    out = tmp;
    for (i = 0; i < len; i++)
        out[i] = mask[i] ? fp_encode_period(blurred[i]) : 0;
    fp_write_stdout(PROG, out, len);

    free(img); free(mask); free(ori); free(tmp); free(mask_dist); free(res);
    free(region); free(padded); free(inp_mask); free(big); free(crop); free(blurred);
    return EXIT_SUCCESS;
}
