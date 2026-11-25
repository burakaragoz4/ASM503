/*
 * segmentation - GMFS (Gradient-Magnitude Fingerprint Segmentation)
 *
 * Kullanim:  segmentation -w <genislik> -h <yukseklik> < girdi.raw > maske.raw
 *
 * Girdi : standart giristen 8-bit raw parmak izi goruntusu.
 * Cikti : standart cikisa ayni boyutta 8-bit raw maske
 *         (255 = parmak izi bolgesi, 0 = arka plan).
 *
 * pyfing.segmentation.Gmfs yonteminin C uyarlamasidir; sabitler pyfing'in
 * varsayilan GmfsParameters degerleridir (500 dpi).
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "fpimg.h"

#define PROG "segmentation"

/* pyfing GmfsParameters varsayilanlari */
#define SIGMA          (13.0 / 3.0)
#define PERCENTILE     95
#define THRESHOLD      0.2f
#define CLOSING_COUNT  6
#define OPENING_COUNT  12

/* Kenara degmeyen arka plan bilesenlerini (delikleri) doldurur. */
static void fill_holes(unsigned char *mask, int w, int h)
{
    size_t n = (size_t)w * (size_t)h, i;
    unsigned char *inv = fp_alloc(PROG, n), *is_hole;
    int *labels = fp_alloc(PROG, sizeof(int) * n);
    CCStats *st;
    int count, k;

    for (i = 0; i < n; i++) inv[i] = (unsigned char)(255 - mask[i]);  /* bitwise_not */
    count = fp_connected_components(inv, w, h, labels, &st);

    /* pyfing ile ayni (kati esitsizlikli) kosul */
    is_hole = fp_alloc(PROG, (size_t)count);
    for (k = 0; k < count; k++)
        is_hole[k] = st[k].left > 0 && st[k].left + st[k].width < w - 1 &&
                     st[k].top > 0 && st[k].top + st[k].height < h - 1;
    for (i = 0; i < n; i++)
        if (is_hole[labels[i]]) mask[i] = 255;

    free(is_hole);
    free(st);
    free(labels);
    free(inv);
}

int main(int argc, char *argv[])
{
    Args a;
    size_t n, i;
    unsigned char *img, *mask;
    float *gx, *gy, *m, *m_a;
    double *sorted;
    float norm_t;
    int gs;

    fp_parse_args(argc, argv, PROG, 0, &a);
    fp_binary_stdio();
    n = (size_t)a.width * (size_t)a.height;
    img = fp_read_stdin(PROG, n);

    gx = fp_alloc(PROG, sizeof(float) * n);
    gy = fp_alloc(PROG, sizeof(float) * n);
    m = fp_alloc(PROG, sizeof(float) * n);
    m_a = fp_alloc(PROG, sizeof(float) * n);
    sorted = fp_alloc(PROG, sizeof(double) * n);
    mask = fp_alloc(PROG, n);

    /* 1. Gradyan buyuklugu: m = |(gx, gy)| */
    fp_sobel3(img, gx, gy, a.width, a.height);
    for (i = 0; i < n; i++)
        m[i] = sqrtf(gx[i] * gx[i] + gy[i] * gy[i]);

    /* 2. Gauss filtresiyle ortalama gradyan buyuklugu */
    gs = (int)ceil(3 * SIGMA) * 2 + 1;
    fp_gaussian_blur_f32(m, m_a, a.width, a.height, gs, SIGMA);

    /* 3. Esik: m'nin 95. yuzdeligi * 0.2 (numpy'da float32 hesaplanir) */
    for (i = 0; i < n; i++) sorted[i] = m[i];
    norm_t = (float)fp_percentile_f64(sorted, n, PERCENTILE) * THRESHOLD;

    /* 4. Esikleme */
    for (i = 0; i < n; i++) mask[i] = m_a[i] > norm_t ? 255 : 0;

    /* 5. Kapama (closing): kucuk delik ve girintileri doldurur */
    fp_dilate_cross(mask, a.width, a.height, CLOSING_COUNT, FP_BORDER_NEUTRAL);
    fp_erode_cross(mask, a.width, a.height, CLOSING_COUNT, FP_BORDER_NEUTRAL);

    /* 6. En buyuk bileseni birak, icindeki delikleri doldur */
    fp_keep_largest_component(mask, a.width, a.height);
    fill_holes(mask, a.width, a.height);

    /* 7. Acma (opening, kenar degeri 0): kucuk cikintilari temizler */
    fp_erode_cross(mask, a.width, a.height, OPENING_COUNT, 0);
    fp_dilate_cross(mask, a.width, a.height, OPENING_COUNT, 0);

    /* 8. Acma yeni bilesenler olusturmus olabilir: yine en buyugu birak */
    fp_keep_largest_component(mask, a.width, a.height);

    fp_write_stdout(PROG, mask, n);

    free(img); free(gx); free(gy); free(m); free(m_a); free(sorted); free(mask);
    return EXIT_SUCCESS;
}
