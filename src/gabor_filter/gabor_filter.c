/*
 * gabor_filter - GBFEN (Gabor-Based Fingerprint ENhancement)
 *
 * Kullanim:  gabor_filter -w <genislik> -h <yukseklik> -s <maske.raw>
 *                         -o <orientasyon.raw> -f <frekans.raw>
 *                         < girdi.raw > iyilestirilmis.raw
 *
 * Girdi : standart giristen 8-bit raw parmak izi goruntusu; -s, -o, -f ile
 *         segmentation, orientation ve frequency araclarinin ciktilari.
 * Cikti : standart cikisa iyilestirilmis 8-bit raw goruntu (siyah zemin
 *         uzerinde beyaz sirtlar, pyfing ile ayni).
 *
 * pyfing.enhancement.Gbfen yonteminin C uyarlamasidir; sabitler pyfing'in
 * varsayilan GbfenParameters degerleridir. Her piksel, kendi yonune ve sirt
 * periyoduna en yakin Gabor cekirdegiyle filtrelenir (cv::filter2D,
 * BORDER_REFLECT_101). pyfing'deki gibi maske filtrelemede kullanilmaz;
 * -s argumani arayuz tutarliligi icin alinir.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "fpimg.h"

#define PROG "gabor_filter"

/* pyfing GbfenParameters varsayilanlari */
#define ORIENTATIONS_COUNT  12
#define PERIODS_COUNT       7
#define PERIOD_MIN          5
#define PERIOD_MAX          16

#define PI_D 3.14159265358979323846
#define PI_F 3.14159265358979323846f

typedef struct {
    int size;       /* cekirdek kenar uzunlugu (tek) */
    float *k;       /* size*size katsayi, satir oncelikli */
} Kernel;

/*
 * cv::getGaborKernel((s, s), sigma, theta, lambd, gamma=1, psi=0, CV_32F),
 * ardindan pyfing'deki gibi ortalamasi cikarilip L2 normuna bolunur
 * (numpy float32 islemleriyle ayni sirada).
 */
static void gabor_kernel(double period, double orientation, Kernel *out)
{
    double sigma = 5 * period / 12, theta = PI_D / 2 - orientation;
    double c = cos(theta), s = sin(theta);
    double ex = -0.5 / (sigma * sigma), ey = ex, cscale = PI_D * 2 / period;
    int size = (int)ceil(3 * sigma) * 2 + 1, half = size / 2, x, y;
    size_t n = (size_t)size * (size_t)size, i;
    float *k = fp_alloc(PROG, sizeof(float) * n), *sq = fp_alloc(PROG, sizeof(float) * n);
    float mean, norm;

    for (y = -half; y <= half; y++)
        for (x = -half; x <= half; x++) {
            double xr = x * c + y * s, yr = -x * s + y * c;
            double v = exp(ex * xr * xr + ey * yr * yr) * cos(cscale * xr);
            k[(size_t)(half - y) * size + (half - x)] = (float)v;
        }
    mean = fp_sum_f32(k, n) / (float)n;               /* f -= f.mean() */
    for (i = 0; i < n; i++) k[i] -= mean;
    for (i = 0; i < n; i++) sq[i] = k[i] * k[i];      /* sqrt((f**2).sum()) */
    norm = sqrtf(fp_sum_f32(sq, n));
    for (i = 0; i < n; i++) k[i] /= norm;
    free(sq);
    out->size = size;
    out->k = k;
}

/* pyfing _discretize_ridge_periods (float32 islemleri) */
static int period_index(float p)
{
    float v = p < (float)PERIOD_MIN ? (float)PERIOD_MIN : p > (float)PERIOD_MAX ? (float)PERIOD_MAX : p;
    v = (v - (float)PERIOD_MIN) / (float)(PERIOD_MAX - PERIOD_MIN) * (float)(PERIODS_COUNT - 1);
    return (int)nearbyintf(v);
}

/* pyfing _discretize_orientations (float32 islemleri) */
static int orientation_index(float o)
{
    float m = fmodf(o, PI_F), v;
    if (m != 0 && m < 0) m += PI_F;
    v = m / PI_F * (float)ORIENTATIONS_COUNT;
    return (int)nearbyintf(v) % ORIENTATIONS_COUNT;
}

int main(int argc, char *argv[])
{
    Args a;
    int w, h, x, y, p, o, nk = PERIODS_COUNT * ORIENTATIONS_COUNT;
    size_t len, i;
    unsigned char *img, *mask, *ori, *frq, *out;
    int *gbi;
    char *needed;
    Kernel bank[PERIODS_COUNT * ORIENTATIONS_COUNT];
    double step = (double)(PERIOD_MAX - PERIOD_MIN) / (PERIODS_COUNT - 1);

    fp_parse_args(argc, argv, PROG, NEED_MASK | NEED_ORIENTATION | NEED_FREQUENCY, &a);
    fp_binary_stdio();
    w = a.width;
    h = a.height;
    len = (size_t)w * (size_t)h;
    img = fp_read_stdin(PROG, len);
    mask = fp_read_file(PROG, a.mask_path, len);
    ori = fp_read_file(PROG, a.orientation_path, len);
    frq = fp_read_file(PROG, a.frequency_path, len);

    /* 1. Her piksel icin Gabor bankasi indeksi: periyot * 12 + yon */
    gbi = fp_alloc(PROG, sizeof(int) * len);
    needed = calloc((size_t)nk, 1);
    if (!needed) fp_die(PROG, "bellek yetersiz");
    for (i = 0; i < len; i++) {
        gbi[i] = period_index(fp_decode_period(frq[i])) * ORIENTATIONS_COUNT +
                 orientation_index(fp_decode_orientation(ori[i]));
        needed[gbi[i]] = 1;
    }

    /* 2. Yalnizca kullanilan cekirdekleri olustur
     *    periyotlar: linspace(5, 16, 7); yonler: arange(0, pi, pi/12) */
    for (p = 0; p < PERIODS_COUNT; p++)
        for (o = 0; o < ORIENTATIONS_COUNT; o++) {
            int idx = p * ORIENTATIONS_COUNT + o;
            double period = p == PERIODS_COUNT - 1 ? PERIOD_MAX : p * step + PERIOD_MIN;
            bank[idx].k = NULL;
            if (needed[idx]) gabor_kernel(period, o * (PI_D / ORIENTATIONS_COUNT), &bank[idx]);
        }

    /* 3. Ters cevrilmis goruntuyu (beyaz sirt, siyah zemin) her pikselde
     *    kendi cekirdegiyle filtrele (korelasyon, BORDER_REFLECT_101) */
    for (i = 0; i < len; i++) img[i] = (unsigned char)(255 - img[i]);
    out = fp_alloc(PROG, len);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            const Kernel *kr = &bank[gbi[(size_t)y * w + x]];
            int half = kr->size / 2, dy, dx;
            double sum = 0, v;
            for (dy = -half; dy <= half; dy++) {
                const unsigned char *row = img + (size_t)fp_reflect101(y + dy, h) * w;
                const float *kk = kr->k + (size_t)(dy + half) * kr->size + half;
                if (x - half >= 0 && x + half < w) {
                    const unsigned char *px = row + x;
                    for (dx = -half; dx <= half; dx++) sum += (double)kk[dx] * px[dx];
                } else {
                    for (dx = -half; dx <= half; dx++)
                        sum += (double)kk[dx] * row[fp_reflect101(x + dx, w)];
                }
            }
            v = nearbyint(sum);
            out[(size_t)y * w + x] = (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v);
        }

    fp_write_stdout(PROG, out, len);

    for (i = 0; i < (size_t)nk; i++) free(bank[i].k);
    free(img); free(mask); free(ori); free(frq); free(out); free(gbi); free(needed);
    return EXIT_SUCCESS;
}
