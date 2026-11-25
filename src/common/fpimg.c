/*
 * fpimg - parmak izi araclarinin ortak goruntu isleme fonksiyonlari.
 * Ayrintilar icin fpimg.h'ye bakiniz.
 */
#include "fpimg.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#define MAX_DIM 100000L

/* ====================================================================== */
/* Komut satiri ve G/C                                                     */
/* ====================================================================== */

void fp_die(const char *prog, const char *msg)
{
    fprintf(stderr, "%s: %s\n", prog, msg);
    exit(EXIT_FAILURE);
}

void *fp_alloc(const char *prog, size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) fp_die(prog, "bellek yetersiz");
    return p;
}

static void usage(const char *prog, int needs)
{
    fprintf(stderr, "Kullanim: %s -w <genislik> -h <yukseklik>", prog);
    if (needs & NEED_MASK)        fprintf(stderr, " -s <segmentasyon_maskesi>");
    if (needs & NEED_ORIENTATION) fprintf(stderr, " -o <orientasyon_goruntusu>");
    if (needs & NEED_FREQUENCY)   fprintf(stderr, " -f <frekans_goruntusu>");
    fprintf(stderr, " < girdi.raw > cikti.raw\n");
    fprintf(stderr, "  -w, --width         goruntu genisligi\n");
    fprintf(stderr, "  -h, --height        goruntu yuksekligi\n");
    if (needs & NEED_MASK)
        fprintf(stderr, "  -s, --segmentation  segmentation aracinin urettigi maske dosyasi\n");
    if (needs & NEED_ORIENTATION)
        fprintf(stderr, "  -o, --orientation   orientation aracinin urettigi dosya\n");
    if (needs & NEED_FREQUENCY)
        fprintf(stderr, "  -f, --frequency     frequency aracinin urettigi dosya\n");
}

static int parse_dim(const char *prog, const char *s, const char *name)
{
    char *end;
    long v;
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno || end == s || *end != '\0' || v <= 0 || v > MAX_DIM) {
        fprintf(stderr, "%s: gecersiz %s degeri: '%s'\n", prog, name, s);
        exit(EXIT_FAILURE);
    }
    return (int)v;
}

void fp_parse_args(int argc, char *argv[], const char *prog, int needs, Args *a)
{
    int i;
    memset(a, 0, sizeof *a);
    for (i = 1; i < argc; i++) {
        const char *opt = argv[i];
        const char **target = NULL;
        int is_w = !strcmp(opt, "-w") || !strcmp(opt, "--width");
        int is_h = !strcmp(opt, "-h") || !strcmp(opt, "--height");

        if ((needs & NEED_MASK) && (!strcmp(opt, "-s") || !strcmp(opt, "--segmentation")))
            target = &a->mask_path;
        else if ((needs & NEED_ORIENTATION) && (!strcmp(opt, "-o") || !strcmp(opt, "--orientation")))
            target = &a->orientation_path;
        else if ((needs & NEED_FREQUENCY) && (!strcmp(opt, "-f") || !strcmp(opt, "--frequency")))
            target = &a->frequency_path;
        else if (!is_w && !is_h) {
            usage(prog, needs);
            fprintf(stderr, "%s: bilinmeyen arguman: '%s'\n", prog, opt);
            exit(EXIT_FAILURE);
        }
        if (++i >= argc) {
            usage(prog, needs);
            fprintf(stderr, "%s: '%s' icin deger eksik\n", prog, opt);
            exit(EXIT_FAILURE);
        }
        if (is_w)       a->width = parse_dim(prog, argv[i], "genislik");
        else if (is_h)  a->height = parse_dim(prog, argv[i], "yukseklik");
        else            *target = argv[i];
    }
    if (a->width == 0 || a->height == 0 ||
        ((needs & NEED_MASK) && !a->mask_path) ||
        ((needs & NEED_ORIENTATION) && !a->orientation_path) ||
        ((needs & NEED_FREQUENCY) && !a->frequency_path)) {
        usage(prog, needs);
        fp_die(prog, "eksik arguman");
    }
}

void fp_binary_stdio(void)
{
#ifdef _WIN32
    /* Metin modu 0x0A <-> 0x0D0A donusumu yapip ikili veriyi bozar */
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

static unsigned char *read_exact(const char *prog, FILE *fp, const char *name, size_t n)
{
    unsigned char *buf = fp_alloc(prog, n);
    if (fread(buf, 1, n, fp) != n) {
        fprintf(stderr, "%s: %s beklenenden kisa (%lu byte gerekli)\n",
                prog, name, (unsigned long)n);
        exit(EXIT_FAILURE);
    }
    if (fgetc(fp) != EOF) {
        fprintf(stderr, "%s: %s beklenenden uzun (-w/-h degerlerini kontrol edin)\n",
                prog, name);
        exit(EXIT_FAILURE);
    }
    return buf;
}

unsigned char *fp_read_stdin(const char *prog, size_t n)
{
    return read_exact(prog, stdin, "standart giris", n);
}

unsigned char *fp_read_file(const char *prog, const char *path, size_t n)
{
    unsigned char *buf;
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "%s: %s: dosya acilamadi\n", prog, path);
        exit(EXIT_FAILURE);
    }
    buf = read_exact(prog, fp, path, n);
    fclose(fp);
    return buf;
}

void fp_write_stdout(const char *prog, const unsigned char *buf, size_t n)
{
    if (fwrite(buf, 1, n, stdout) != n || fflush(stdout) != 0)
        fp_die(prog, "standart cikisa yazilamadi");
}

/* ====================================================================== */
/* Kenar indeksleri (cv::borderInterpolate)                                */
/* ====================================================================== */

static int reflect_generic(int p, int len, int delta)
{
    if (p >= 0 && p < len) return p;
    if (len == 1) return 0;
    do {
        if (p < 0) p = -p - 1 + delta;
        else       p = len - 1 - (p - len) - delta;
    } while (p < 0 || p >= len);
    return p;
}

int fp_reflect101(int p, int len) { return reflect_generic(p, len, 1); }
int fp_reflect(int p, int len)    { return reflect_generic(p, len, 0); }
int fp_replicate(int p, int len)  { return p < 0 ? 0 : (p >= len ? len - 1 : p); }

/* ====================================================================== */
/* Gauss filtreleri                                                        */
/* ====================================================================== */

/* cv::getGaussianKernel (getGaussianKernelBitExact ile ayni islem sirasi) */
void fp_gaussian_kernel(int n, double sigma, double *k)
{
    static const double t3[] = {0.25, 0.5, 0.25};
    static const double t5[] = {0.0625, 0.25, 0.375, 0.25, 0.0625};
    static const double t7[] = {0.03125, 0.109375, 0.21875, 0.28125,
                                0.21875, 0.109375, 0.03125};
    double sigma_x, scale2, sum = 0.0;
    int i, x, half = (n - 1) / 2;

    if (sigma <= 0 && (n == 1 || n == 3 || n == 5 || n == 7)) {
        const double *t = n == 3 ? t3 : n == 5 ? t5 : t7;
        if (n == 1) { k[0] = 1.0; return; }
        for (i = 0; i < n; i++) k[i] = t[i];
        return;
    }
    sigma_x = sigma > 0 ? sigma : n * 0.15 + 0.35;   /* ((n-1)*0.5-1)*0.3+0.8 */
    scale2 = -0.125 / (sigma_x * sigma_x);
    for (i = 0, x = 1 - n; i < half; i++, x += 2) {  /* x = 2*(i - merkez) */
        k[i] = exp((double)x * x * scale2);
        sum += k[i];
    }
    sum = sum * 2.0 + 1.0;                           /* merkez agirligi 1 */
    for (i = 0; i < half; i++) {
        k[i] /= sum;
        k[n - 1 - i] = k[i];
    }
    k[half] = 1.0 / sum;
}

/* Ayrilabilir (separable) float filtre: once satirlar, sonra sutunlar */
void fp_gaussian_blur_f32(const float *src, float *dst, int w, int h,
                          int ksize, double sigma)
{
    double *k = fp_alloc("fpimg", sizeof(double) * (size_t)ksize);
    float *kf = fp_alloc("fpimg", sizeof(float) * (size_t)ksize);
    float *tmp = fp_alloc("fpimg", sizeof(float) * (size_t)w * (size_t)h);
    int *idx = fp_alloc("fpimg", sizeof(int) * (size_t)(ksize + (w > h ? w : h)));
    int r = ksize / 2, x, y, i;

    fp_gaussian_kernel(ksize, sigma, k);
    for (i = 0; i < ksize; i++) kf[i] = (float)k[i];   /* CV_32F cekirdek */

    /* yatay */
    for (x = -r; x < w + r; x++) idx[x + r] = fp_reflect101(x, w);
    for (y = 0; y < h; y++) {
        const float *row = src + (size_t)y * w;
        for (x = 0; x < w; x++) {
            double s = 0.0;
            for (i = 0; i < ksize; i++) s += (double)kf[i] * row[idx[x + i]];
            tmp[(size_t)y * w + x] = (float)s;
        }
    }
    /* dikey */
    for (y = -r; y < h + r; y++) idx[y + r] = fp_reflect101(y, h);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            double s = 0.0;
            for (i = 0; i < ksize; i++) s += (double)kf[i] * tmp[(size_t)idx[y + i] * w + x];
            dst[(size_t)y * w + x] = (float)s;
        }
    }
    free(k); free(kf); free(tmp); free(idx);
}

/* Cekirdegi 2^bits olcekli tamsayilara cevirir; yuvarlama hatasi kenardan
 * merkeze tasinir, merkez toplami tam 2^bits yapar
 * (OpenCV getGaussianKernelFixedPoint_ED). */
static void kernel_fixed_point(const double *k, int n, int bits, long *out)
{
    double mult = (double)(1L << bits), err = 0.0;
    long sum = 0;
    int i, half = n / 2;
    for (i = 0; i < half; i++) {
        double adj = k[i] * mult + err;
        long v = (long)nearbyint(adj);    /* yarim degerde ciftte yuvarla */
        err = adj - (double)v;
        out[i] = out[n - 1 - i] = v;
        sum += v;
    }
    out[half] = (1L << bits) - 2 * sum;
}

void fp_gaussian_blur_u8(const unsigned char *src, unsigned char *dst,
                         int w, int h, int ksize, double sigma)
{
    double *k = fp_alloc("fpimg", sizeof(double) * (size_t)ksize);
    long *kf = fp_alloc("fpimg", sizeof(long) * (size_t)ksize);
    unsigned long *tmp = fp_alloc("fpimg", sizeof(unsigned long) * (size_t)w * (size_t)h);
    int *idx = fp_alloc("fpimg", sizeof(int) * (size_t)(ksize + (w > h ? w : h)));
    int r = ksize / 2, x, y, i;

    fp_gaussian_kernel(ksize, sigma, k);
    kernel_fixed_point(k, ksize, 8, kf);      /* ufixedpoint16: 8 kesir biti */

    /* yatay: sonuc 8 kesir bitli tam sayi (kayipsiz) */
    for (x = -r; x < w + r; x++) idx[x + r] = fp_reflect101(x, w);
    for (y = 0; y < h; y++) {
        const unsigned char *row = src + (size_t)y * w;
        for (x = 0; x < w; x++) {
            unsigned long s = 0;
            for (i = 0; i < ksize; i++) s += (unsigned long)kf[i] * row[idx[x + i]];
            tmp[(size_t)y * w + x] = s;
        }
    }
    /* dikey: 16 kesir biti, sonra yuvarlayarak 8 bite */
    for (y = -r; y < h + r; y++) idx[y + r] = fp_reflect101(y, h);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            unsigned long long s = 0;
            for (i = 0; i < ksize; i++)
                s += (unsigned long long)kf[i] * tmp[(size_t)idx[y + i] * w + x];
            s = (s + 0x8000ULL) >> 16;
            dst[(size_t)y * w + x] = (unsigned char)(s > 255 ? 255 : s);
        }
    }
    free(k); free(kf); free(tmp); free(idx);
}

void fp_median_blur_u8(const unsigned char *src, unsigned char *dst,
                       int w, int h, int ksize)
{
    int r = ksize / 2, x, y, dx, dy, v, need = ksize * ksize / 2 + 1;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            unsigned short hist[256];
            int count = 0;
            memset(hist, 0, sizeof hist);
            for (dy = -r; dy <= r; dy++) {
                const unsigned char *row = src + (size_t)fp_replicate(y + dy, h) * w;
                for (dx = -r; dx <= r; dx++) hist[row[fp_replicate(x + dx, w)]]++;
            }
            for (v = 0; v < 256; v++) {
                count += hist[v];
                if (count >= need) break;
            }
            dst[(size_t)y * w + x] = (unsigned char)v;
        }
    }
}

void fp_sobel3(const unsigned char *src, float *gx, float *gy, int w, int h)
{
    int x, y;
    for (y = 0; y < h; y++) {
        const unsigned char *up = src + (size_t)fp_reflect101(y - 1, h) * w;
        const unsigned char *mid = src + (size_t)y * w;
        const unsigned char *dn = src + (size_t)fp_reflect101(y + 1, h) * w;
        for (x = 0; x < w; x++) {
            int l = fp_reflect101(x - 1, w), r = fp_reflect101(x + 1, w);
            int vx = (up[r] - up[l]) + 2 * (mid[r] - mid[l]) + (dn[r] - dn[l]);
            int vy = (dn[l] - up[l]) + 2 * (dn[x] - up[x]) + (dn[r] - up[r]);
            gx[(size_t)y * w + x] = (float)vx;
            gy[(size_t)y * w + x] = (float)vy;
        }
    }
}

/* ====================================================================== */
/* Istatistik                                                              */
/* ====================================================================== */

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

double fp_percentile_f64(double *v, size_t n, double q)
{
    double idx, g, a, b, diff;
    size_t lo, hi;
    if (n == 0) return 0.0;
    qsort(v, n, sizeof *v, cmp_double);
    idx = q / 100.0 * (double)(n - 1);
    lo = (size_t)floor(idx);
    hi = lo + 1 < n ? lo + 1 : n - 1;
    g = idx - (double)lo;
    a = v[lo];
    b = v[hi];
    diff = b - a;
    /* numpy _lerp: t >= 0.5 icin b - (b-a)*(1-t) */
    return g >= 0.5 ? b - diff * (1.0 - g) : a + diff * g;
}

/* numpy pairwise_sum (float32): <8 eleman sirali, <=128 eleman 8 akumulator,
 * daha buyukleri ikiye bolunerek (8'in katina hizali) toplanir. */
float fp_sum_f32(const float *v, size_t n)
{
    if (n < 8) {
        float s = 0.0f;
        size_t i;
        for (i = 0; i < n; i++) s += v[i];
        return s;
    }
    if (n <= 128) {
        float r[8], s;
        size_t i;
        for (i = 0; i < 8; i++) r[i] = v[i];
        for (i = 8; i < n - (n % 8); i += 8) {
            r[0] += v[i + 0]; r[1] += v[i + 1]; r[2] += v[i + 2]; r[3] += v[i + 3];
            r[4] += v[i + 4]; r[5] += v[i + 5]; r[6] += v[i + 6]; r[7] += v[i + 7];
        }
        s = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
        for (; i < n; i++) s += v[i];
        return s;
    } else {
        size_t n2 = n / 2;
        n2 -= n2 % 8;
        return fp_sum_f32(v, n2) + fp_sum_f32(v + n2, n - n2);
    }
}

/* ====================================================================== */
/* Araclar arasi veri kodlamasi                                            */
/* ====================================================================== */

#define FP_PI_F 3.14159265358979323846f

unsigned char fp_encode_orientation(float theta)
{
    float t = fmodf(theta, FP_PI_F);
    float v;
    if (t < 0) t += FP_PI_F;
    v = nearbyintf(t * 255.0f / FP_PI_F);
    return (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v);
}

float fp_decode_orientation(unsigned char v)
{
    return (float)v * FP_PI_F / 255.0f;
}

unsigned char fp_encode_period(float period)
{
    float v = nearbyintf(period * 10.0f);
    return (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v);
}

float fp_decode_period(unsigned char v)
{
    return (float)v / 10.0f;
}

/* ====================================================================== */
/* Morfoloji                                                               */
/* ====================================================================== */

static void morph_cross(unsigned char *img, int w, int h, int iterations,
                        int border_value, int dilate)
{
    unsigned char *src = fp_alloc("fpimg", (size_t)w * (size_t)h);
    int it, x, y;
    for (it = 0; it < iterations; it++) {
        memcpy(src, img, (size_t)w * (size_t)h);
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                int v = src[(size_t)y * w + x], n[4], k;
                n[0] = y > 0     ? src[(size_t)(y - 1) * w + x] : border_value;
                n[1] = y < h - 1 ? src[(size_t)(y + 1) * w + x] : border_value;
                n[2] = x > 0     ? src[(size_t)y * w + x - 1] : border_value;
                n[3] = x < w - 1 ? src[(size_t)y * w + x + 1] : border_value;
                for (k = 0; k < 4; k++) {
                    if (n[k] == FP_BORDER_NEUTRAL) continue;
                    if (dilate ? n[k] > v : n[k] < v) v = n[k];
                }
                img[(size_t)y * w + x] = (unsigned char)v;
            }
        }
    }
    free(src);
}

void fp_dilate_cross(unsigned char *img, int w, int h, int iterations, int border_value)
{
    morph_cross(img, w, h, iterations, border_value, 1);
}

void fp_erode_cross(unsigned char *img, int w, int h, int iterations, int border_value)
{
    morph_cross(img, w, h, iterations, border_value, 0);
}

/* ====================================================================== */
/* Bagli bilesenler                                                        */
/* ====================================================================== */

int fp_connected_components(const unsigned char *img, int w, int h,
                            int *labels, CCStats **out_stats)
{
    size_t n = (size_t)w * (size_t)h, i, sp;
    size_t *stack = fp_alloc("fpimg", sizeof(size_t) * n);
    int cap = 64, count = 1, x, y, dx, dy;
    CCStats *st = fp_alloc("fpimg", sizeof(CCStats) * (size_t)cap);

    for (i = 0; i < n; i++) labels[i] = -1;

    /* Etiket 0: tum sifir pikseller (OpenCV'deki gibi tek "arka plan") */
    st[0].left = w; st[0].top = h; st[0].width = -1; st[0].height = -1; st[0].area = 0;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            if (!img[(size_t)y * w + x]) {
                CCStats *s = &st[0];
                labels[(size_t)y * w + x] = 0;
                if (x < s->left) s->left = x;
                if (y < s->top) s->top = y;
                if (x > s->width) s->width = x;     /* gecici: max x */
                if (y > s->height) s->height = y;   /* gecici: max y */
                s->area++;
            }
    if (st[0].area) {
        st[0].width = st[0].width - st[0].left + 1;
        st[0].height = st[0].height - st[0].top + 1;
    } else {
        st[0].left = st[0].top = st[0].width = st[0].height = 0;
    }

    /* Sifir olmayan bilesenler: tarama sirasinda ilk pikselden itibaren
     * 8 komsulukla doldurma */
    for (i = 0; i < n; i++) {
        int minx, miny, maxx, maxy;
        long area = 0;
        if (labels[i] != -1) continue;
        if (count == cap) {
            CCStats *tmp;
            cap *= 2;
            tmp = realloc(st, sizeof(CCStats) * (size_t)cap);
            if (!tmp) fp_die("fpimg", "bellek yetersiz");
            st = tmp;
        }
        minx = maxx = (int)(i % (size_t)w);
        miny = maxy = (int)(i / (size_t)w);
        labels[i] = count;
        sp = 0;
        stack[sp++] = i;
        while (sp) {
            size_t p = stack[--sp];
            int px = (int)(p % (size_t)w), py = (int)(p / (size_t)w);
            area++;
            if (px < minx) minx = px;
            if (px > maxx) maxx = px;
            if (py < miny) miny = py;
            if (py > maxy) maxy = py;
            for (dy = -1; dy <= 1; dy++) {
                for (dx = -1; dx <= 1; dx++) {
                    int qx = px + dx, qy = py + dy;
                    size_t q;
                    if (qx < 0 || qy < 0 || qx >= w || qy >= h) continue;
                    q = (size_t)qy * w + qx;
                    if (labels[q] == -1) {
                        labels[q] = count;
                        stack[sp++] = q;
                    }
                }
            }
        }
        st[count].left = minx;
        st[count].top = miny;
        st[count].width = maxx - minx + 1;
        st[count].height = maxy - miny + 1;
        st[count].area = area;
        count++;
    }
    free(stack);
    *out_stats = st;
    return count;
}

void fp_keep_largest_component(unsigned char *mask, int w, int h)
{
    size_t n = (size_t)w * (size_t)h, i;
    int *labels = fp_alloc("fpimg", sizeof(int) * n);
    CCStats *st;
    int count = fp_connected_components(mask, w, h, labels, &st), best = 1, k;
    if (count > 1) {
        for (k = 2; k < count; k++)
            if (st[k].area > st[best].area) best = k;   /* esitlikte ilki */
        for (i = 0; i < n; i++)
            if (labels[i] != best) mask[i] = 0;
    }
    free(labels);
    free(st);
}
