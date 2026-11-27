"""
Kullanim:
    python scripts/perturb_bmp.py <girdi.bmp> <cikti.bmp> [piksel_sayisi]
"""
import random
import struct
import sys

def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    count = int(sys.argv[3]) if len(sys.argv) == 4 else 5

    data = bytearray(open(src, 'rb').read())
    if data[:2] != b'BM' or struct.unpack('<H', data[28:30])[0] != 8:
        sys.exit('Girdi 8-bit BMP degil')
    offset = struct.unpack('<I', data[10:14])[0]
    width, height = struct.unpack('<ii', data[18:26])
    height = abs(height)
    stride = (width + 3) & ~3

    positions = [offset + y * stride + x for y in range(height) for x in range(width)]
    rng = random.Random(503) 
    for pos in rng.sample(positions, count):
        if data[pos] == 255:
            data[pos] -= 1
        elif data[pos] == 0:
            data[pos] += 1
        else:
            data[pos] += rng.choice((-1, 1))

    open(dst, 'wb').write(data)
    print(f'{dst}: {count} piksel 1 degistirildi ({width}x{height})')


if __name__ == '__main__':
    main()
