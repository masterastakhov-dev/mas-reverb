# MASTER SPACE 1.4 measurements: SMART DUCKER / RESONANCE GUARD.
# Engine-level renders through tests/render_space.cpp built with -DMAS_ATMOS -DMAS_RI (the same DSP as the plugin).
#
#   g++ -O2 -std=c++17 -DMAS_ATMOS -DMAS_RI -o /tmp/r14 tests/render_space.cpp
#   python3 tests/ri/measure_14.py --render /tmp/r14 --vocal <dry vocal .wav> --out <dir> [parts]
# parts: cpu timing spectral vocal decays materials map mono automation (default: all)
import argparse, json, os, subprocess, sys, tempfile
import numpy as np
from scipy import signal

ap = argparse.ArgumentParser()
ap.add_argument('--render', required=True)
ap.add_argument('--vocal', required=True)
ap.add_argument('--out', required=True)
ap.add_argument('parts', nargs='*')
A = ap.parse_args()
PARTS = set(A.parts) or {'cpu', 'timing', 'spectral', 'vocal', 'decays', 'materials', 'map', 'mono', 'automation'}
OUT = A.out
os.makedirs(OUT, exist_ok=True)
TMP = tempfile.mkdtemp()
FS = 48000
RESULTS = {}
if os.path.exists(os.path.join(OUT, 'results.json')):
    RESULTS = json.load(open(os.path.join(OUT, 'results.json')))

# ---------------------------------------------------------------------------- helpers
def render(x, args, fs=FS, block=256, sc=None, meter=False, cutmap=False):
    """x: (n, 2) float -> (y, meter (k, 3) or None, cutmap (frames, bands) or None, hz, stderr text)"""
    inp, out = os.path.join(TMP, 'in.f32'), os.path.join(TMP, 'out.f32')
    np.asarray(x, '<f4').tofile(inp)
    cmd = [A.render, inp, out, str(fs), str(block)] + list(args)
    if sc is not None:
        np.asarray(sc, '<f4').tofile(os.path.join(TMP, 'sc.f32')); cmd.append('scfile=' + os.path.join(TMP, 'sc.f32'))
    if meter: cmd.append('meter=' + os.path.join(TMP, 'm.f32'))
    if cutmap: cmd.append('cutmap=' + os.path.join(TMP, 'c.f32'))
    p = subprocess.run(cmd, capture_output=True, text=True, check=True)
    y = np.fromfile(out, '<f4').reshape(-1, 2).astype(np.float64)
    m = np.fromfile(os.path.join(TMP, 'm.f32'), '<f4').reshape(-1, 3) if meter else None
    c = hz = None
    if cutmap:
        hz = np.fromfile(os.path.join(TMP, 'c.f32.hz'), '<f4')
        c = np.fromfile(os.path.join(TMP, 'c.f32'), '<f4').reshape(-1, len(hz))
    return y, m, c, hz, p.stderr

def stereo(m): return np.stack([m, m], 1)

def kweight(x, fs=FS):
    b1, a1 = signal.iirfilter(2, 1500 / (fs / 2), btype='highpass', ftype='butter')   # shelf approximation
    sos_hp = signal.butter(2, 38 / (fs / 2), btype='highpass', output='sos')
    # BS.1770 pre-filter: high shelf +4 dB at ~1.7 kHz, then RLB high-pass
    f0, G, Q = 1681.97, 3.99984, 0.7071752
    K = np.tan(np.pi * f0 / fs); Vh = 10 ** (G / 20); Vb = Vh ** 0.4996667741545416
    a0 = 1 + K / Q + K * K
    b = [(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0]
    a = [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0]
    y = signal.lfilter(b, a, x, axis=0)
    return signal.sosfilt(sos_hp, y, axis=0)

def lufs(x, fs=FS):
    y = kweight(x, fs); w = int(0.4 * fs); hop = int(0.1 * fs)
    ms = np.array([np.mean(np.sum(y[i:i + w] ** 2, 1)) for i in range(0, len(y) - w, hop)])
    l = -0.691 + 10 * np.log10(ms + 1e-20)
    g = ms[l > -70]
    if len(g) == 0: return -120.0
    rel = -0.691 + 10 * np.log10(g.mean()) - 10
    g2 = ms[l > max(-70, rel)]
    return -0.691 + 10 * np.log10(g2.mean())

def write_wav(path, y, fs=FS):
    import soundfile as sf
    peak = np.max(np.abs(y))
    if peak > 0.999: y = y * (0.999 / peak)
    sf.write(path, y.astype(np.float32), fs, subtype='PCM_24')

def db(x): return 10 * np.log10(np.maximum(x, 1e-30))

def env_db(x, w=480):
    e = (x[: len(x) // w * w] ** 2).reshape(-1, w, *x.shape[1:]).mean(1)
    if e.ndim > 1: e = e.sum(1)
    return db(e)

def third_bands(lo=63, hi=16000):
    c = 1000 * 2 ** (np.arange(-24, 15) / 3)
    return c[(c >= lo) & (c <= hi)]

def band_levels(x, centers, fs=FS):
    f, P = signal.welch(x, fs, nperseg=8192, axis=0)
    if P.ndim > 1: P = P.sum(1)
    return np.array([db(P[(f >= c * 2 ** -(1 / 6)) & (f < c * 2 ** (1 / 6))].sum()) for c in centers])

def save():
    json.dump(RESULTS, open(os.path.join(OUT, 'results.json'), 'w'), indent=1, default=float)

# ATMOS base (factory "ATMOS Vocal Glow", Aux 100 % wet; decay set per test)
ATMOS = ['tailMode=1', 'routing=1', 'mix=1', 'erOn=1', 'erLevelDb=-12', 'lateLevelDb=0', 'diffusion=0.7', 'density=0.7',
         'shape=0.35', 'spreadMs=60', 'lateDelayMs=12', 'lfMult=0.9', 'lmMult=1', 'hmMult=1', 'hfMult=1', 'xLow=250',
         'xMid=2000', 'xHigh=7000', 'lowCutHz=90', 'crossfeed=0.15', 'decorrelation=0.85', 'bloom=0.25', 'motion=0.5', 'air=0.55']
DUCK = ['duckOn=1', 'duckMode=1']                      # plugin defaults: Vocal Focus, 6 dB, -30 dBFS, 10/120/600 ms, 4:1
RG = ['rgOn=1', 'rgMode=0', 'rgMaxDb=3']               # plugin defaults: Smooth, max 3 dB, Late target, LIVE

# ---------------------------------------------------------------------------- materials (48 kHz mono)
def load_vocal():
    import soundfile as sf
    v, fs = sf.read(A.vocal, always_2d=True); v = v.mean(1)
    if fs != FS: v = signal.resample_poly(v, FS, fs)
    return v

def vocal_phrases(v):
    # the dry vocal with two 2.5 s pauses inserted at phrase ends, so the tail can open
    cut = [int(3.53 * FS), int(6.95 * FS)]; gap = np.zeros(int(2.5 * FS))
    return np.concatenate([v[:cut[0]], gap, v[cut[0]:cut[1]], gap, v[cut[1]:], np.zeros(int(4 * FS))])

def adsr(n, a, r):
    e = np.ones(n); na, nr = int(a * FS), int(r * FS)
    e[:na] = np.linspace(0, 1, na); e[-nr:] *= np.linspace(1, 0, nr); return e

def sustained_vowels(rng):
    out = []
    for f0 in (220.0, 246.9, 196.0):
        n = int(2.2 * FS); t = np.arange(n) / FS
        vib = 1 + 0.006 * np.sin(2 * np.pi * 5.5 * t) * np.minimum(1, t / 0.6)
        ph = np.cumsum(2 * np.pi * f0 * vib / FS)
        g = sum(np.sin(k * ph) / k ** 1.2 for k in range(1, 40) if k * f0 < 8000)
        for fc, bw in ((800, 80), (1150, 90), (2900, 120), (3900, 130)):
            b, a = signal.iirpeak(fc, fc / bw, FS); g = g + 0.6 * signal.lfilter(b, a, g)
        g = g / np.max(np.abs(g)) * 0.25 * adsr(n, 0.08, 0.25)
        out += [g, np.zeros(int(0.7 * FS))]
    return np.concatenate(out + [np.zeros(int(3 * FS))])

def sax(rng):
    notes = [233.1, 261.6, 293.7, 349.2, 311.1, 293.7, 261.6]; durs = [0.5, 0.5, 0.6, 1.2, 0.5, 0.5, 2.0]
    out = []
    for f0, d in zip(notes, durs):
        n = int(d * FS); t = np.arange(n) / FS
        vib = 1 + 0.008 * np.sin(2 * np.pi * 5.0 * t) * np.clip((t - 0.3) / 0.4, 0, 1)
        ph = np.cumsum(2 * np.pi * f0 * vib / FS)
        x = sum(np.sin(k * ph) * (1 / k) * (1.4 if k % 2 else 1.0) for k in range(1, 30) if k * f0 < 9000)
        for fc, q in ((520, 3), (1450, 4), (2600, 5)):
            b, a = signal.iirpeak(fc, q, FS); x = x + 0.8 * signal.lfilter(b, a, x)
        breath = signal.lfilter(*signal.butter(2, [2000 / (FS / 2), 6000 / (FS / 2)], 'band'), rng.standard_normal(n)) * 0.05
        x = (x / np.max(np.abs(x)) + breath) * 0.22 * adsr(n, 0.03, 0.08)
        out.append(x)
    return np.concatenate(out + [np.zeros(int(4 * FS))])

def guitar(rng):
    chords = [[82.4, 123.5, 164.8, 207.7, 246.9, 329.6], [110.0, 164.8, 220.0, 277.2, 329.6], [98.0, 146.8, 196.0, 246.9, 293.7, 392.0]]
    total = np.zeros(int(9 * FS))
    for ci, ch in enumerate(chords):
        for s, f0 in enumerate(ch):
            p = int(FS / f0); buf = rng.uniform(-1, 1, p); n = int(2.6 * FS); y = np.zeros(n)
            for i in range(n):
                y[i] = buf[i % p]; buf[i % p] = 0.996 * 0.5 * (buf[i % p] + buf[(i + 1) % p])
            st = int((0.2 + ci * 2.6) * FS + s * 0.018 * FS)
            total[st:st + n] += y * 0.12
    for fc, q, g in ((110, 2, 1.2), (220, 3, 0.6), (2500, 1, 0.3)):
        b, a = signal.iirpeak(fc, q, FS); total = total + g * signal.lfilter(b, a, total)
    return total / np.max(np.abs(total)) * 0.4

def piano(rng):
    events = [(0.2, [130.8, 196.0, 261.6, 329.6]), (1.4, [392.0]), (1.9, [440.0]), (2.4, [523.3]), (3.2, [110.0, 164.8, 220.0, 261.6]),
              (4.6, [349.2, 440.0, 523.3]), (6.0, [130.8, 196.0, 261.6, 329.6, 392.0])]
    total = np.zeros(int(10 * FS)); B = 0.0004
    for t0, fs_ in events:
        for f0 in fs_:
            n = int(3.5 * FS); t = np.arange(n) / FS
            y = sum(np.sin(2 * np.pi * k * f0 * np.sqrt(1 + B * k * k) * t) * np.exp(-t * (0.5 + 0.12 * k)) / k ** 0.9
                    for k in range(1, 20) if k * f0 < 10000)
            y[:200] += rng.standard_normal(200) * np.linspace(0.3, 0, 200)
            st = int(t0 * FS); total[st:st + n] += y * 0.1
    return total / np.max(np.abs(total)) * 0.4

def drums(rng):
    total = np.zeros(int(7 * FS)); beat = 60 / 100
    def kick():
        t = np.arange(int(0.35 * FS)) / FS; f = 50 + 100 * np.exp(-t / 0.03)
        return np.sin(2 * np.pi * np.cumsum(f) / FS) * np.exp(-t / 0.12)
    def snare():
        t = np.arange(int(0.2 * FS)) / FS
        nz = signal.lfilter(*signal.butter(2, [1000 / (FS / 2), 6000 / (FS / 2)], 'band'), rng.standard_normal(len(t)))
        return (0.7 * nz * np.exp(-t / 0.05) + 0.5 * np.sin(2 * np.pi * 190 * t) * np.exp(-t / 0.04))
    def hat():
        t = np.arange(int(0.06 * FS)) / FS
        return signal.lfilter(*signal.butter(2, 8000 / (FS / 2), 'high'), rng.standard_normal(len(t))) * np.exp(-t / 0.015) * 0.5
    for bar in range(2):
        for k in range(8):
            t0 = int((0.2 + (bar * 8 + k) * beat / 2) * FS)
            h = hat(); total[t0:t0 + len(h)] += h
            if k in (0, 3, 5): x = kick(); total[t0:t0 + len(x)] += x
            if k in (2, 6): x = snare(); total[t0:t0 + len(x)] += x
    return total / np.max(np.abs(total)) * 0.5

def impulses(rng):
    x = np.zeros(int(9 * FS))
    for t in (0.2, 3.2, 6.2): x[int(t * FS)] = 0.5
    return x

def broadband(rng):
    x = np.zeros(int(9 * FS)); b = [0.0] * 3
    pink = signal.lfilter([0.049922035, -0.095993537, 0.050612699, -0.004408786], [1, -2.494956002, 2.017265875, -0.522189400],
                          rng.standard_normal(len(x)))
    for t in (0.2, 3.2, 6.2):
        i = int(t * FS); n = int(0.5 * FS); x[i:i + n] = pink[i:i + n] * adsr(n, 0.005, 0.005)
    return x / np.max(np.abs(x)) * 0.35

def pink(n, rng, level=0.1):
    p = signal.lfilter([0.049922035, -0.095993537, 0.050612699, -0.004408786], [1, -2.494956002, 2.017265875, -0.522189400],
                       rng.standard_normal(n))
    return p / np.std(p) * level

# ---------------------------------------------------------------------------- 5. CPU
if 'cpu' in PARTS:
    res = {}
    rng = np.random.default_rng(1)
    cfgs = [('modules off', []), ('SMART DUCKER Vocal Focus', ['duckOn=1', 'duckMode=1']), ('SMART DUCKER Spectral', ['duckOn=1', 'duckMode=2']),
            ('RESONANCE GUARD LIVE Smooth', ['rgOn=1', 'rgMode=0', 'rgTarget=0']), ('RESONANCE GUARD LIVE Deep', ['rgOn=1', 'rgMode=1', 'rgTarget=0']),
            ('RESONANCE GUARD STUDIO HQ', ['rgOn=1', 'rgMode=0', 'rgTarget=0', 'quality=1']),
            ('both LIVE', ['duckOn=1', 'duckMode=2', 'rgOn=1', 'rgMode=1', 'rgTarget=0']),
            ('both STUDIO HQ', ['duckOn=1', 'duckMode=2', 'rgOn=1', 'rgMode=1', 'rgTarget=0', 'quality=1'])]
    for fs in (48000, 96000):
        x = stereo(pink(fs * 10, rng))
        for blk in (64, 256):
            for name, extra in cfgs:
                runs = []
                for k in range(5):
                    _, _, _, _, err = render(x, ATMOS + ['decay=6'] + extra, fs=fs, block=blk)
                    rt = float(err.split('rt ')[1].split('x')[0])
                    p999 = float(err.split('p99.9 ')[1].split(' %')[0]); worst = float(err.split('worst ')[1].split(' %')[0])
                    runs.append((100 / rt, p999, worst))
                r = np.array(runs)
                res[f'{fs}/{blk}/{name}'] = {'cpu_pct': float(r[:, 0].min()), 'block_p999_pct': float(np.median(r[:, 1])),
                                              'block_worst_pct': float(np.median(r[:, 2]))}
                print(f'cpu {fs} {blk:4d} {name:30s} {r[:, 0].min():5.2f} %  p99.9 block {np.median(r[:, 1]):5.1f} %  worst {np.median(r[:, 2]):5.1f} %', flush=True)
    RESULTS['cpu'] = res; save()

# ---------------------------------------------------------------------------- 2a. ducker ballistics (external key, tone burst)
if 'timing' in PARTS:
    rng = np.random.default_rng(2)
    n = int(6 * FS); x = stereo(pink(n, rng, 0.05))
    key = np.zeros(n); t = np.arange(n) / FS
    on, off = int(1.0 * FS), int(3.0 * FS)
    key[on:off] = 0.25 * np.sin(2 * np.pi * 1000 * t[on:off])
    res = []
    for mode, mname in ((0, 'Classic'), (1, 'Vocal Focus')):
        for att, hold, rel in ((1, 0, 50), (10, 120, 600), (50, 250, 1500), (100, 500, 3000)):
            _, m, _, _, _ = render(x, ATMOS + ['decay=3', 'duckOn=1', f'duckMode={mode}', 'duckAmountDb=12', 'duckThreshDb=-40',
                                               'duckRatio=10', f'duckAttackMs={att}', f'duckHoldMs={hold}', f'duckReleaseMs={rel}', 'scExternal=1'],
                                   block=16, sc=stereo(key), meter=True)
            gr = m[:, 1]; tt = np.arange(len(gr)) * 16 / FS
            steady = np.median(gr[(tt > 2.5) & (tt < 2.95)])
            def first(cond, after):
                idx = np.where(cond & (tt >= after))[0]; return tt[idx[0]] if len(idx) else np.nan
            a63 = first(gr >= 0.632 * steady, 1.0) - 1.0
            a90 = first(gr >= 0.9 * steady, 1.0) - 1.0
            h = first(gr <= 0.98 * steady, 3.0) - 3.0
            r37 = first(gr <= 0.368 * steady, 3.0) - 3.0 - h
            r10 = first(gr <= 0.1 * steady, 3.0) - 3.0 - h
            zero = first(gr <= 1e-6, 3.0) - 3.0
            row = dict(mode=mname, attack=att, hold=hold, release=rel, steady_db=float(steady), attack63_ms=1000 * a63, attack90_ms=1000 * a90,
                       hold_ms=1000 * h, release37_ms=1000 * r37, release90_ms=1000 * r10, back_to_0_s=float(zero))
            res.append(row)
            print('timing', {k: (round(v, 1) if isinstance(v, float) else v) for k, v in row.items()}, flush=True)
    RESULTS['timing'] = res; save()

# ---------------------------------------------------------------------------- 2b. SPECTRAL mode: where it reduces, jitter
if 'spectral' in PARTS:
    rng = np.random.default_rng(3)
    n = int(8 * FS); x = stereo(pink(n, rng, 0.06)); t = np.arange(n) / FS
    # key: a sung vowel (f0 196 Hz) from 2 to 6 s, harmonics up to ~4 kHz
    key = np.zeros(n); seg = (t >= 2) & (t < 6)
    ph = 2 * np.pi * 196 * t
    k = sum(np.sin(h * ph) / h for h in range(1, 21))
    for fc, bw in ((700, 90), (1100, 100), (2600, 140)):
        b, a = signal.iirpeak(fc, fc / bw, FS); k = k + 0.7 * signal.lfilter(b, a, k)
    k = k / np.max(np.abs(k))
    cen = third_bands(80, 12000)
    res = {'bands_hz': cen.tolist()}
    for klev in (0.3, 0.06):
        key[:] = 0.0
        key[seg] = klev * k[seg] * adsr(seg.sum(), 0.05, 0.05)
        base = ATMOS + ['decay=3', 'scExternal=1', 'duckAmountDb=9', 'duckThreshDb=-40']
        y0, _, _, _, _ = render(x, base, sc=stereo(key))
        a0, a1 = int(3 * FS), int(5.8 * FS)
        drr = band_levels(stereo(key)[a0:a1], cen) - band_levels(y0[a0:a1], cen)      # direct (key) vs reverb, per band
        sub = res[f'key {20 * np.log10(klev):.0f} dBFS peak'] = {'drr_db': drr.tolist()}
        for mode, name in ((0, 'Classic'), (1, 'Vocal Focus'), (2, 'Spectral')):
            y, _, _, _, _ = render(x, base + ['duckOn=1', f'duckMode={mode}'], sc=stereo(key))
            red = band_levels(y[a0:a1], cen) - band_levels(y0[a0:a1], cen)
            f, tt, Z0 = signal.stft(y0[a0:a1].sum(1), FS, nperseg=2400, noverlap=0)
            _, _, Z1 = signal.stft(y[a0:a1].sum(1), FS, nperseg=2400, noverlap=0)
            jit = []
            for c in cen:
                sel = (f >= c * 2 ** -(1 / 6)) & (f < c * 2 ** (1 / 6))
                if sel.sum() == 0: jit.append(np.nan); continue
                r = db((np.abs(Z1[sel]) ** 2).sum(0)) - db((np.abs(Z0[sel]) ** 2).sum(0))
                jit.append(np.std(r[1:-1]))
            sub[name] = {'reduction_db': red.tolist(), 'jitter_db': [float(j) for j in jit]}
            masking = np.abs(drr) < 10; dominant = drr >= 10; absent = drr <= -10
            print(f'spectral key {klev}: {name:11s} masking zone (|DRR|<10 dB, {masking.sum()} bands) {red[masking].mean() if masking.any() else np.nan:+.2f} dB, '
                  f'direct dominant {red[dominant].mean() if dominant.any() else np.nan:+.2f} dB, no direct {red[absent].mean() if absent.any() else np.nan:+.2f} dB, '
                  f'deepest {red.min():+.2f} dB, jitter median {np.nanmedian(jit):.2f} dB', flush=True)
    RESULTS['spectral'] = res; save()

# ---------------------------------------------------------------------------- 2c / 8. vocal: GR over time, pumping, tail in pauses
if 'vocal' in PARTS:
    import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
    v = vocal_phrases(load_vocal()); x = stereo(v)
    act = env_db(v) > env_db(v).max() - 30
    res = {}
    fig, axes = plt.subplots(3, 1, figsize=(11, 7.6), sharex=True)
    fig.patch.set_facecolor('#fcfcfb')
    colors = {'early': '#2a78d6', 'late': '#eb6834'}
    for decay in (6.0, 20.0):
        y0, _, _, _, _ = render(x, ATMOS + [f'decay={decay}'], block=64)
        for ax_i, (mode, name) in enumerate(((0, 'Classic'), (1, 'Vocal Focus'), (2, 'Spectral'))):
            y, m, _, _, _ = render(x, ATMOS + [f'decay={decay}', 'duckOn=1', f'duckMode={mode}', 'duckAmountDb=9'], block=64, meter=True)
            gE, gL = m[:, 0], m[:, 1]; tt = np.arange(len(gL)) * 64 / FS
            a = np.interp(tt, np.arange(len(act)) * 0.01, act.astype(float)) > 0.5
            inph = a & (tt < len(v) / FS - 4)
            e0, e1 = env_db(y0, 2400), env_db(y, 2400); te = np.arange(len(e0)) * 0.05
            ae = np.interp(te, np.arange(len(act)) * 0.01, act.astype(float)) > 0.5
            # pauses: the second half of each inserted gap (ducker released)
            pauses = ((te > 3.53 + 1.4) & (te < 3.53 + 2.5)) | ((te > 6.95 + 2.5 + 1.4) & (te < 6.95 + 5.0))
            r = dict(gr_late_median_in_phrases=float(np.median(gL[inph])), gr_late_std_in_phrases=float(np.std(gL[inph])),
                     gr_early_median_in_phrases=float(np.median(gE[inph])),
                     wet_change_in_phrases_db=float(np.median((e1 - e0)[ae])), wet_change_in_pauses_db=float(np.median((e1 - e0)[pauses])),
                     tail_after_release_identical=bool(np.array_equal(y[int((len(v) / FS - 2.5) * FS):], y0[int((len(v) / FS - 2.5) * FS):])))
            res[f'{decay:g}s {name}'] = r
            print('vocal', decay, name, {k: (round(q, 2) if isinstance(q, float) else q) for k, q in r.items()}, flush=True)
            if decay == 6.0:
                ax = axes[ax_i]
                ax.set_facecolor('#fcfcfb')
                ve = env_db(v, 480); tv = np.arange(len(ve)) * 0.01
                ax.fill_between(tv, -18, -18 + 9 * np.clip((ve - ve.max() + 40) / 40, 0, 1), color='#e3e2de', lw=0, label='vocal level (scaled)')
                ax.plot(tt, -gL, color=colors['late'], lw=1.6, label='late GR')
                ax.plot(tt, -gE, color=colors['early'], lw=1.6, label='early GR')
                ax.set_ylim(-10, 0.6); ax.set_ylabel('dB', color='#52514e')
                ax.set_title(f'{name}  (DUCK AMOUNT 9 dB, ATMOS 6 s)', loc='left', fontsize=10.5, color='#0b0b0b')
                ax.grid(axis='y', color='#e8e7e3', lw=0.8); [s.set_visible(False) for s in ax.spines.values()]
                ax.tick_params(colors='#52514e', labelsize=9)
                if ax_i == 0: ax.legend(loc='lower right', fontsize=8.5, frameon=False, ncol=3)
    axes[-1].set_xlabel('time, s', color='#52514e'); axes[-1].set_xlim(0, len(v) / FS)
    fig.suptitle('SMART DUCKER: gain reduction over time on a dry vocal (pauses of 2.5 s inserted)', x=0.01, ha='left', fontsize=12, color='#0b0b0b')
    fig.tight_layout(); fig.savefig(os.path.join(OUT, 'GR_over_time_vocal.png'), dpi=130); plt.close(fig)
    RESULTS['vocal'] = res; save()

# ---------------------------------------------------------------------------- ATMOS 1.6..30 s: modules on, tail motion and stereo kept
def ild_std(y):
    sos = signal.butter(3, [500, 4000], btype='band', fs=FS, output='sos'); b = signal.sosfilt(sos, y, axis=0)
    w = int(0.05 * FS); n = len(b) // w; L, R = b[:n * w, 0].reshape(n, w), b[:n * w, 1].reshape(n, w)
    eL, eR = (L ** 2).mean(1), (R ** 2).mean(1); ok = (eL + eR) > (eL + eR).max() * 1e-3
    return float(np.std(10 * np.log10(eL[ok] / eR[ok])))

def motion(y, t0, t1):
    # movement of the fine structure in the free tail: 1 - spectral correlation between frames 85 ms apart
    seg = y[int(t0 * FS):int(t1 * FS)].sum(1)
    f, t, Z = signal.stft(seg, FS, nperseg=4096, noverlap=3072); P = np.log(np.abs(Z[(f > 300) & (f < 6000)]) ** 2 + 1e-20)
    P = P - P.mean(1, keepdims=True)
    return float(1 - np.mean([np.corrcoef(P[:, i], P[:, i + 4])[0, 1] for i in range(P.shape[1] - 4)]))

if 'decays' in PARTS:
    v = vocal_phrases(load_vocal()); x = stereo(np.concatenate([v, np.zeros(int(8 * FS))]))
    tail0 = len(v) / FS - 4 + 0.3
    res = {}
    for d in (1.6, 3.0, 6.0, 12.0, 20.0, 30.0):
        outs = {}
        for name, extra in (('ATMOS', []), ('+DUCK', DUCK), ('+RG', RG), ('+both', DUCK + RG), ('+RG TAIL AWARE', ['rgOn=1', 'rgMode=2', 'rgMaxDb=3'])):
            y, m, _, _, _ = render(x, ATMOS + [f'decay={d}'] + extra, meter=True)
            outs[name] = (y, m)
        y0 = outs['ATMOS'][0]
        t1 = tail0 + min(3.0, max(0.8, d * 0.5))
        row = {}
        for name, (y, m) in outs.items():
            row[name] = dict(motion=motion(y, tail0, t1), ild_std=ild_std(y), rg_max_cut=float(m[:, 2].max()),
                             rg_time_cutting_pct=float(100 * np.mean(m[:, 2] > 0.05)),
                             third_oct_max_change_db=float(np.max(np.abs(band_levels(y, third_bands(100, 12000)) - band_levels(y0, third_bands(100, 12000))))),
                             tail_level_change_db=float(db(np.sum(y[int(tail0 * FS):] ** 2)) - db(np.sum(y0[int(tail0 * FS):] ** 2))))
        res[f'{d:g}'] = row
        print('decay', d, {k: {kk: round(vv, 3) for kk, vv in r.items()} for k, r in row.items()}, flush=True)
    RESULTS['decays'] = res; save()

# ---------------------------------------------------------------------------- 6/7. listening renders + DELTA, matched loudness
MATERIALS = None
def materials():
    rng = np.random.default_rng(7)
    v = vocal_phrases(load_vocal())
    return [('01_vocal_ATMOS6s', v, 6.0), ('02_vocal_ATMOS20s', v, 20.0), ('03_sustained_vowels', sustained_vowels(rng), 6.0),
            ('04_sax_synth', sax(rng), 12.0), ('05_acoustic_guitar_synth', guitar(rng), 3.0), ('06_piano_synth', piano(rng), 6.0),
            ('07_drums_synth', drums(rng), 1.6), ('08_impulses', impulses(rng), 6.0), ('09_broadband_pink_bursts', broadband(rng), 3.0)]

if 'materials' in PARTS:
    rdir = os.path.join(OUT, 'renders'); os.makedirs(rdir, exist_ok=True)
    res = {}
    for name, src, d in materials():
        x = stereo(src)
        wets = {}
        for vname, extra in (('A_ATMOS', []), ('B_ATMOS+DUCK', DUCK), ('C_ATMOS+RG', RG), ('D_ATMOS+DUCK+RG', DUCK + RG)):
            y, m, _, _, _ = render(x, ATMOS + [f'decay={d}'] + extra, meter=True)
            wets[vname] = (y, m)
        dl, _, _, _, _ = render(x, ATMOS + [f'decay={d}'] + RG + ['rgDelta=1', 'rgMatch=0'])
        yr, _, _, _, _ = render(x, ATMOS + [f'decay={d}'] + RG + ['rgMatch=0'])
        # mix as on a send: dry + reverb return; return level set once (wet 8 LU under the dry, ATMOS version)
        dry = x[:len(wets['A_ATMOS'][0])]
        lw = lufs(wets['A_ATMOS'][0]); ld = lufs(dry)
        gw = 10 ** ((ld - 8 - lw) / 20) if np.isfinite(lw) and lw > -100 and ld > -100 else 0.5
        mixes = {k: dry + gw * y for k, (y, _) in wets.items()}
        ref = lufs(mixes['A_ATMOS'])
        row = {'return_gain_db': float(20 * np.log10(gw))}
        for k, mx in mixes.items():
            l = lufs(mx); g = ref - l
            row[k] = {'match_gain_db': float(g), 'rg_max_cut_db': float(wets[k][1][:, 2].max()), 'duck_max_gr_db': float(wets[k][1][:, 1].max())}
            write_wav(os.path.join(rdir, f'{name}__{k}.wav'), mx * 10 ** (g / 20))
        drel = float(db(np.sum(dl ** 2)) - db(np.sum(wets['A_ATMOS'][0] ** 2)))
        row['delta_level_re_wet_db'] = drel
        row['delta_exact_err'] = float(np.max(np.abs(wets['A_ATMOS'][0] - yr - dl)))
        write_wav(os.path.join(rdir, f'{name}__E_DELTA_RG.wav'), gw * dl * 10 ** ((ref - lufs(mixes['C_ATMOS+RG'])) / 20))
        res[name] = row
        print('render', name, json.dumps(row, default=float)[:300], flush=True)
    # a resonant room (stone, small, low diffusion): RESONANCE GUARD DEEP has something to remove here
    HARDROOM = ['tailMode=0', 'algorithm=0', 'routing=1', 'mix=1', 'decay=2.5', 'size=0.25', 'diffusion=0.25', 'density=0.4',
                'mat0=4', 'mat1=4', 'mat2=4', 'mat3=4', 'mat4=4', 'mat5=4']
    RGD = ['rgOn=1', 'rgMode=1', 'rgMaxDb=6', 'rgTarget=0']
    for name, src, _ in [m for m in materials() if m[0] in ('01_vocal_ATMOS6s', '03_sustained_vowels', '09_broadband_pink_bursts')]:
        name = 'S' + name[1:].replace('_ATMOS6s', '') + '_STONE_ROOM'
        x = stereo(src)
        y0, _, _, _, _ = render(x, HARDROOM)
        y1, m1, _, _, _ = render(x, HARDROOM + RGD, meter=True)
        dl, _, _, _, _ = render(x, HARDROOM + RGD + ['rgDelta=1', 'rgMatch=0'])
        dry = x[:len(y0)]; gw = 10 ** ((lufs(dry) - 8 - lufs(y0)) / 20)
        m0, m1x = dry + gw * y0, dry + gw * y1
        g = lufs(m0) - lufs(m1x)
        write_wav(os.path.join(rdir, f'{name}__A_room.wav'), m0)
        write_wav(os.path.join(rdir, f'{name}__C_room+RG_DEEP.wav'), m1x * 10 ** (g / 20))
        write_wav(os.path.join(rdir, f'{name}__E_DELTA_RG_DEEP.wav'), gw * dl)
        res[name] = {'rg_max_cut_db': float(m1[:, 2].max()), 'rg_time_cutting_pct': float(100 * np.mean(m1[:, 2] > 0.05)), 'match_gain_db': float(g),
                     'delta_level_re_wet_db': float(db(np.sum(dl ** 2)) - db(np.sum(y0 ** 2)))}
        print('render', name, res[name], flush=True)
    RESULTS['materials'] = res; save()

# ---------------------------------------------------------------------------- 9. RG spectral suppression map
if 'map' in PARTS:
    import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
    rng = np.random.default_rng(9)
    v = vocal_phrases(load_vocal())
    # a hard, resonant room (stone, small, low diffusion) - where the guard has something to do - and ATMOS for contrast
    hard = ['tailMode=0', 'algorithm=0', 'routing=1', 'mix=1', 'decay=2.5', 'size=0.25', 'diffusion=0.25', 'density=0.4',
            'mat0=4', 'mat1=4', 'mat2=4', 'mat3=4', 'mat4=4', 'mat5=4']
    cases = [('Stone room 2.5 s, DEEP', hard + ['rgOn=1', 'rgMode=1', 'rgMaxDb=6', 'rgTarget=0']),
             ('Stone room 2.5 s, SMOOTH', hard + ['rgOn=1', 'rgMode=0', 'rgMaxDb=6', 'rgTarget=0']),
             ('Stone room 2.5 s, TAIL AWARE', hard + ['rgOn=1', 'rgMode=2', 'rgMaxDb=6', 'rgTarget=0']),
             ('ATMOS 6 s, SMOOTH', ATMOS + ['decay=6', 'rgOn=1', 'rgMode=0', 'rgMaxDb=6', 'rgTarget=0'])]
    fig, axes = plt.subplots(len(cases), 1, figsize=(11, 2.6 * len(cases)), sharex=True)
    fig.patch.set_facecolor('#fcfcfb')
    res = {}
    for ax, (name, args) in zip(axes, cases):
        y, m, c, hz, _ = render(stereo(v), args, block=256, meter=True, cutmap=True)
        ref, _, _, _, _ = render(stereo(v), [a for a in args if not a.startswith('rgOn')])
        dl, _, _, _, _ = render(stereo(v), args + ['rgDelta=1', 'rgMatch=0'])
        tt = np.arange(c.shape[0]) * 256 / FS
        im = ax.pcolormesh(tt, hz, c.T, cmap='Blues', vmin=0, vmax=6, shading='auto')
        ax.set_yscale('log'); ax.set_ylim(80, 16000)
        ax.set_title(name, loc='left', fontsize=10.5, color='#0b0b0b'); ax.set_ylabel('Hz', color='#52514e')
        ax.tick_params(colors='#52514e', labelsize=9)
        top = np.argsort(c.max(0))[::-1][:5]
        res[name] = dict(max_cut_db=float(c.max()), mean_cut_when_active_db=float(c[c > 0.05].mean()) if (c > 0.05).any() else 0.0,
                         time_cutting_pct=float(100 * np.mean(m[:, 2] > 0.05)), top_bands_hz=[float(hz[i]) for i in top],
                         delta_level_re_wet_db=float(db(np.sum(dl ** 2)) - db(np.sum(ref ** 2))),
                         third_oct_max_change_db=float(np.max(np.abs(band_levels(y, third_bands(100, 12000)) - band_levels(ref, third_bands(100, 12000))))))
        print('map', name, res[name], flush=True)
    axes[-1].set_xlabel('time, s', color='#52514e')
    cb = fig.colorbar(im, ax=axes, shrink=0.6, pad=0.01); cb.set_label('reduction, dB', color='#52514e')
    fig.suptitle('RESONANCE GUARD: spectral suppression map (LIVE, MAX 6 dB, dry vocal through Aux)', x=0.01, ha='left', fontsize=12, color='#0b0b0b')
    fig.savefig(os.path.join(OUT, 'RG_suppression_map.png'), dpi=130, bbox_inches='tight'); plt.close(fig)
    RESULTS['map'] = res; save()

# ---------------------------------------------------------------------------- mono compatibility
if 'mono' in PARTS:
    res = {}
    for name, src, d in materials():
        x = stereo(src); row = {}
        for vname, extra in (('ATMOS', []), ('+DUCK', DUCK), ('+RG', RG), ('+both', DUCK + RG), ('+RG HQ', RG + ['quality=1'])):
            y, _, _, _, _ = render(x, ATMOS + [f'decay={d}'] + extra)
            ms = np.mean(((y[:, 0] + y[:, 1]) * 0.5) ** 2); st = np.mean((y[:, 0] ** 2 + y[:, 1] ** 2) * 0.5)
            corr = float(np.sum(y[:, 0] * y[:, 1]) / np.sqrt(np.sum(y[:, 0] ** 2) * np.sum(y[:, 1] ** 2)))
            row[vname] = dict(mono_minus_stereo_db=float(db(ms) - db(st)), lr_correlation=corr)
        res[name] = row
        print('mono', name, {k: (round(r['mono_minus_stereo_db'], 2), round(r['lr_correlation'], 3)) for k, r in row.items()}, flush=True)
    RESULTS['mono'] = res; save()

# ---------------------------------------------------------------------------- 10. automation (engine level): sweeps and toggles
if 'automation' in PARTS:
    v = vocal_phrases(load_vocal()); x = stereo(v); n = len(v)
    def clickiness(y):
        d2 = y[2:] - 2 * y[1:-1] + y[:-2]
        return float(np.max(np.abs(d2)))
    base = ATMOS + ['decay=6']                 # wet only (Aux): the dry sound would hide a click in the reverb
    HARDROOM = ['tailMode=0', 'algorithm=0', 'routing=1', 'mix=1', 'decay=2.5', 'size=0.25', 'diffusion=0.25', 'density=0.4',
                'mat0=4', 'mat1=4', 'mat2=4', 'mat3=4', 'mat4=4', 'mat5=4']
    y0, _, _, _, _ = render(x, base + DUCK + RG, block=64)
    yh, _, _, _, _ = render(x, HARDROOM + ['rgOn=1', 'rgMode=1', 'rgMaxDb=6', 'rgTarget=0'], block=64)
    tests = {
        'DUCK AMOUNT 0..18 dB sweep (every 10 ms)': [f'@{i}:duckAmountDb={18 * (0.5 + 0.5 * np.sin(i / FS * 2)):.3f}' for i in range(480, n, 480)],
        'DUCK on/off every 250 ms': [f'@{i}:duckOn={(i // 12000) % 2}' for i in range(12000, n, 12000)],
        'DUCK mode cycle every 500 ms': [f'@{i}:duckMode={(i // 24000) % 3}' for i in range(24000, n, 24000)],
        'RG MAX 0..12 dB sweep (every 10 ms)': [f'@{i}:rgMaxDb={12 * (0.5 + 0.5 * np.sin(i / FS * 3)):.3f}' for i in range(480, n, 480)],
        'RG on/off every 250 ms': [f'@{i}:rgOn={(i // 12000) % 2}' for i in range(12000, n, 12000)],
        'RG mode cycle every 500 ms': [f'@{i}:rgMode={(i // 24000) % 3}' for i in range(24000, n, 24000)],
        'RG LOW/HIGH/SMOOTH/DETAIL random every 20 ms': [f'@{i}:{k}={val}' for i in range(960, n, 960)
                                                        for k, val in [(['rgLoHz', 'rgHiHz', 'rgSmooth', 'rgDetail'][(i // 960) % 4],
                                                                        [150 + (i % 7) * 120, 3000 + (i % 11) * 1500, (i % 13) / 13, (i % 17) / 17][(i // 960) % 4])]],
        'DELTA on/off every 500 ms': [f'@{i}:rgDelta={(i // 24000) % 2}' for i in range(24000, n, 24000)],
        'quality LIVE/HQ every 1 s (engine fade)': [f'@{i}:quality={(i // 48000) % 2}' for i in range(48000, n, 48000)],
    }
    res = {'reference_2nd_diff_peak ATMOS': clickiness(y0), 'reference_2nd_diff_peak stone room DEEP': clickiness(yh)}
    for name, ch in tests.items():
        rgTest = name.startswith('RG') or name.startswith('DELTA')
        y, _, _, _, _ = render(x, (HARDROOM + ['rgOn=1', 'rgMode=1', 'rgMaxDb=6', 'rgTarget=0'] if rgTest else base + DUCK + RG) + ch, block=64)
        ref = clickiness(yh if rgTest else y0)
        res[name] = dict(scene='stone room, RG DEEP' if rgTest else 'ATMOS 6 s, DUCK + RG', finite=bool(np.all(np.isfinite(y))),
                         second_diff_peak=clickiness(y), reference=ref, peak=float(np.max(np.abs(y))))
        print('automation', name, res[name], flush=True)
    RESULTS['automation'] = res; save()
print('done')
