/*
 * raw2an2k - raw (basliksiz) 8-bit gri parmak izi goruntusunu
 *            ANSI/NIST-ITL 1-2007 dosyasina cevirir.
 *
 * Kullanim:  raw2an2k -w <genislik> -h <yukseklik> < girdi.raw > cikti.an2k
 *            raw2an2k --width <genislik> --height <yukseklik> ...
 *
 * Girdi : standart giristen genislik*yukseklik byte raw piksel verisi
 *         (satirlar yukaridan asagiya, piksel basina 1 byte, 0 = siyah).
 * Cikti : standart cikisa uc kayittan olusan ANSI/NIST-ITL 1-2007 dosyasi:
 *           Type-1  : islem (transaction) bilgisi
 *           Type-2  : kullanici tanimli aciklama kaydi (yalnizca IDC)
 *           Type-14 : degisken cozunurluklu parmak izi goruntusu,
 *                     sikistirmasiz (CGA = NONE), 8 bit gri
 *         Genislik ve yukseklik disindaki tum degerler (cozunurluk, tarih,
 *         kurum kodlari vb.) asagidaki sabitlerden alinir.
 *
 * Kayit bicimi: her alan "<kayit>.<alan>:<deger>" seklindedir; alanlar GS
 * (0x1D), kayitlar FS (0x1C) ile biter. Her kaydin ilk alani LEN (kaydin
 * byte cinsinden toplam uzunlugu), ikincisi IDC'dir. Type-14'un son alani
 * 14.999 ikili goruntu verisidir.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

/* --- Sabit degerler --- */
#define AN2K_VERSION      "0400"      /* ANSI/NIST-ITL 1-2007 */
#define TRANSACTION_TYPE  "FVC"       /* 1.004 TOT */
#define DATE              "20020101"  /* 1.005 DAT / 14.005 FCD (YYYYMMDD) */
#define DEST_AGENCY       "ASM503"    /* 1.007 DAI */
#define ORIG_AGENCY       "ASM503"    /* 1.008 ORI ve 14.004 SRC */
#define CONTROL_NUMBER    "1"         /* 1.009 TCN */
#define PPI               500         /* tarama cozunurlugu (piksel/inc) */
#define PPMM              "19.69"     /* 500 ppi = 19.69 piksel/mm (1.011, 1.012) */
#define IMP_LIVE_PLAIN    "0"         /* 14.003 IMP: canli tarama, duz basma */
#define FGP_UNKNOWN       "0"         /* 14.013 FGP: parmak bilinmiyor */
#define IDC_TYPE2         "00"
#define IDC_TYPE14        "01"

#define MAX_DIM           100000L
#define MAX_FIELDS        16

/* ANSI/NIST ayraclari */
#define FS  0x1C   /* kayit sonu */
#define GS  0x1D   /* alan ayraci */
#define RS  0x1E   /* alt alan ayraci */
#define US  0x1F   /* oge ayraci */

typedef struct {
    int number;                 /* alan numarasi (orn. 999) */
    const unsigned char *value; /* alan degeri */
    unsigned long length;       /* deger uzunlugu (byte) */
} Field;

typedef struct {
    int type;
    Field fields[MAX_FIELDS];   /* LEN (x.001) haric, artan numara sirasinda */
    int count;
} Record;

static void usage(void)
{
    fprintf(stderr,
        "Kullanim: raw2an2k -w <genislik> -h <yukseklik> < girdi.raw > cikti.an2k\n"
        "          raw2an2k --width <genislik> --height <yukseklik> ...\n");
}

static void die(const char *msg)
{
    fprintf(stderr, "raw2an2k: %s\n", msg);
    exit(EXIT_FAILURE);
}

static long parse_dim(const char *s, const char *name)
{
    char *end;
    long v;
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno || end == s || *end != '\0' || v <= 0 || v > MAX_DIM) {
        fprintf(stderr, "raw2an2k: gecersiz %s degeri: '%s'\n", name, s);
        exit(EXIT_FAILURE);
    }
    return v;
}

/* Kayda metin degerli alan ekler (deger bellekte kalici olmali). */
static void add_text(Record *r, int number, const char *value)
{
    if (r->count >= MAX_FIELDS) die("ic hata: alan sayisi asildi");
    r->fields[r->count].number = number;
    r->fields[r->count].value = (const unsigned char *)value;
    r->fields[r->count].length = (unsigned long)strlen(value);
    r->count++;
}

/* Kayda ikili degerli alan ekler (Type-14 icin 999 goruntu alani). */
static void add_binary(Record *r, int number, const unsigned char *data,
                       unsigned long length)
{
    if (r->count >= MAX_FIELDS) die("ic hata: alan sayisi asildi");
    r->fields[r->count].number = number;
    r->fields[r->count].value = data;
    r->fields[r->count].length = length;
    r->count++;
}

/* "<tip>.<alan>:" etiketinin uzunlugu, orn. "14.006:" -> 7 */
static unsigned long tag_length(int type, int number)
{
    char tag[32];
    return (unsigned long)sprintf(tag, "%d.%03d:", type, number);
}

static unsigned long digits(unsigned long v)
{
    unsigned long n = 1;
    while (v >= 10) { v /= 10; n++; }
    return n;
}

/*
 * Kaydin LEN degerini hesaplar. LEN, kendi rakamlari dahil kaydin toplam
 * uzunlugudur; rakam sayisi toplami degistirebilecegi icin sabit noktaya
 * ulasana kadar tekrarlanir.
 */
static unsigned long record_length(const Record *r)
{
    unsigned long rest = tag_length(r->type, 1) + 1;  /* "x.001:" + ayrac */
    unsigned long len;
    int i;
    for (i = 0; i < r->count; i++)
        rest += tag_length(r->type, r->fields[i].number)
              + r->fields[i].length + 1;              /* deger + GS/FS */
    len = rest + 1;
    while (rest + digits(len) != len)
        len = rest + digits(len);
    return len;
}

static void put_bytes(const void *p, unsigned long n)
{
    if (n && fwrite(p, 1, n, stdout) != n) die("standart cikisa yazilamadi");
}

static void write_record(const Record *r)
{
    char buf[64];
    int i, n;

    n = sprintf(buf, "%d.%03d:%lu", r->type, 1, record_length(r));
    put_bytes(buf, (unsigned long)n);
    for (i = 0; i < r->count; i++) {
        buf[0] = GS;
        put_bytes(buf, 1);
        n = sprintf(buf, "%d.%03d:", r->type, r->fields[i].number);
        put_bytes(buf, (unsigned long)n);
        put_bytes(r->fields[i].value, r->fields[i].length);
    }
    buf[0] = FS;
    put_bytes(buf, 1);
}

int main(int argc, char *argv[])
{
    long width = 0, height = 0;
    unsigned long total;
    unsigned char *pixels;
    char hll[16], vll[16], ppi[16], cnt[64];
    Record t1, t2, t14;
    int i;

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
            fprintf(stderr, "raw2an2k: bilinmeyen arguman: '%s'\n", argv[i]);
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
    if (fread(pixels, 1, total, stdin) != total)
        die("girdi beklenenden kisa (genislik*yukseklik byte okunamadi)");
    if (fgetc(stdin) != EOF)
        die("girdi beklenenden uzun (-w/-h degerlerini kontrol edin)");

    sprintf(hll, "%ld", width);
    sprintf(vll, "%ld", height);
    sprintf(ppi, "%d", PPI);
    /* 1.003 CNT: ilk alt alan "1<US><diger kayit sayisi>", sonra her kayit
     * icin "<tip><US><IDC>"; alt alanlar RS ile ayrilir. */
    sprintf(cnt, "1%c2%c2%c%s%c14%c%s",
            US, RS, US, IDC_TYPE2, RS, US, IDC_TYPE14);

    /* --- Type-1: islem bilgisi --- */
    memset(&t1, 0, sizeof t1);
    t1.type = 1;
    add_text(&t1, 2, AN2K_VERSION);      /* VER */
    add_text(&t1, 3, cnt);               /* CNT */
    add_text(&t1, 4, TRANSACTION_TYPE);  /* TOT */
    add_text(&t1, 5, DATE);              /* DAT */
    add_text(&t1, 7, DEST_AGENCY);       /* DAI */
    add_text(&t1, 8, ORIG_AGENCY);       /* ORI */
    add_text(&t1, 9, CONTROL_NUMBER);    /* TCN */
    add_text(&t1, 11, PPMM);             /* NSR: tarama cozunurlugu */
    add_text(&t1, 12, PPMM);             /* NTR: iletim cozunurlugu */

    /* --- Type-2: aciklama kaydi --- */
    memset(&t2, 0, sizeof t2);
    t2.type = 2;
    add_text(&t2, 2, IDC_TYPE2);         /* IDC */

    /* --- Type-14: parmak izi goruntusu --- */
    memset(&t14, 0, sizeof t14);
    t14.type = 14;
    add_text(&t14, 2, IDC_TYPE14);       /* IDC */
    add_text(&t14, 3, IMP_LIVE_PLAIN);   /* IMP: baski tipi */
    add_text(&t14, 4, ORIG_AGENCY);      /* SRC: kaynak kurum */
    add_text(&t14, 5, DATE);             /* FCD: alim tarihi */
    add_text(&t14, 6, hll);              /* HLL: genislik */
    add_text(&t14, 7, vll);              /* VLL: yukseklik */
    add_text(&t14, 8, "1");              /* SLC: 1 = piksel/inc */
    add_text(&t14, 9, ppi);              /* HPS: yatay cozunurluk */
    add_text(&t14, 10, ppi);             /* VPS: dikey cozunurluk */
    add_text(&t14, 11, "NONE");          /* CGA: sikistirma yok */
    add_text(&t14, 12, "8");             /* BPX: bit/piksel */
    add_text(&t14, 13, FGP_UNKNOWN);     /* FGP: parmak pozisyonu */
    add_binary(&t14, 999, pixels, total);/* DAT: goruntu verisi */

    write_record(&t1);
    write_record(&t2);
    write_record(&t14);
    if (fflush(stdout) != 0) die("standart cikisa yazilamadi");

    free(pixels);
    return EXIT_SUCCESS;
}
