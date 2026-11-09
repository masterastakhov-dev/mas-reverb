import numpy as np, subprocess, sys, os
from scipy import signal

HERE = os.path.dirname(os.path.abspath(__file__))
R = os.environ.get('MAS_RENDER', os.path.join(HERE, 'render'))
TMP = os.environ.get('MAS_TMP', '/tmp/mas-reverb-test')
os.makedirs(TMP, exist_ok=True)

def run(x, fs, pre=0, size=0.5, decay=2.5, damp=0.35, lowcut=20, mod=0.4, mix=1.0, block=256):
    x = np.asarray(x, dtype=np.float32)
    x.tofile(f'{TMP}/in.f32')
    r = subprocess.run([R, f'{TMP}/in.f32', f'{TMP}/out.f32', str(fs), str(pre), str(size), str(decay),
                        str(damp), str(lowcut), str(mod), str(mix), str(block)], capture_output=True, text=True)
    y = np.fromfile(f'{TMP}/out.f32', dtype=np.float32).reshape(-1, 2)
    return y, r.stderr.strip()

def rt60(ir, fs, band=None):
    h = ir.astype(np.float64)
    if band:
        lo, hi = band / np.sqrt(2), band * np.sqrt(2)
        sos = signal.butter(4, [lo, hi], btype='band', fs=fs, output='sos')
        h = signal.sosfilt(sos, h)
    e = np.cumsum(h[::-1] ** 2)[::-1]
    e = 10 * np.log10(e / e[0] + 1e-30)
    t = np.arange(len(e)) / fs
    i5 = np.argmax(e <= -5); i35 = np.argmax(e <= -35)
    if i35 == 0: return float('nan')
    slope = np.polyfit(t[i5:i35], e[i5:i35], 1)[0]
    return -60 / slope

if __name__ == '__main__':
    fs = 48000
    for size, decay, damp in [(0.5, 2.5, 0.0), (0.5, 2.5, 0.35), (0.2, 1.0, 0.35), (1.0, 6.0, 0.35), (0.8, 12.0, 0.2)]:
        n = int(fs * (decay * 2.2 + 1))
        x = np.zeros((n, 2), np.float32); x[100] = 1.0
        y, log = run(x, fs, size=size, decay=decay, damp=damp, mod=0.0)
        L, Rr = y[:, 0], y[:, 1]
        m = (L + Rr) / 2
        corr = np.corrcoef(L[200:int(fs*0.5)], Rr[200:int(fs*0.5)])[0, 1]
        print(f'size {size} decay {decay} damp {damp}: RT60 broadband {rt60(m, fs):.2f}s | 500Hz {rt60(m, fs, 500):.2f} 1k {rt60(m, fs, 1000):.2f} 4k {rt60(m, fs, 4000):.2f} | L/R corr {corr:+.2f} | peak {np.abs(y).max():.3f} | nan {np.isnan(y).any()} | {log}')
