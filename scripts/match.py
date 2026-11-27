"""
Kullanim (ASM503 dizininden):
    python scripts/match.py                     # uc veri tabaninin hepsi
    python scripts/match.py FVC2002DB1          # yalnizca biri
    python scripts/match.py --fingers 10        # hizli deneme (ilk 10 parmak)
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from itertools import combinations

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = '.exe' if os.name == 'nt' else ''

DATABASES = {
    'FVC2000DB1': ('FVC2000', 'Db1_a'),
    'FVC2002DB1': ('FVC2002', 'Db1_a'),
    'FVC2004DB1': ('FVC2004', 'DB1_A'),
}
FINGERS = 100
IMPRESSIONS = 8

INVERT_TABLE = bytes(255 - i for i in range(256))


def tiff_size(path):
    """TIFF dosyasinin genislik ve yuksekligini dondurur."""
    with open(path, 'rb') as f:
        data = f.read()
    endian = '<' if data[:2] == b'II' else '>'
    ifd = struct.unpack(endian + 'I', data[4:8])[0]
    count = struct.unpack(endian + 'H', data[ifd:ifd + 2])[0]
    size = {}
    for i in range(count):
        tag, typ = struct.unpack(endian + 'HH', data[ifd + 2 + 12 * i: ifd + 6 + 12 * i])
        fmt = 'H' if typ == 3 else 'I'
        value = struct.unpack(endian + fmt, data[ifd + 10 + 12 * i: ifd + 10 + 12 * i + struct.calcsize(fmt)])[0]
        if tag in (256, 257):
            size[tag] = value
    return size[256], size[257]


def run(cmd, data=None):
    """Komutu calistirir, standart cikisini (bytes) dondurur."""
    r = subprocess.run(cmd, input=data, capture_output=True)
    if r.returncode != 0:
        raise RuntimeError(f'{os.path.basename(cmd[0])} hata verdi ({r.returncode}): '
                           f'{r.stderr.decode(errors="replace").strip()}')
    return r.stdout


def tool(name, args):
    return os.path.join(args.bin, name + EXE)


def process_image(tif, out_dir, w, h, args):
    """Bir goruntuyu iyilestirir ve minutiae dosyasini (.xyt) uretir."""
    name = os.path.splitext(os.path.basename(tif))[0]
    xyt = os.path.join(out_dir, name + '.xyt')
    if os.path.exists(xyt) and not args.force:
        return xyt
    size = ['-w', str(w), '-h', str(h)]
    with tempfile.TemporaryDirectory(prefix='match_') as tmp:
        mask, ori, frq = (os.path.join(tmp, n) for n in ('mask', 'orient', 'freq'))
        with open(tif, 'rb') as f:
            raw = run([tool('tiff2raw', args)], f.read())
        with open(mask, 'wb') as f:
            f.write(run([tool('segmentation', args)] + size, raw))
        with open(ori, 'wb') as f:
            f.write(run([tool('orientation', args)] + size + ['-s', mask], raw))
        with open(frq, 'wb') as f:
            f.write(run([tool('frequency', args)] + size + ['-s', mask, '-o', ori], raw))
        enhanced = run([tool('gabor_filter', args)] + size + ['-s', mask, '-o', ori, '-f', frq], raw)
        with open(os.path.join(out_dir, name + '_filtered.bmp'), 'wb') as f:
            f.write(run([tool('raw2bmp', args)] + size, enhanced))
        enhanced = enhanced.translate(INVERT_TABLE)
        an2k = os.path.join(tmp, name + '.an2k')
        with open(an2k, 'wb') as f:
            f.write(run([tool('raw2an2k', args)] + size, enhanced))
        run([os.path.join(args.nbis, 'mindtct' + EXE), an2k, os.path.join(tmp, name)])
        shutil.copyfile(os.path.join(tmp, name + '.xyt'), xyt + '.tmp')
        os.replace(xyt + '.tmp', xyt)     # yarida kesilirse eksik dosya kalmasin
    return xyt


def bozorth3(pairs, args, tmp_dir):
    """(sablon, sorgu) .xyt ciftleri icin bozorth3 skorlarini dondurur."""
    mates = os.path.join(tmp_dir, 'mates.lis')
    with open(mates, 'w') as f:
        for gallery, probe in pairs:
            f.write(f'{probe}\n{gallery}\n')
    out = run([os.path.join(args.nbis, 'bozorth3' + EXE), '-A', f'maxfiles={2 * len(pairs)}',
               '-A', 'outfmt=s', '-M', mates]).decode().split()
    if len(out) != len(pairs):
        raise RuntimeError(f'bozorth3 {len(pairs)} yerine {len(out)} skor dondurdu')
    return [int(s) for s in out]


def match_database(db, args):
    year_dir, set_dir = DATABASES[db]
    src = os.path.join(args.fvc, year_dir, 'Dbs', set_dir)
    if not os.path.isdir(src):
        sys.exit(f'Veri tabani bulunamadi: {src}')
    fingers = range(1, args.fingers + 1)
    images = {(f, i): os.path.join(src, f'{f}_{i}.tif') for f in fingers for i in range(1, IMPRESSIONS + 1)}
    w, h = tiff_size(images[(1, 1)])
    out_dir = os.path.join(args.work, db)
    os.makedirs(out_dir, exist_ok=True)

    # 1. Tum goruntuleri paralel isle
    print(f'{db}: {len(images)} goruntu ({w}x{h}) isleniyor, {args.jobs} is parcacigi...')
    t0, done, xyt = time.time(), 0, {}
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(process_image, path, out_dir, w, h, args): key for key, path in images.items()}
        for fut in as_completed(futures):
            xyt[futures[fut]] = fut.result()
            done += 1
            if done % 50 == 0 or done == len(images):
                print(f'  {done}/{len(images)} ({time.time() - t0:.0f} s)')

    # 2. FVC protokolune gore ciftler
    genuine = [((f, j), (f, k)) for f in fingers for j, k in combinations(range(1, IMPRESSIONS + 1), 2)]
    impostor = [((f, 1), (g, 1)) for f, g in combinations(fingers, 2)]

    # 3. bozorth3 skorlari ve dosyalara yazma
    os.makedirs(args.out, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='bozorth_') as tmp:
        for kind, pairs in (('genuine', genuine), ('impostor', impostor)):
            scores = bozorth3([(xyt[a], xyt[b]) for a, b in pairs], args, tmp)
            path = os.path.join(args.out, f'{db}_{kind}_scores.txt')
            with open(path, 'w') as f:
                for (a, b), s in zip(pairs, scores):
                    f.write(f'{a[0]}_{a[1]} {b[0]}_{b[1]} {s}\n')
            print(f'  {kind:8s}: {len(scores)} skor -> {os.path.relpath(path, ROOT)}')


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('databases', nargs='*', metavar='DB',
                   help='FVC2000DB1, FVC2002DB1, FVC2004DB1 (varsayilan: hepsi)')
    p.add_argument('--fvc', default=os.path.join(ROOT, '..', 'FVC'), help='FVC veri tabanlari dizini')
    p.add_argument('--nbis', default=os.path.join(ROOT, '..', 'nbis', 'bin'), help='NBIS bin dizini')
    p.add_argument('--bin', default=os.path.join(ROOT, 'bin'), help='proje araclarinin bin dizini')
    p.add_argument('--work', default=os.path.join(ROOT, '..', 'work'),
                   help='ara dosyalar (calisma dizini disinda olmali)')
    p.add_argument('--out', default=os.path.join(ROOT, 'scores'), help='skor dosyalarinin dizini')
    p.add_argument('--jobs', type=int, default=os.cpu_count() or 1, help='paralel is sayisi')
    p.add_argument('--fingers', type=int, default=FINGERS, help='kullanilacak parmak sayisi (deneme icin)')
    p.add_argument('--force', action='store_true', help='islenmis goruntuleri yeniden isle')
    args = p.parse_args()
    for d in ('fvc', 'nbis', 'bin', 'work', 'out'):
        setattr(args, d, os.path.normpath(getattr(args, d)))
    unknown = [db for db in args.databases if db not in DATABASES]
    if unknown:
        p.error(f'bilinmeyen veri tabani: {", ".join(unknown)} (secenekler: {", ".join(DATABASES)})')
    if not 1 <= args.fingers <= FINGERS:
        p.error(f'--fingers 1..{FINGERS} araliginda olmali')
    for db in args.databases or list(DATABASES):
        match_database(db, args)


if __name__ == '__main__':
    main()
