/*
 * orientation - GBFOE (Gradient-Based Fingerprint Orientation Estimation)
 *
 * Kullanim:  orientation -w <genislik> -h <yukseklik> -s <maske.raw>
 *                        < girdi.raw > orientasyon.raw
 *
 * Girdi : standart giristen 8-bit raw parmak izi goruntusu ve -s ile
 *         segmentation aracinin urettigi maske.
 * Cikti : standart cikisa her piksel icin sirt yonu, 8-bit raw:
 *         deger = round(yon * 255 / pi), yon [0, pi) radyan
 *         (0 = yatay, saat yonunun tersine artar; pyfing ile ayni kural).
 *
 * pyfing.orientations.Gbfoe yonteminin C uyarlamasidir; sabitler pyfing'in
 * varsayilan GbfoeParameters degerleridir.
 */
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "fpimg.h"

#define PROG "orientation"

/* pyfing GbfoeParameters varsayilanlari */
#define SIGMA_BASE        27
#define SIGMA_MULTIPLIER  43
#define SIGMA_SMOOTH      1.25
#define MEDIAN_SIZE       5
#define PERCENTILE        19

#define PI_F 3.14159265358979323846f

/* cv::phase (cv::fastAtan32): OpenCV'nin polinom atan2 yaklasimi, [0, 2pi) */
static float cv_phase(float x, float y)
{
    static const float p1 = 0.9997878412794807f * (float)(180 / 3.14159265358979323846);
    static const float p3 = -0.3258083974640975f * (float)(180 / 3.14159265358979323846);
    static const float p5 = 0.1555786518463281f * (float)(180 / 3.14159265358979323846);
    static const float p7 = -0.04432655554792128f * (float)(180 / 3.14159265358979323846);
    float ax = fabsf(x), ay = fabsf(y), a, c, c2;
    if (ax >= ay) {
        c = ay / (ax + (float)DBL_EPSILON);
        c2 = c * c;
        a = (((p7 * c2 + p5) * c2 + p3) * c2 + p1) * c;
    } else {
        c = ax / (ay + (float)DBL_EPSILON);
        c2 = c * c;
        a = 90.f - (((p7 * c2 + p5) * c2 + p3) * c2 + p1) * c;
    }
    if (x < 0) a = 180.f - a;
    if (y < 0) a = 360.f - a;
    return a * (float)(3.14159265358979323846 / 180);
}

/*
 * Gauss penceresi ile n = S(gx^2) - S(gy^2), d = S(-2 gx gy) ve yon gucu
 * (coherence) = sqrt(n^2 + d^2) / (S(gx^2) + S(gy^2)) hesaplanir.
 * Not: pyfing S(gx^2) ve S(gy^2) icin sigma=0 (pencereden hesaplanan),
 * d icin verilen sigmayi kullanir; ayni sekilde yapilir.
 */
static void compute_n_d_strengths(double sigma, const float *gx2, const float *gy2,
                                  const float *g2xy, const unsigned char *mask,
                                  int w, int h, float *n, float *d, float *strengths)
{
    size_t len = (size_t)w * (size_t)h, i;
    int ksize = (int)ceil(3 * sigma) * 2 + 1;
    float *sx = fp_alloc(PROG, sizeof(float) * len);
    float *sy = fp_alloc(PROG, sizeof(float) * len);

    fp_gaussian_blur_f32(gx2, sx, w, h, ksize, 0);
    fp_gaussian_blur_f32(gy2, sy, w, h, ksize, 0);
    fp_gaussian_blur_f32(g2xy, d, w, h, ksize, sigma);
    for (i = 0; i < len; i++) {
        float sum = sx[i] + sy[i];
        n[i] = sx[i] - sy[i];
        strengths[i] = (sum != 0 && mask[i]) ? sqrtf(n[i] * n[i] + d[i] * d[i]) / sum : 0.0f;
    }
    free(sx);
    free(sy);
}

int main(int argc, char *argv[])
{
    Args a;
    size_t len, i, cnt;
    int w, h, smooth_w;
    unsigned char *img, *mask, *tmp, *out;
    float *gx, *gy, *gx2, *gy2, *g2xy, *n, *d, *strengths, *masked_s;
    double *values, v1, v2;
    float avg_s, sigma;

    fp_parse_args(argc, argv, PROG, NEED_MASK, &a);
    fp_binary_stdio();
    w = a.width;
    h = a.height;
    len = (size_t)w * (size_t)h;
    img = fp_read_stdin(PROG, len);
    mask = fp_read_file(PROG, a.mask_path, len);
    tmp = fp_alloc(PROG, len);

    /* 1. Kontrast germe: maske icindeki %19 ve %81 yuzdelikleri 0..255'e */
    values = fp_alloc(PROG, sizeof(double) * len);
    for (i = 0, cnt = 0; i < len; i++)
        if (mask[i]) values[cnt++] = img[i];
    if (cnt == 0) {                       /* maske bossa tum goruntu */
        for (i = 0; i < len; i++) values[i] = img[i];
        cnt = len;
    }
    v1 = fp_percentile_f64(values, cnt, PERCENTILE);
    v2 = fp_percentile_f64(values, cnt, 100 - PERCENTILE);
    if (v2 > v1) {
        for (i = 0; i < len; i++) {
            double v = ((double)img[i] - v1) * 255 / (v2 - v1);
            img[i] = (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v);  /* astype: kesme */
        }
    }
    free(values);

    /* 2. Gauss (9x9, sigma 1.25) ve 3. medyan (5x5) yumusatma */
    smooth_w = (int)ceil(3 * SIGMA_SMOOTH) * 2 + 1;
    fp_gaussian_blur_u8(img, tmp, w, h, smooth_w, SIGMA_SMOOTH);
    fp_median_blur_u8(tmp, img, w, h, MEDIAN_SIZE);

    /* 4. Sobel gradyanlari; arka plan gradyanlari kullanilmaz */
    gx = fp_alloc(PROG, sizeof(float) * len);
    gy = fp_alloc(PROG, sizeof(float) * len);
    fp_sobel3(img, gx, gy, w, h);
    gx2 = fp_alloc(PROG, sizeof(float) * len);
    gy2 = fp_alloc(PROG, sizeof(float) * len);
    g2xy = fp_alloc(PROG, sizeof(float) * len);
    for (i = 0; i < len; i++) {
        if (!mask[i]) gx[i] = gy[i] = 0;
        gx2[i] = gx[i] * gx[i];
        gy2[i] = gy[i] * gy[i];
        g2xy[i] = -2.0f * gx[i] * gy[i];   /* eksi: saat yonunun tersi */
    }

    /* 5. Once sigma_base ile yon gucleri */
    n = fp_alloc(PROG, sizeof(float) * len);
    d = fp_alloc(PROG, sizeof(float) * len);
    strengths = fp_alloc(PROG, sizeof(float) * len);
    compute_n_d_strengths(SIGMA_BASE, gx2, gy2, g2xy, mask, w, h, n, d, strengths);

    /* 6. Ortalama guce gore son sigma (numpy'daki gibi float32) */
    masked_s = fp_alloc(PROG, sizeof(float) * len);
    for (i = 0, cnt = 0; i < len; i++)
        if (mask[i]) masked_s[cnt++] = strengths[i];
    avg_s = cnt ? fp_sum_f32(masked_s, cnt) / (float)cnt : 0.0f;
    sigma = (float)SIGMA_MULTIPLIER * (1.0f - avg_s);
    free(masked_s);

    /* 7. Son sigma ile n, d ve yon = ((phase(n, d) + pi) / 2) mod pi */
    compute_n_d_strengths(sigma, gx2, gy2, g2xy, mask, w, h, n, d, strengths);
    out = tmp;
    for (i = 0; i < len; i++) {
        float theta = fmodf((cv_phase(n[i], d[i]) + PI_F) / 2.0f, PI_F);
        out[i] = fp_encode_orientation(theta);
    }

    fp_write_stdout(PROG, out, len);

    free(img); free(mask); free(tmp); free(gx); free(gy); free(gx2); free(gy2);
    free(g2xy); free(n); free(d); free(strengths);
    return EXIT_SUCCESS;
}
