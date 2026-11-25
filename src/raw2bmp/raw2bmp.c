/*
 * raw2bmp - raw (basliksiz) 8-bit gri goruntuyu BMP dosyasina cevirir.
 *
 * Kullanim:  raw2bmp -w <genislik> -h <yukseklik> < girdi.raw > cikti.bmp
 *            raw2bmp --width <genislik> --height <yukseklik> ...
 *
 * Girdi : standart giristen genislik*yukseklik byte raw piksel verisi
 *         (satirlar yukaridan asagiya, piksel basina 1 byte, 0 = siyah).
 * Cikti : standart cikisa 8-bit, 256 gri tonlu paletli, sikistirmasiz BMP.
 *
 * BMP yapisi:
 *   BITMAPFILEHEADER (14 byte) + BITMAPINFOHEADER (40 byte)
 *   + palet (256 x 4 byte) + piksel verisi.
 *   Piksel satirlari alttan uste saklanir ve her satir 4'un katina
 *   tamamlanir (padding).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#define FILE_HEADER_SIZE  14
#define INFO_HEADER_SIZE  40
#define PALETTE_SIZE      (256 * 4)
#define PIXEL_OFFSET      (FILE_HEADER_SIZE + INFO_HEADER_SIZE + PALETTE_SIZE)
#define MAX_DIM           100000L

static void usage(void)
{
    fprintf(stderr,
        "Kullanim: raw2bmp -w <genislik> -h <yukseklik> < girdi.raw > cikti.bmp\n"
        "          raw2bmp --width <genislik> --height <yukseklik> ...\n");
}

static void die(const char *msg)
{
    fprintf(stderr, "raw2bmp: %s\n", msg);
    exit(EXIT_FAILURE);
}

static long parse_dim(const char *s, const char *name)
{
    char *end;
    long v;
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno || end == s || *end != '\0' || v <= 0 || v > MAX_DIM) {
        fprintf(stderr, "raw2bmp: gecersiz %s degeri: '%s'\n", name, s);
        exit(EXIT_FAILURE);
    }
    return v;
}

/* Little-endian yazma yardimcilari (BMP alanlari little-endian'dir) */
static void put16(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void put32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

int main(int argc, char *argv[])
{
    long width = 0, height = 0;
    unsigned long stride, image_size, file_size, total;
    unsigned char header[PIXEL_OFFSET];
    unsigned char *pixels, *row;
    int i, extra;
    long y;

    /* --- Argumanlar --- */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--width") == 0) {
            if (++i >= argc) { usage(); die("genislik degeri eksik"); }
            width = parse_dim(argv[i], "genislik");
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--height") == 0) {
            if (++i >= argc) { usage(); die("yukseklik degeri eksik"); }
            height = parse_dim(argv[i], "yukseklik");
        } else {
            usage();
            fprintf(stderr, "raw2bmp: bilinmeyen arguman: '%s'\n", argv[i]);
            return EXIT_FAILURE;
        }
    }
    if (width == 0 || height == 0) { usage(); die("-w ve -h zorunludur"); }

#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    /* --- Raw veriyi oku --- */
    total = (unsigned long)width * (unsigned long)height;
    pixels = malloc(total);
    if (!pixels) die("bellek yetersiz");
    if (fread(pixels, 1, total, stdin) != total) {
        free(pixels);
        die("girdi beklenenden kisa (genislik*yukseklik byte okunamadi)");
    }
    extra = fgetc(stdin);
    if (extra != EOF) {
        free(pixels);
        die("girdi beklenenden uzun (-w/-h degerlerini kontrol edin)");
    }

    /* --- Basliklar --- */
    stride = ((unsigned long)width + 3) & ~3UL;   /* 4'un katina yuvarla */
    image_size = stride * (unsigned long)height;
    file_size = PIXEL_OFFSET + image_size;

    memset(header, 0, sizeof header);
    /* BITMAPFILEHEADER */
    header[0] = 'B';
    header[1] = 'M';
    put32(header + 2, file_size);            /* dosya boyu */
    put32(header + 10, PIXEL_OFFSET);        /* piksellerin baslangici */
    /* BITMAPINFOHEADER */
    put32(header + 14, INFO_HEADER_SIZE);    /* baslik boyu */
    put32(header + 18, (unsigned long)width);
    put32(header + 22, (unsigned long)height); /* pozitif: alttan uste */
    put16(header + 26, 1);                   /* renk duzlemi */
    put16(header + 28, 8);                   /* bit/piksel */
    put32(header + 30, 0);                   /* BI_RGB: sikistirma yok */
    put32(header + 34, image_size);
    put32(header + 38, 2835);                /* ~72 DPI (piksel/metre) */
    put32(header + 42, 2835);
    put32(header + 46, 256);                 /* kullanilan renk sayisi */
    put32(header + 50, 256);                 /* onemli renk sayisi */
    /* Palet: i. renk (B,G,R,0) = (i,i,i,0) -> gri tonlar */
    for (i = 0; i < 256; i++) {
        unsigned char *p = header + FILE_HEADER_SIZE + INFO_HEADER_SIZE + i * 4;
        p[0] = p[1] = p[2] = (unsigned char)i;
        p[3] = 0;
    }
    if (fwrite(header, 1, sizeof header, stdout) != sizeof header)
        die("standart cikisa yazilamadi");

    /* --- Pikseller: alttan uste, padding ile --- */
    row = calloc(stride, 1);
    if (!row) { free(pixels); die("bellek yetersiz"); }
    for (y = height - 1; y >= 0; y--) {
        memcpy(row, pixels + (unsigned long)y * (unsigned long)width, (size_t)width);
        if (fwrite(row, 1, stride, stdout) != stride)
            die("standart cikisa yazilamadi");
    }
    if (fflush(stdout) != 0) die("standart cikisa yazilamadi");

    free(row);
    free(pixels);
    return EXIT_SUCCESS;
}
