"""Objective tests for the MASTER SPACE engine (tests/render_space.cpp).

Run:  g++ -O2 -std=c++17 -o tests/render_space tests/render_space.cpp && python3 tests/analyze_space.py
"""
import numpy as np, os, sys
from scipy import signal

HERE = os.path.dirname(os.path.abspath(__file__))
os.environ.setdefault('MV2_RENDER', os.path.join(HERE, 'render_space'))
sys.path.insert(0, HERE)
import analyze2 as a   # shared helpers: run(), impulse(), rt60(), ned(), peakiness()

fs = 48000
rng = np.random.default_rng(5)

def pink(n, ch=2, rms=0.1):
    w = rng.standard_normal((n, ch))
    b, aa = [0.049922035, -0.095993537, 0.050612699, -0.004408786], [1, -2.494956002, 2.017265875, -0.522189400]
    x = signal.lfilter(b, aa, w, axis=0)
    return (x / np.sqrt(np.mean(x ** 2)) * rms).astype(np.float32)

def kweight_lufs(y, sr=fs):
    # ITU-R BS.1770 K-weighting (pre-filter + RLB), integrated over the whole signal
    b1, a1 = [1.53512485958697, -2.69169618940638, 1.19839281085285], [1.0, -1.69065929318241, 0.73248077421585]
    b2, a2 = [1.0, -2.0, 1.0], [1.0, -1.99004745483398, 0.99007225036621]
    z = signal.lfilter(b2, a2, signal.lfilter(b1, a1, y, axis=0), axis=0)
    return -0.691 + 10 * np.log10(np.mean(z ** 2, axis=0).sum() + 1e-20)

def hf_ratio(y):
    f, P = signal.welch(y.mean(1), fs, nperseg=4096)
    return 10 * np.log10(P[(f > 4000) & (f < 12000)].sum() / P[(f > 200) & (f < 2000)].sum())

def side_ratio(y):
    m, s = 0.5 * (y[:, 0] + y[:, 1]), 0.5 * (y[:, 0] - y[:, 1])
    return 10 * np.log10(np.mean(s ** 2) / np.mean(m ** 2))

def section(t): print('\n== ' + t)

x = pink(fs * 5)
depths = [0.0, 0.25, 0.5, 0.75, 1.0]

section('DEPTH, Insert, mix 35 %: loudness (LUFS) vs depth, Level Lock off / on')
for lock in [0, 1]:
    vals = []
    for d in depths:
        y, _ = a.run(x, depth=d, lockLevel=lock)
        vals.append(kweight_lufs(y[fs:]))
    print(f'  lock {lock}: ' + '  '.join(f'{d:.2f}:{v:6.1f}' for d, v in zip(depths, vals)) + f'   spread {max(vals) - min(vals):.1f} dB')

section('DEPTH, Aux (wet only): loudness vs depth, Level Lock off / on')
for lock in [0, 1]:
    vals = []
    for d in depths:
        y, _ = a.run(x, depth=d, routing=1, lockLevel=lock)
        vals.append(kweight_lufs(y[fs:]))
    print(f'  lock {lock}: ' + '  '.join(f'{d:.2f}:{v:6.1f}' for d, v in zip(depths, vals)) + f'   spread {max(vals) - min(vals):.1f} dB')

section('DEPTH, Insert, Early only (late off): direct-to-early ratio and first reflection time')
for d in depths:
    y, _ = a.run(a.impulse(fs, 0.4), depth=d, lateOn=0, mix=0.5, lockLevel=0, erDiffusion=0)
    m = y.mean(1)
    direct = np.sum(m[8:14] ** 2)
    early = np.sum(m[14 + int(0.0025 * fs):] ** 2)
    idx = np.nonzero(np.abs(m[14:]) > np.abs(m[14:]).max() * 0.05)[0]
    print(f'  depth {d:.2f}: D/E {10 * np.log10(direct / early):+6.1f} dB, first reflection {idx[0] / fs * 1000:5.1f} ms')

section('TONE LOCK: HF balance (4-12k vs 0.2-2k, dB) vs depth')
for lock in [0, 1]:
    vals = [hf_ratio(a.run(x, depth=d, lockTone=lock, lockLevel=1)[0][fs:]) for d in depths]
    print(f'  lock {lock}: ' + '  '.join(f'{d:.2f}:{v:6.1f}' for d, v in zip(depths, vals)) + f'   spread {max(vals) - min(vals):.1f} dB')

section('WIDTH LOCK: wet side/mid ratio (dB) vs depth, mono source, Aux, early only')
xm = np.repeat(x[:, :1], 2, axis=1)
for lock in [0, 1]:
    vals = [side_ratio(a.run(xm, depth=d, routing=1, lateOn=0, lockWidth=lock, crossfeed=0)[0][fs:]) for d in depths]
    print(f'  lock {lock}: ' + '  '.join(f'{d:.2f}:{v:6.1f}' for d, v in zip(depths, vals)) + f'   spread {max(vals) - min(vals):.1f} dB')

section('DECAY LOCK: late RT60 vs depth')
for lock in [0, 1]:
    vals = []
    for d in [0.0, 0.5, 1.0]:
        y, _ = a.run(a.impulse(fs, 5), depth=d, routing=1, erOn=0, decay=2.0, lockDecay=lock, modDepth=0)
        vals.append(a.rt60(y.mean(1), fs))
    print(f'  lock {lock}: ' + '  '.join(f'{v:.2f}s' for v in vals))

section('Materials: early energy and HF balance (Aux, early only)')
for name, mat in [('all absorber', 0), ('all curtain', 1), ('all wood', 2), ('all plaster', 3), ('all concrete', 4)]:
    kw = {f'mat{i}': mat for i in range(6)}
    y, _ = a.run(x, routing=1, lateOn=0, lockLevel=0, **kw)
    print(f'  {name:13s}: level {kweight_lufs(y[fs:]):6.1f} LUFS, HF balance {hf_ratio(y[fs:]):6.1f} dB')

section('Room size: first-reflection time and early span (Aux, early only)')
for (w, dd, h) in [(4, 5, 2.6), (7.5, 10, 4), (20, 30, 12)]:
    y, _ = a.run(a.impulse(fs, 0.5), routing=1, lateOn=0, roomW=w, roomD=dd, roomH=h, erDiffusion=0, lockLevel=0, erStopMs=300)
    m = np.abs(y.mean(1)); idx = np.nonzero(m > m.max() * 0.05)[0]
    print(f'  {w}x{dd}x{h} m: first {(idx[0] - 10) / fs * 1000:5.1f} ms, last strong {(idx[-1] - 10) / fs * 1000:6.1f} ms')

section('Comb colouration of direct + early: notch depth / peak height vs smoothed spectrum (dB)')
def comb(seg):
    f, P = signal.welch(seg, fs, nperseg=4096)
    band = (f > 200) & (f < 8000)
    Pd = 10 * np.log10(P[band] + 1e-30); dev = Pd - signal.medfilt(Pd, 31)
    return np.percentile(dev, 1), np.percentile(dev, 99)
for diff in [0.0, 0.4, 0.8]:
    y, _ = a.run(x, lateOn=0, mix=0.5, erDiffusion=diff, lockLevel=0)
    lo, hi = comb(y[fs:, 0])
    print(f'  ER diffusion {diff}: notches {lo:6.1f} dB, peaks {hi:+5.1f} dB')

section('Automation while playing (max 2nd difference vs steady state)')
noise = (rng.standard_normal((fs * 4, 2)) * 0.1).astype(np.float32)
base, _ = a.run(noise, block=128)
ref = np.percentile(np.abs(np.diff(base[:, 0], 2))[fs:], 99.99)
for name, ch in {'depth 0 -> 1': [(fs * 2, 'depth', 1.0)], 'source x 0.2 -> 0.8': [(fs * 2, 'srcX', 0.8)],
                 'room 7.5x10 -> 20x30': [(fs * 2, 'roomW', 20), (fs * 2, 'roomD', 30)], 'Aux -> Insert': [(fs * 2, 'routing', 0)],
                 'material plaster -> absorber': [(fs * 2, 'mat2', 0)], 'level lock off': [(fs * 2, 'lockLevel', 0)]}.items():
    y, _ = a.run(noise, block=128, depth=0.0 if 'depth' in name else 0.35, at_=ch)
    w = np.abs(np.diff(y[:, 0], 2))[fs * 2 - 200: fs * 2 + int(0.3 * fs)]
    print(f'  {name:30s}: {w.max() / ref:4.2f} x, nan {np.isnan(y).any()}')

section('Stability and CPU')
for sr in [48000, 96000]:
    for kw in [dict(), dict(decay=30, lfMult=4, lmMult=4, hmMult=4, hfMult=4, size=1, roomW=40, roomD=60, roomH=20, erDensity=1, depth=1),
               dict(decay=0.1, size=0, roomW=2, roomD=2, roomH=2, depth=0, erDensity=1, spreadMs=500, preDelayMs=500)]:
        z = (rng.standard_normal((sr * 3, 2)) * 0.1).astype(np.float32)
        y, log = a.run(z, fs=sr, **kw)
        print(f'  {sr} Hz {"default" if not kw else "extreme"}: peak {np.abs(y).max():.3f}, nan {np.isnan(y).any()}, {log}')
