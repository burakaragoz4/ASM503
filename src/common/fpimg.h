/*
 * fpimg - parmak izi araclarinin (segmentation, orientation, frequency,
 * gabor_filter) ortak goruntu isleme fonksiyonlari.
 *
 * Fonksiyonlar, pyfing'in kullandigi OpenCV / numpy fonksiyonlarinin
 * davranisini (kenar islemleri, yuvarlama, ara veri tipleri) taklit eder.
 * Tum goruntuler satir oncelikli (row-major), ustten alta saklanir.
 */
#ifndef FPIMG_H
#define FPIMG_H

#include <stddef.h>

/* ---- Komut satiri ---------------------------------------------------- */

typedef struct {
    int width, height;
    const char *mask_path;         /* -s / --segmentation */
    const char *orientation_path;  /* -o / --orientation  */
    const char *frequency_path;    /* -f / --frequency    */
} Args;

/* Bayraklar: hangi ek dosya argumanlari zorunlu */
#define NEED_MASK         1
#define NEED_ORIENTATION  2
#define NEED_FREQUENCY    4

void fp_parse_args(int argc, char *argv[], const char *prog, int needs, Args *a);
void fp_die(const char *prog, const char *msg);

/* ---- Raw goruntu G/C -------------------------------------------------- */

void fp_binary_stdio(void);
unsigned char *fp_read_stdin(const char *prog, size_t n);
unsigned char *fp_read_file(const char *prog, const char *path, size_t n);
void fp_write_stdout(const char *prog, const unsigned char *buf, size_t n);
void *fp_alloc(const char *prog, size_t n);

/* ---- Kenar indeksleri (cv::borderInterpolate) ------------------------- */

int fp_reflect101(int p, int len);   /* BORDER_REFLECT_101 (gfedcb|abcdefgh|gfedcba) */
int fp_reflect(int p, int len);      /* BORDER_REFLECT     (fedcba|abcdefgh|hgfedcb) */
int fp_replicate(int p, int len);    /* BORDER_REPLICATE   (aaaaaa|abcdefgh|hhhhhhh) */

/* ---- Filtreler -------------------------------------------------------- */

/* cv::getGaussianKernel: ksize tek; sigma <= 0 ise ksize'dan hesaplanir. */
void fp_gaussian_kernel(int ksize, double sigma, double *k);

/* cv::GaussianBlur (float goruntu, BORDER_REFLECT_101) */
void fp_gaussian_blur_f32(const float *src, float *dst, int w, int h,
                          int ksize, double sigma);

/* cv::GaussianBlur (8-bit goruntu): OpenCV'nin bit-exact sabit noktali
 * (8 kesir bitli cekirdek) uygulamasi, BORDER_REFLECT_101. */
void fp_gaussian_blur_u8(const unsigned char *src, unsigned char *dst,
                         int w, int h, int ksize, double sigma);

/* cv::medianBlur (8-bit, BORDER_REPLICATE) */
void fp_median_blur_u8(const unsigned char *src, unsigned char *dst,
                       int w, int h, int ksize);

/* 3x3 Sobel turevleri (cv::Sobel / cv::spatialGradient, BORDER_REFLECT_101) */
void fp_sobel3(const unsigned char *src, float *gx, float *gy, int w, int h);

/* ---- Istatistik ------------------------------------------------------- */

/* numpy.percentile (method='linear'). Dizi siralanir (yerinde). */
double fp_percentile_f64(double *v, size_t n, double q);

/* numpy float32 toplami (pairwise summation, ayni islem sirasi) */
float fp_sum_f32(const float *v, size_t n);

/* ---- Araclar arasi veri kodlamasi -------------------------------------- */
/*
 * Yon ve sirt periyodu float degerlerdir; araclar arasinda 8-bit raw
 * goruntu olarak aktarilirlar. Kodlama pyfing'in SNFFE/SNFEN modellerinin
 * girdisinde kullandigi kodlamanin aynisidir:
 *   yon      (radyan, [0, pi))  -> round(yon * 255 / pi)
 *   periyot  (piksel)           -> round(periyot * 10)
 */
unsigned char fp_encode_orientation(float theta);
float fp_decode_orientation(unsigned char v);
unsigned char fp_encode_period(float period);
float fp_decode_period(unsigned char v);

/* ---- Morfoloji ve bagli bilesenler (0/255 maskeler) -------------------- */

#define FP_BORDER_NEUTRAL  (-1)   /* OpenCV varsayilani: kenar etkisiz */

/* 3x3 arti (+) yapi elemaniyla genisletme/asindirma, 'iterations' kez.
 * border_value: goruntu disindaki piksel degeri (0..255) veya
 * FP_BORDER_NEUTRAL. */
void fp_dilate_cross(unsigned char *img, int w, int h, int iterations, int border_value);
void fp_erode_cross(unsigned char *img, int w, int h, int iterations, int border_value);

typedef struct {
    int left, top, width, height;
    long area;
} CCStats;

/* cv::connectedComponentsWithStats (8 komsuluk). Sifir olmayan pikseller
 * 1..n-1 etiketlerini, sifir pikseller 0 etiketini alir. Etiket sayisini
 * (arka plan dahil) dondurur; labels ve *stats cagiran tarafindan free edilir. */
int fp_connected_components(const unsigned char *img, int w, int h,
                            int *labels, CCStats **stats);

/* Yalnizca en buyuk bagli bileseni birakir (pyfing _remove_other_cc). */
void fp_keep_largest_component(unsigned char *mask, int w, int h);

#endif
