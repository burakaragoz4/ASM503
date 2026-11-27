"""
Kullanim (ASM503 dizininden):
    python scripts/det.py                  # uc veri tabaninin hepsi
    python scripts/det.py FVC2002DB1       # yalnizca biri
Girdi : scores/<DB>_genuine_scores.txt, scores/<DB>_impostor_scores.txt
        (her satirin son sutunu skor)
Cikti : images/<DB>_det.png
"""
import argparse
import os
import sys
import matplotlib
matplotlib.use('Agg')                  
import matplotlib.pyplot as plt        
import numpy as np                     
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATABASES = ('FVC2000DB1', 'FVC2002DB1', 'FVC2004DB1')

def read_scores(path):
    if not os.path.exists(path):
        sys.exit(f'Skor dosyasi bulunamadi: {path} (once match.py calistirilmali)')
    with open(path) as f:
        scores = [float(line.split()[-1]) for line in f if line.strip()]
    if not scores:
        sys.exit(f'Skor dosyasi bos: {path}')
    return np.array(scores)

def far_frr(genuine, impostor):
    thresholds = np.unique(np.concatenate([genuine, impostor, [genuine.max() + 1, impostor.max() + 1]]))
    g, i = np.sort(genuine), np.sort(impostor)
    frr = np.searchsorted(g, thresholds, side='left') / g.size          
    far = 1 - np.searchsorted(i, thresholds, side='left') / i.size       
    return thresholds, far, frr

def fvc_eer(thresholds, far, frr):
    i1 = np.nonzero(frr <= far)[0].max()
    i2 = np.nonzero(frr >= far)[0].min()
    if frr[i1] + far[i1] <= frr[i2] + far[i2]:
        low, high, t = frr[i1], far[i1], thresholds[i1]
    else:
        low, high, t = far[i2], frr[i2], thresholds[i2]
    return (low + high) / 2, low, high, t


def plot_det(db, genuine, impostor, out_path):
    thresholds, far, frr = far_frr(genuine, impostor)
    eer, low, high, t = fvc_eer(thresholds, far, frr)

    floor = 0.5 / max(genuine.size, impostor.size)
    x, y = np.maximum(far, floor) * 100, np.maximum(frr, floor) * 100

    fig, ax = plt.subplots(figsize=(7, 6))
    ax.step(x, y, where='post', color='#1f5fa8', lw=2, label='DET eğrisi')
    lim = (floor * 100, 100)
    ax.plot(lim, lim, ls='--', color='gray', lw=1, label='FAR = FRR')
    ax.plot(eer * 100, eer * 100, 'o', color='#c0392b', ms=9, zorder=5,
            label=f'EER = {eer * 100:.2f}% (eşik = {t:g})')
    
    ax.annotate(f'EER = {eer * 100:.2f}%', (eer * 100, eer * 100), xytext=(-16, -30),
                textcoords='offset points', ha='right', color='#c0392b', fontsize=12, fontweight='bold',
                bbox=dict(boxstyle='round', fc='white', ec='#c0392b'),
                arrowprops=dict(arrowstyle='-', color='#c0392b'))
    ax.set_xscale('log')
    ax.set_yscale('log')
    ax.set_xlim(*lim)
    ax.set_ylim(*lim)
    ax.set_xlabel('FAR - False Accept Rate (%)')
    ax.set_ylabel('FRR - False Reject Rate (%)')
    ax.set_title(f'{db} DET eğrisi\n(gabor_filter + mindtct + bozorth3; '
                 f'{genuine.size} genuine, {impostor.size} impostor)')
    ax.grid(True, which='both', alpha=0.3)
    ax.legend(loc='upper right')
    fig.tight_layout()
    fig.savefig(out_path, dpi=120)
    plt.close(fig)
    return eer, low, high, t


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('databases', nargs='*', metavar='DB',
                   help='FVC2000DB1, FVC2002DB1, FVC2004DB1 (varsayilan: hepsi)')
    p.add_argument('--scores', default=os.path.join(ROOT, 'scores'), help='skor dosyalarinin dizini')
    p.add_argument('--out', default=os.path.join(ROOT, 'images'), help='DET grafiklerinin dizini')
    args = p.parse_args()
    unknown = [db for db in args.databases if db not in DATABASES]
    if unknown:
        p.error(f'bilinmeyen veri tabani: {", ".join(unknown)} (secenekler: {", ".join(DATABASES)})')
    os.makedirs(args.out, exist_ok=True)

    for db in args.databases or DATABASES:
        genuine = read_scores(os.path.join(args.scores, f'{db}_genuine_scores.txt'))
        impostor = read_scores(os.path.join(args.scores, f'{db}_impostor_scores.txt'))
        out_path = os.path.join(args.out, f'{db}_det.png')
        eer, low, high, t = plot_det(db, genuine, impostor, out_path)
        print(f'{db}: EER = {eer * 100:.2f}%  [{low * 100:.2f}%, {high * 100:.2f}%], esik = {t:g}'
              f'  -> {os.path.relpath(out_path, ROOT)}')

if __name__ == '__main__':
    main()
