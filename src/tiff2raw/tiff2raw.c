/*
 * tiff2raw - TIFF goruntusunu raw (basliksiz) 8-bit gri goruntuye cevirir.
 *
 * Kullanim:  tiff2raw < girdi.tif > cikti.raw
 *
 * Girdi : standart giristen okunan TIFF dosyasi (FVC veri tabanlari formati:
 *         8-bit, tek kanal, sikistirmasiz).
 * Cikti : standart cikisa yazilan raw piksel verisi. Satirlar yukaridan
 *         asagiya, her satirda pikseller soldan saga, piksel basina 1 byte
 *         (0 = siyah, 255 = beyaz). Toplam genislik*yukseklik byte.
 *
 * Desteklenenler: little/big endian (II/MM), tek veya cok strip,
 *                 BlackIsZero / WhiteIsZero.
 * Desteklenmeyenler (hata verir): sikistirma, 8 disi bit derinligi,
 *                 cok kanalli (renkli) goruntuler, tile tabanli TIFF.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

/* TIFF etiket numaralari */
#define TAG_IMAGE_WIDTH        256
#define TAG_IMAGE_LENGTH       257
#define TAG_BITS_PER_SAMPLE    258
#define TAG_COMPRESSION        259
#define TAG_PHOTOMETRIC        262
#define TAG_STRIP_OFFSETS      273
#define TAG_SAMPLES_PER_PIXEL  277
#define TAG_STRIP_BYTE_COUNTS  279
#define TAG_TILE_WIDTH         322

/* TIFF alan tipleri */
#define TYPE_BYTE   1
#define TYPE_SHORT  3
#define TYPE_LONG   4

typedef struct {
    const unsigned char *data;
    size_t size;
    int big_endian;
} Tiff;

static void die(const char *msg)
{
    fprintf(stderr, "tiff2raw: %s\n", msg);
    exit(EXIT_FAILURE);
}

/* Tum standart girisi bellege okur. (Pipe'larda fseek yapilamadigi icin
 * TIFF'teki adreslere atlayabilmek adina dosyanin tamami bellekte olmali.) */
static unsigned char *read_all(FILE *fp, size_t *out_size)
{
    size_t cap = 1 << 16, len = 0, n;
    unsigned char *buf = malloc(cap);
    if (!buf) die("bellek yetersiz");
    while ((n = fread(buf + len, 1, cap - len, fp)) > 0) {
        len += n;
        if (len == cap) {
            unsigned char *tmp;
            cap *= 2;
            tmp = realloc(buf, cap);
            if (!tmp) { free(buf); die("bellek yetersiz"); }
            buf = tmp;
        }
    }
    if (ferror(fp)) { free(buf); die("standart giris okunamadi"); }
    *out_size = len;
    return buf;
}

static int in_range(const Tiff *t, size_t off, size_t len)
{
    return off <= t->size && len <= t->size - off;
}

static unsigned rd16(const Tiff *t, size_t off)
{
    const unsigned char *p = t->data + off;
    if (!in_range(t, off, 2)) die("bozuk TIFF: dosya beklenenden kisa");
    return t->big_endian ? (unsigned)(p[0] << 8 | p[1])
                         : (unsigned)(p[1] << 8 | p[0]);
}

static unsigned long rd32(const Tiff *t, size_t off)
{
    const unsigned char *p = t->data + off;
    if (!in_range(t, off, 4)) die("bozuk TIFF: dosya beklenenden kisa");
    return t->big_endian
        ? ((unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 |
           (unsigned long)p[2] << 8 | p[3])
        : ((unsigned long)p[3] << 24 | (unsigned long)p[2] << 16 |
           (unsigned long)p[1] << 8 | p[0]);
}

static size_t type_size(unsigned type)
{
    switch (type) {
    case TYPE_BYTE:  return 1;
    case TYPE_SHORT: return 2;
    case TYPE_LONG:  return 4;
    default:         return 0;
    }
}

/* Bir IFD girdisinin (entry) i. degerini okur. Degerler toplam 4 byte'a
 * sigiyorsa girdinin icinde, sigmiyorsa girdide yazan adreste durur. */
static unsigned long entry_value(const Tiff *t, size_t entry, unsigned long i)
{
    unsigned type = rd16(t, entry + 2);
    unsigned long count = rd32(t, entry + 4);
    size_t sz = type_size(type), base;

    if (sz == 0) die("bozuk TIFF: desteklenmeyen etiket tipi");
    if (i >= count) die("bozuk TIFF: etiket deger sayisi yetersiz");
    base = (count * sz <= 4) ? entry + 8 : rd32(t, entry + 8);
    base += i * sz;
    switch (type) {
    case TYPE_BYTE:
        if (!in_range(t, base, 1)) die("bozuk TIFF: dosya beklenenden kisa");
        return t->data[base];
    case TYPE_SHORT: return rd16(t, base);
    default:         return rd32(t, base);
    }
}

int main(void)
{
    Tiff t;
    unsigned char *file;
    size_t ifd, strip_entry = 0, count_entry = 0;
    unsigned n_entries, i;
    unsigned long width = 0, height = 0, bps = 1, compression = 1;
    unsigned long photometric = 1, spp = 1;
    unsigned long n_strips = 0, s, total, written = 0;
    unsigned char *pixels;

#ifdef _WIN32
    /* Windows'ta metin modu 0x0A <-> 0x0D0A donusumu yapip ikili veriyi
     * bozar; stdin/stdout ikili moda alinir. */
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    file = read_all(stdin, &t.size);
    t.data = file;

    /* --- Baslik: "II"/"MM", 42, ilk IFD adresi --- */
    if (t.size < 8) die("girdi TIFF degil (cok kisa)");
    if (file[0] == 'I' && file[1] == 'I')      t.big_endian = 0;
    else if (file[0] == 'M' && file[1] == 'M') t.big_endian = 1;
    else die("girdi TIFF degil (II/MM imzasi yok)");
    if (rd16(&t, 2) != 42) die("girdi TIFF degil (42 imzasi yok)");
    ifd = rd32(&t, 4);

    /* --- IFD: etiketleri tek tek oku --- */
    n_entries = rd16(&t, ifd);
    for (i = 0; i < n_entries; i++) {
        size_t e = ifd + 2 + (size_t)i * 12;
        switch (rd16(&t, e)) {
        case TAG_IMAGE_WIDTH:       width = entry_value(&t, e, 0); break;
        case TAG_IMAGE_LENGTH:      height = entry_value(&t, e, 0); break;
        case TAG_BITS_PER_SAMPLE:   bps = entry_value(&t, e, 0); break;
        case TAG_COMPRESSION:       compression = entry_value(&t, e, 0); break;
        case TAG_PHOTOMETRIC:       photometric = entry_value(&t, e, 0); break;
        case TAG_SAMPLES_PER_PIXEL: spp = entry_value(&t, e, 0); break;
        case TAG_STRIP_OFFSETS:
            strip_entry = e;
            n_strips = rd32(&t, e + 4);
            break;
        case TAG_STRIP_BYTE_COUNTS: count_entry = e; break;
        case TAG_TILE_WIDTH: die("tile tabanli TIFF desteklenmiyor");
        }
    }

    /* --- Desteklenen format mi? --- */
    if (width == 0 || height == 0) die("goruntu boyutlari bulunamadi");
    if (compression != 1) die("sikistirilmis TIFF desteklenmiyor");
    if (bps != 8) die("yalnizca 8-bit gri TIFF destekleniyor");
    if (spp != 1) die("yalnizca tek kanalli (gri) TIFF destekleniyor");
    if (photometric != 0 && photometric != 1)
        die("yalnizca gri (BlackIsZero/WhiteIsZero) TIFF destekleniyor");
    if (strip_entry == 0 || n_strips == 0) die("strip adresleri bulunamadi");
    if (width > 100000UL || height > 100000UL) die("goruntu boyutlari gecersiz");

    /* --- Strip'leri sirayla birlestir --- */
    total = width * height;
    pixels = malloc(total);
    if (!pixels) die("bellek yetersiz");

    for (s = 0; s < n_strips && written < total; s++) {
        unsigned long off = entry_value(&t, strip_entry, s);
        unsigned long len;
        if (count_entry) {
            len = entry_value(&t, count_entry, s);
        } else if (n_strips == 1) {
            len = total;  /* StripByteCounts yoksa tek strip = tum goruntu */
        } else {
            die("StripByteCounts etiketi eksik");
        }
        if (len > total - written) len = total - written;
        if (!in_range(&t, off, len)) die("bozuk TIFF: strip dosya disinda");
        memcpy(pixels + written, file + off, len);
        written += len;
    }
    if (written != total) die("bozuk TIFF: piksel verisi eksik");

    /* WhiteIsZero ise (0 = beyaz) degerleri ters cevir */
    if (photometric == 0)
        for (s = 0; s < total; s++) pixels[s] = (unsigned char)(255 - pixels[s]);

    if (fwrite(pixels, 1, total, stdout) != total || fflush(stdout) != 0)
        die("standart cikisa yazilamadi");

    free(pixels);
    free(file);
    return EXIT_SUCCESS;
}
