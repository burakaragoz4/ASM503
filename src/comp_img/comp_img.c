/*
 * comp_img - ayni boyuttaki iki 8-bit BMP goruntusunu karsilastirir.
 *
 * Kullanim:  comp_img <goruntu1.bmp> <goruntu2.bmp>
 *
 * Cikti (standart cikis):
 *   Total difference: <toplam |g1(i,j) - g2(i,j)|>
 *   Maximum difference: <en buyuk |g1(i,j) - g2(i,j)|>
 *
 * Desteklenen BMP: 8 bit/piksel, sikistirmasiz (BI_RGB); alttan uste
 * (pozitif yukseklik) veya ustten alta (negatif yukseklik) satir duzeni.
 * Karsilastirma piksellerin 8-bit degerleri uzerinden yapilir.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DIM 100000L

typedef struct {
    long width;
    long height;
    unsigned char *pixels;   /* ustten alta, satir basina width byte */
} Image;

static unsigned long get16(const unsigned char *p)
{
    return (unsigned long)p[0] | (unsigned long)p[1] << 8;
}

static unsigned long get32(const unsigned char *p)
{
    return (unsigned long)p[0] | (unsigned long)p[1] << 8 |
           (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

static void fail(const char *path, const char *msg)
{
    fprintf(stderr, "comp_img: %s: %s\n", path, msg);
    exit(EXIT_FAILURE);
}

static void read_bmp(const char *path, Image *img)
{
    FILE *fp;
    unsigned char hdr[54];
    unsigned long off_bits, info_size, compression, bpp, stride;
    long raw_height, y, row;
    int top_down;
    unsigned char *line;

    fp = fopen(path, "rb");
    if (!fp) fail(path, "dosya acilamadi");

    /* BITMAPFILEHEADER (14) + BITMAPINFOHEADER'in ilk 40 byte'i */
    if (fread(hdr, 1, sizeof hdr, fp) != sizeof hdr)
        fail(path, "BMP basligi okunamadi (dosya cok kisa)");
    if (hdr[0] != 'B' || hdr[1] != 'M')
        fail(path, "BMP dosyasi degil (BM imzasi yok)");

    off_bits    = get32(hdr + 10);
    info_size   = get32(hdr + 14);
    img->width  = (long)get32(hdr + 18);
    raw_height  = (long)(int)get32(hdr + 22);   /* isaretli */
    bpp         = get16(hdr + 28);
    compression = get32(hdr + 30);

    if (info_size < 40) fail(path, "desteklenmeyen BMP basligi (eski format)");
    if (bpp != 8) fail(path, "yalnizca 8 bit/piksel BMP destekleniyor");
    if (compression != 0) fail(path, "sikistirilmis BMP desteklenmiyor");

    top_down = raw_height < 0;
    img->height = top_down ? -raw_height : raw_height;
    if (img->width <= 0 || img->height <= 0 ||
        img->width > MAX_DIM || img->height > MAX_DIM)
        fail(path, "gecersiz goruntu boyutlari");

    stride = ((unsigned long)img->width + 3) & ~3UL;
    img->pixels = malloc((size_t)img->width * (size_t)img->height);
    line = malloc(stride);
    if (!img->pixels || !line) fail(path, "bellek yetersiz");

    if (fseek(fp, (long)off_bits, SEEK_SET) != 0)
        fail(path, "piksel verisine gidilemedi");

    /* Dosyadaki her satiri oku, padding'i atla, ustten alta yerlestir. */
    for (y = 0; y < img->height; y++) {
        if (fread(line, 1, stride, fp) != stride)
            fail(path, "piksel verisi eksik");
        row = top_down ? y : img->height - 1 - y;
        memcpy(img->pixels + (size_t)row * (size_t)img->width, line,
               (size_t)img->width);
    }

    free(line);
    fclose(fp);
}

int main(int argc, char *argv[])
{
    Image a, b;
    unsigned long long total = 0;
    unsigned max = 0, d;
    size_t i, n;

    if (argc != 3) {
        fprintf(stderr, "Kullanim: comp_img <goruntu1.bmp> <goruntu2.bmp>\n");
        return EXIT_FAILURE;
    }

    read_bmp(argv[1], &a);
    read_bmp(argv[2], &b);

    if (a.width != b.width || a.height != b.height) {
        fprintf(stderr, "comp_img: goruntu boyutlari farkli: %ldx%ld ve %ldx%ld\n",
                a.width, a.height, b.width, b.height);
        return EXIT_FAILURE;
    }

    n = (size_t)a.width * (size_t)a.height;
    for (i = 0; i < n; i++) {
        d = a.pixels[i] > b.pixels[i] ? (unsigned)(a.pixels[i] - b.pixels[i])
                                      : (unsigned)(b.pixels[i] - a.pixels[i]);
        total += d;
        if (d > max) max = d;
    }

    printf("Total difference: %llu\n", total);
    printf("Maximum difference: %u\n", max);

    free(a.pixels);
    free(b.pixels);
    return EXIT_SUCCESS;
}
