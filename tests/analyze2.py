"""Objective tests for the MASTER VERB 2.0 engine (tests/render2.cpp)."""
import numpy as np, subprocess, os, sys
from scipy import signal
from scipy.special import erfc

HERE = os.path.dirname(os.path.abspath(__file__))
R = os.environ.get('MV2_RENDER', os.path.join(HERE, 'render2'))
TMP = os.environ.get('MAS_TMP', '/tmp/mv2-test')
os.makedirs(TMP, exist_ok=True)

def run(x, fs=48000, block=256, **kw):
    x = np.asarray(x, dtype=np.float32)
    x.tofile(f'{TMP}/in.f32')
    args = [R, f'{TMP}/in.f32', f'{TMP}/out.f32', str(fs), str(block)]
    for k, v in kw.items():
        if k.startswith('at_'):
            for frame, kk, vv in v: args.append(f'@{frame}:{kk}={vv}')
        else:
            args.append(f'{k}={v}')
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode: raise RuntimeError(r.stderr)
    y = np.fromfile(f'{TMP}/out.f32', dtype=np.float32).reshape(-1, 2)
    return y, r.stderr.strip()

def impulse(fs, secs):
    x = np.zeros((int(fs * secs), 2), np.float32); x[10] = 1.0
    return x

def rt60(h, fs, band=None):
    h = h.astype(np.float64)
    if band:
        sos = signal.butter(3, [band / np.sqrt(2), min(band * np.sqrt(2), fs * 0.45)], btype='band', fs=fs, output='sos')
        h = signal.sosfilt(sos, h)
    e = np.cumsum(h[::-1] ** 2)[::-1]; e = 10 * np.log10(e / e[0] + 1e-30)
    t = np.arange(len(e)) / fs
    i5 = np.argmax(e <= -5); i25 = np.argmax(e <= -25)
    if i25 == 0: return float('nan')
    return -60 / np.polyfit(t[i5:i25], e[i5:i25], 1)[0]

def ned(h, fs, win_ms=20):
    w = int(fs * win_ms / 1000); out = []
    for i in range(0, len(h) - w, w // 2):
        seg = h[i:i + w]; sd = seg.std()
        out.append(np.mean(np.abs(seg) > sd) / erfc(1 / np.sqrt(2)) if sd > 0 else 0)
    return np.array(out), (np.arange(len(out)) * (w // 2) + w / 2) / fs

def peakiness(seg, fs):
    f, P = signal.welch(seg, fs, nperseg=8192)
    band = (f > 150) & (f < 6000)
    Pd = 10 * np.log10(P[band] + 1e-30)
    dev = Pd - signal.medfilt(Pd, 101)
    return float(np.percentile(dev, 99.5))

def section(title): print('\n== ' + title)

if __name__ == '__main__':
    fs = 48000
    names = ['Studio Room', 'Chamber', 'Hall']

    section('RT60 per band (late only, mults LF1.2 LM1.0 HM0.85 HF0.55)')
    for algo in range(3):
        for decay in [0.5, 1.8, 6.0]:
            y, log = run(impulse(fs, decay * 2.5 + 0.5), algorithm=algo, decay=decay, mix=1, erOn=0, modDepth=0)
            m = y.mean(1)
            bands = {b: rt60(m, fs, b) for b in [125, 500, 1000, 3000, 8000]}
            print(f'{names[algo]:12s} decay {decay:4.1f}: broadband {rt60(m, fs):5.2f} | ' +
                  ' '.join(f'{b}:{v:5.2f}' for b, v in bands.items()) + f' | peak {np.abs(y).max():.3f} | {log}')

    section('Independent band control: Chamber 2 s, LF x2.0, HF x0.25')
    y, _ = run(impulse(fs, 6), algorithm=1, decay=2.0, mix=1, erOn=0, lfMult=2.0, lmMult=1.0, hmMult=1.0, hfMult=0.25, modDepth=0)
    m = y.mean(1)
    print('  ' + ' '.join(f'{b}:{rt60(m, fs, b):5.2f}' for b in [125, 500, 1000, 3000, 8000]))

    section('Echo density (NED >= 0.9 time) vs Density / Diffusion, Chamber 2 s')
    for dens, diff in [(0.1, 0.1), (0.1, 0.9), (0.9, 0.1), (0.9, 0.9)]:
        y, _ = run(impulse(fs, 1.5), algorithm=1, decay=2.0, mix=1, erOn=0, density=dens, diffusion=diff, spreadMs=0, lateDelayMs=0, preDelayMs=0, modDepth=0)
        n, t = ned(y[:, 0], fs)
        hit = t[np.argmax(n >= 0.9)] if (n >= 0.9).any() else float('nan')
        print(f'  density {dens} diffusion {diff}: NED>=0.9 at {hit * 1000:6.1f} ms, late NED mean {n[t > 0.3].mean():.2f}')

    section('Tail resonances (99.5th pct spectral deviation, dB; lower = smoother)')
    for algo in range(3):
        for mod in [0.0, 0.35]:
            y, _ = run(impulse(fs, 3), algorithm=algo, decay=2.5, mix=1, erOn=0, modDepth=mod)
            print(f'  {names[algo]:12s} mod {mod}: {peakiness(y[int(0.4 * fs):int(1.9 * fs), 0], fs):5.1f} dB')

    section('Shape / Spread: energy-envelope peak time (Chamber, spread 250 ms, decay 3 s)')
    for shape in [0.0, 0.5, 1.0]:
        y, _ = run(impulse(fs, 2), algorithm=1, decay=3.0, mix=1, erOn=0, shape=shape, spreadMs=250, lateDelayMs=0, preDelayMs=0)
        env = np.sqrt(signal.lfilter([1], [1, -0.995], y.mean(1) ** 2))
        print(f'  shape {shape}: envelope peak at {np.argmax(env) / fs * 1000:6.1f} ms')

    section('Early reflections: first arrivals (ER only), Studio pattern')
    for pos in [0.0, 1.0]:
        y, _ = run(impulse(fs, 0.4), algorithm=0, mix=1, lateOn=0, erPosition=pos, erDiffusion=0, preDelayMs=0)
        m = np.abs(y.mean(1)); idx = np.nonzero(m > m.max() * 0.15)[0]
        print(f'  position {pos}: first {(idx[0] - 10) / fs * 1000:5.1f} ms, last {(idx[-1] - 10) / fs * 1000:5.1f} ms, energy {np.sum(y ** 2):.3f}')
    y, _ = run(impulse(fs, 0.5), algorithm=2, mix=1, lateOn=0, erStartMs=20, erStopMs=60, erDiffusion=0, preDelayMs=0)
    m = np.abs(y.mean(1)); idx = np.nonzero(m > 1e-4)[0]
    print(f'  Hall, start 20 / stop 60 ms: arrivals between {(idx[0] - 10) / fs * 1000:.1f} and {(idx[-1] - 10) / fs * 1000:.1f} ms')

    section('Stereo')
    rng = np.random.default_rng(1)
    noise = (rng.standard_normal((fs * 4, 2)) * 0.1).astype(np.float32)
    for decor in [0.0, 0.85]:
        y, _ = run(noise, algorithm=1, decay=2.0, mix=1, decorrelation=decor)
        s = y[fs:]
        c = np.corrcoef(s[:, 0], s[:, 1])[0, 1]
        mono = 20 * np.log10(np.std(s.mean(1)) / np.std(s))
        print(f'  decorrelation {decor}: L/R corr {c:+.2f}, mono sum vs stereo {mono:+.1f} dB')
    for algo in range(3):
        y, _ = run(noise, algorithm=algo, decay=1.8, mix=1)
        print(f'  {names[algo]:12s} wet level re input (noise): {20 * np.log10(np.std(y[fs:]) / 0.1):+.1f} dB')

    section('Stability (extremes, long run)')
    worst = 0
    for kw in [dict(decay=30, lfMult=4, lmMult=4, hmMult=4, hfMult=4, size=1, density=1, diffusion=1, modDepth=1, modCharacter=2),
               dict(decay=0.1, size=0, density=0, diffusion=0, spreadMs=500, shape=1, lateDelayMs=300, preDelayMs=500),
               dict(decay=12, algorithm=2, size=1, erSize=2, erPosition=1, erStopMs=300, erPreDelayMs=200, modDepth=1, modCharacter=1, modRate=5)]:
        y, log = run(noise[:fs * 3], mix=1, **kw)
        y2, _ = run(np.zeros((fs * 20, 2), np.float32), mix=1, **kw)
        pk = float(np.abs(y).max()); worst = max(worst, pk)
        print(f'  peak {pk:.3f} nan {np.isnan(y).any()} | silence after: max {np.abs(y2).max():.2e} | {log}')

    section('Automation clicks (max 2nd difference around change vs steady state)')
    base, _ = run(noise, algorithm=1, decay=2.0, mix=1, block=128)
    d2 = lambda z: np.abs(np.diff(z[:, 0], 2))
    ref = np.percentile(d2(base)[fs:], 99.99)
    tests = {
        'size 0.2 -> 0.9': [(fs * 2, 'size', 0.9)],
        'algorithm Chamber -> Hall': [(fs * 2, 'algorithm', 2)],
        'ER pattern -> Hall': [(fs * 2, 'erPattern', 4)],
        'position 0 -> 1': [(fs * 2, 'erPosition', 1.0)],
        'HF mult 0.55 -> 2.0': [(fs * 2, 'hfMult', 2.0)],
        'pre-delay 10 -> 300 ms': [(fs * 2, 'preDelayMs', 300)],
        'ER off': [(fs * 2, 'erOn', 0)],
    }
    for name, ch in tests.items():
        y, _ = run(noise, algorithm=1, decay=2.0, mix=1, block=128, at_=ch)
        w = d2(y)[fs * 2 - 200: fs * 2 + int(0.3 * fs)]
        print(f'  {name:26s}: max d2 {w.max() / ref:4.2f} x steady-state 99.99th pct, nan {np.isnan(y).any()}')

    section('Sample rates')
    for sr in [44100, 88200, 96000]:
        y, log = run(impulse(sr, 4), fs=sr, algorithm=1, decay=1.8, mix=1, erOn=0, modDepth=0)
        print(f'  {sr}: RT60 {rt60(y.mean(1), sr):.2f} s, 1k {rt60(y.mean(1), sr, 1000):.2f}, peak {np.abs(y).max():.3f}, {log}')
