# MASTER SPACE 1.4 — HARMONIC COLOR measurements.
#   g++ -O2 -std=c++17 -o /tmp/hcd tests/hc_driver.cpp                                  (the module alone)
#   g++ -O2 -std=c++17 -DMAS_ATMOS -DMAS_RI -o /tmp/r14 tests/render_space.cpp          (the whole engine)
#   python3 tests/ri/measure_hc.py --driver /tmp/hcd --render /tmp/r14 --vocal <dry vocal .wav> --out <dir> [parts]
# parts: latency spectra alias level lowmid decays cpu renders charts (default: all)
import argparse, json, os, re, subprocess, tempfile
import numpy as np
from scipy import signal

ap = argparse.ArgumentParser()
ap.add_argument('--driver', required=True)
ap.add_argument('--render', required=True)
ap.add_argument('--vocal', required=True)
ap.add_argument('--out', required=True)
ap.add_argument('parts', nargs='*')
A = ap.parse_args()
PARTS = set(A.parts) or {'latency', 'spectra', 'alias', 'level', 'lowmid', 'decays', 'cpu', 'renders', 'charts'}
OUT = A.out
os.makedirs(OUT, exist_ok=True)
TMP = tempfile.mkdtemp()
FS = 48000
CH = ['SOFT', 'TUBE', 'TAPE']
R = {}
RP = os.path.join(OUT, 'hc_results.json')
if os.path.exists(RP): R = json.load(open(RP))
def save(): json.dump(R, open(RP, 'w'), indent=1, default=float)
def db(x): return 10 * np.log10(np.maximum(x, 1e-30))
def stereo(m): return np.stack([m, m], 1)

def hc(x, **kw):
    """the module alone; x (n, 2) -> y, stderr"""
    i, o = os.path.join(TMP, 'hi.f32'), os.path.join(TMP, 'ho.f32')
    np.asarray(x, '<f4').tofile(i)
    fs = kw.pop('fs', FS)
    p = subprocess.run([A.driver, i, o, str(fs)] + [f'{k}={v}' for k, v in kw.items()], capture_output=True, text=True, check=True)
    return np.fromfile(o, '<f4').reshape(-1, 2).astype(np.float64), p.stdout + p.stderr

def eng(x, args, fs=FS, block=256):
    i, o = os.path.join(TMP, 'ei.f32'), os.path.join(TMP, 'eo.f32')
    np.asarray(x, '<f4').tofile(i)
    p = subprocess.run([A.render, i, o, str(fs), str(block)] + list(args), capture_output=True, text=True, check=True)
    return np.fromfile(o, '<f4').reshape(-1, 2).astype(np.float64), p.stderr

def HC(char, drive, pos=0, comp=1, q=0, color=0.0, mix=1.0):
    return ['hcOn=1', f'hcChar={char}', f'hcDrive={drive}', f'hcPos={pos}', f'hcLevelComp={comp}', f'quality={q}',
            f'hcColor={color}', f'hcMix={mix}']

# ATMOS base (factory "ATMOS Vocal Glow", Aux 100 % wet; decay set per test) — same as measure_14.py
ATMOS = ['tailMode=1', 'routing=1', 'mix=1', 'erOn=1', 'erLevelDb=-12', 'lateLevelDb=0', 'diffusion=0.7', 'density=0.7',
         'shape=0.35', 'spreadMs=60', 'lateDelayMs=12', 'lfMult=0.9', 'lmMult=1', 'hmMult=1', 'hfMult=1', 'xLow=250',
         'xMid=2000', 'xHigh=7000', 'lowCutHz=90', 'crossfeed=0.15', 'decorrelation=0.85', 'bloom=0.25', 'motion=0.5', 'air=0.55']

def kweight(x, fs=FS):
    f0, G, Q = 1681.97, 3.99984, 0.7071752
    K = np.tan(np.pi * f0 / fs); Vh = 10 ** (G / 20); Vb = Vh ** 0.4996667741545416
    a0 = 1 + K / Q + K * K
    b = [(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0]
    a = [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0]
    return signal.sosfilt(signal.butter(2, 38 / (fs / 2), btype='highpass', output='sos'), signal.lfilter(b, a, x, axis=0), axis=0)

def lufs(x, fs=FS):
    y = kweight(x, fs); w = int(0.4 * fs); hop = int(0.1 * fs)
    ms = np.array([np.mean(np.sum(y[i:i + w] ** 2, 1)) for i in range(0, len(y) - w, hop)])
    l = -0.691 + 10 * np.log10(ms + 1e-20); g = ms[l > -70]
    if len(g) == 0: return -120.0
    rel = -0.691 + 10 * np.log10(g.mean()) - 10
    return -0.691 + 10 * np.log10(ms[l > max(-70, rel)].mean())

def write_wav(path, y, fs=FS):
    import soundfile as sf
    pk = np.max(np.abs(y))
    if pk > 0.999: y = y * (0.999 / pk)
    sf.write(path, y.astype(np.float32), fs, subtype='PCM_24')

def third_bands(lo=63, hi=16000):
    c = 1000 * 2 ** (np.arange(-24, 15) / 3); return c[(c >= lo) & (c <= hi)]

def band_levels(x, centers, fs=FS):
    f, P = signal.welch(x, fs, nperseg=8192, axis=0)
    if P.ndim > 1: P = P.sum(1)
    return np.array([db(P[(f >= c * 2 ** -(1 / 6)) & (f < c * 2 ** (1 / 6))].sum()) for c in centers])

def pink(n, rng):
    return signal.lfilter([0.049922035, -0.095993537, 0.050612699, -0.004408786], [1, -2.494956002, 2.017265875, -0.522189400],
                          rng.standard_normal(n))

def sine(f, amp, secs, fs=FS):
    t = np.arange(int(secs * fs)) / fs; return stereo(amp * np.sin(2 * np.pi * f * t))

def spectrum(y, fs=FS):
    """last second, Blackman-Harris, amplitude spectrum (dB re full-scale sine), 1 Hz bins"""
    seg = y[-fs:, 0]; w = signal.windows.blackmanharris(len(seg))
    S = np.abs(np.fft.rfft(seg * w)) / (w.sum() / 2)
    return np.fft.rfftfreq(len(seg), 1 / fs), 20 * np.log10(S + 1e-12)

def harmonics(y, f0, kmax=9, fs=FS):
    f, S = spectrum(y, fs)
    lv = []
    for k in range(1, kmax + 1):
        if k * f0 >= fs / 2: lv.append(-200.0); continue
        i = int(round(k * f0)); lv.append(float(S[max(0, i - 2):i + 3].max()))
    lv = np.array(lv)
    p = 10 ** (lv / 10)
    thd = 10 * np.log10(p[1:].sum() / p[0])
    even = 10 * np.log10(p[1::2].sum() / p[0] + 1e-30); odd = 10 * np.log10(p[2::2].sum() / p[0] + 1e-30)
    return lv - lv[0], thd, even, odd

# ============================================================================ latency
if 'latency' in PARTS:
    res = {}
    # the module alone at a linear level: transfer function from stationary noise (H1 estimate) -> group delay, magnitude;
    # an impulse for where the peak comes out
    rng0 = np.random.default_rng(2)
    xn = stereo(1e-3 * rng0.standard_normal(6 * FS))
    for q, qn in ((0, 'LIVE'), (1, 'HQ')):
        y, _ = hc(xn, on=1, char=0, drive=0.0, quality=q, comp=0)
        cc = signal.correlate(y[FS:, 0], xn[FS:, 0], 'full', method='fft'); bulk = int(np.argmax(cc)) - (len(xn) - FS - 1)
        xa, ya = xn[FS:len(xn) - 256, 0], y[FS + bulk:len(xn) - 256 + bulk, 0]      # bulk delay removed, then the residual
        f, Pxy = signal.csd(xa, ya, FS, nperseg=16384)
        _, Pxx = signal.welch(xa, FS, nperseg=16384)
        H = Pxy / Pxx
        ph = np.unwrap(np.angle(H))
        gd = bulk - np.gradient(ph, 2 * np.pi * f / FS)
        gd = np.convolve(gd, np.ones(9) / 9, 'same')
        mag = 20 * np.log10(np.abs(H) + 1e-12)
        sel = (f > 20) & (f < 20000)
        xi = np.zeros((FS, 2)); xi[FS // 2] = 1e-3
        yi, _ = hc(xi, on=1, char=0, drive=0.0, quality=q)
        res[qn] = {'reported': 0 if q == 0 else 71, 'peak_sample': int(np.argmax(np.abs(yi[:, 0]))) - FS // 2,
                   'gd_samples': {str(fr): float(np.interp(fr, f, gd)) for fr in (100, 1000, 5000, 10000, 15000, 18000, 20000)},
                   'mag_dev_db_20_20k': [float(mag[sel].min()), float(mag[sel].max())]}
        R.setdefault('_gd_curve', {})['f'] = f[sel].tolist()
        R['_gd_curve'][qn] = gd[sel].tolist()
    # the module driven (broadband, DRIVE 60 %): lag of the coloured output against the input
    rng = np.random.default_rng(1)
    x = stereo(0.2 * pink(4 * FS, rng) / 3)
    for q, qn in ((0, 'LIVE'), (1, 'HQ')):
        y, _ = hc(x, on=1, char=1, drive=0.6, quality=q)
        c = signal.correlate(y[FS:, 0], x[FS:, 0], 'full', method='fft'); lag = int(np.argmax(c)) - (len(x) - FS - 1)
        res[qn]['driven_lag_samples'] = lag
    # the plugin engine: reported latency and where the direct sound actually comes out (Insert, MIX 0)
    eng_res = {}
    for fs in (44100, 48000, 88200, 96000):
        x = np.zeros((int(1.5 * fs), 2)); x[100] = 0.5
        row = {}
        for q in (0, 1):
            for pos in (0, 1):
                y, err = eng(x, ATMOS + ['routing=0', 'mix=0'] + HC(1, 0.6, pos=pos, q=q), fs=fs)
                lat = int(re.search(r'latency (\d+)', err).group(1))
                row[f'{"LIVE" if q == 0 else "HQ"}_{"POST" if pos == 0 else "PRE"}'] = {'reported': lat, 'direct_at': int(np.argmax(np.abs(y[:, 0]))) - 100}
        eng_res[str(fs)] = row
    res['engine'] = eng_res
    R['latency'] = res; save()
    print('latency', json.dumps(res, indent=1)[:1500])

# ============================================================================ distortion spectra
if 'spectra' in PARTS:
    res = {}
    for q, qn in ((0, 'LIVE'), (1, 'HQ')):
        for ci, cn in enumerate(CH):
            for d in (0.25, 0.5, 0.75, 1.0):
                y, _ = hc(sine(500, 0.25, 2.0), on=1, char=ci, drive=d, quality=q)
                lv, thd, ev, od = harmonics(y, 500)
                res[f'{qn}_{cn}_{d}'] = {'H': lv[1:].tolist(), 'thd': thd, 'even': ev, 'odd': od}
                if d == 0.5 and q == 0:
                    f, S = spectrum(y); R.setdefault('_spec', {})[cn] = S[:8001:2].tolist()
    # TUBE: even / odd balance over drive (the character changes with the drive)
    R['spectra'] = res; save()
    for k, v in res.items():
        if k.startswith('LIVE'): print(k, 'THD %.1f even %.1f odd %.1f' % (v['thd'], v['even'], v['odd']), ' '.join('%.0f' % h for h in v['H'][:6]))

# ============================================================================ aliasing
if 'alias' in PARTS:
    res = {}
    def nonharm(y, f0, fmax=20000, fs=FS):
        """largest product that is not a harmonic of f0, 30 Hz..fmax, dB re the fundamental"""
        f, S = spectrum(y, fs)
        mask = (f > 30) & (f < fmax)
        for k in range(1, int(fs / 2 / f0) + 1): mask &= np.abs(f - k * f0) > 4
        S1 = S[int(round(f0)) - 2:int(round(f0)) + 3].max()
        return float(S[mask].max() - S1), f, S - S1
    for f0 in (3000, 7000, 10000, 13000):
        for lv in (-12, -24):
            for d in (0.5, 0.8):
                g = 10 ** ((-12 + 36 * d) / 20)
                xs = sine(f0, 10 ** (lv / 20), 2.0)
                v = xs * g; naive = (v / np.sqrt(1 + v * v)) / g            # SOFT's curve at 1x: what oversampling avoids
                res[f'{f0}_{lv}_{d}_naive_SOFT'] = nonharm(naive, f0)[0]
                for q, qn in ((0, 'LIVE'), (1, 'HQ')):
                    for ci, cn in enumerate(CH):
                        y, _ = hc(xs, on=1, char=ci, drive=d, quality=q)
                        a, f, S = nonharm(y, f0)
                        res[f'{f0}_{lv}_{d}_{qn}_{cn}'] = a
                        res[f'{f0}_{lv}_{d}_{qn}_{cn}_to24k'] = nonharm(y, f0, 23900)[0]
                        if f0 == 13000 and lv == -12 and d == 0.8 and ci == 0:
                            R.setdefault('_alias', {})[qn] = S[::4].tolist(); R['_alias']['f'] = f[::4].tolist()
                if f0 == 13000 and lv == -12 and d == 0.8: R['_alias']['naive'] = nonharm(naive, f0)[2][::4].tolist()
    R['alias'] = res; save()
    for f0 in (3000, 7000, 10000, 13000):
        for lv in (-12, -24):
            for d in (0.5, 0.8):
                print('alias %5d Hz %d dBFS drive %.1f  naive %.0f |' % (f0, lv, d, res[f'{f0}_{lv}_{d}_naive_SOFT']),
                      ' '.join('%s %s %.0f' % (qn, cn, res[f'{f0}_{lv}_{d}_{qn}_{cn}']) for qn in ('LIVE', 'HQ') for cn in CH))

# ============================================================================ THD vs level (transparency of a decaying tail)
if 'level' in PARTS:
    res = {}
    for ci, cn in enumerate(CH):
        for lv in range(-60, 1, 6):
            y, _ = hc(sine(500, 10 ** (lv / 20), 2.0), on=1, char=ci, drive=0.5)
            res[f'{cn}_{lv}'] = harmonics(y, 500)[1]
    R['level'] = res; save()
    for cn in CH: print('THD', cn, ' '.join('%d:%.0f' % (lv, res[f'{cn}_{lv}']) for lv in range(-60, 1, 6)))

# ============================================================================ 200-500 Hz on reverb material
def bursts(rng, secs=12.0, amp=0.5):
    x = np.zeros(int(secs * FS))
    for t in np.arange(0.2, secs - 3, 1.5):
        n = int(0.25 * FS); i = int(t * FS)
        x[i:i + n] = pink(n, rng) * signal.windows.tukey(n, 0.2) * amp
    return stereo(x)

def load_vocal(target=-23.0):
    import soundfile as sf
    v, fs = sf.read(A.vocal, always_2d=True); v = v.mean(1)
    if fs != FS: v = signal.resample_poly(v, FS, fs)
    v = v / (10 ** ((lufs(stereo(v)) - target) / 20))
    return stereo(np.concatenate([v, np.zeros(4 * FS)]))

if 'lowmid' in PARTS:
    res = {}
    rng = np.random.default_rng(7)
    mats = {'pink_bursts': bursts(rng), 'vocal': load_vocal(-16.0)}      # hot material: the stress case for the low mids
    c = third_bands(63, 16000)
    R['_bands'] = c.tolist()
    for mn, x in mats.items():
        for dec in (3.0, 6.0):
            base = ATMOS + [f'decay={dec}']
            off, _ = eng(x, base)
            Lo = band_levels(off, c); lo_lufs = lufs(off)
            for pos in (0, 1):
                for comp in (1, 0):
                    for ci, cn in enumerate(CH):
                        for d in (0.5, 0.75, 1.0):
                            if pos == 1 and (d != 0.75 or comp == 0): continue
                            y, _ = eng(x, base + HC(ci, d, pos=pos, comp=comp))
                            Ly = band_levels(y, c) - Lo
                            sel = (c >= 200) & (c <= 500)
                            key = f'{mn}_{dec}_{"POST" if pos == 0 else "PRE"}_{"comp" if comp else "raw"}_{cn}_{d}'
                            Lyy = Ly + Lo
                            res[key] = {'lowmid_energy': float(db(np.sum(10 ** (Lyy[sel] / 10))) - db(np.sum(10 ** (Lo[sel] / 10)))),
                                        'lowmid_mean': float(Ly[sel].mean()), 'lowmid_max': float(Ly[sel].max()), 'lowmid_min': float(Ly[sel].min()),
                                        'curve': Ly.tolist(), 'lufs': float(lufs(y) - lo_lufs),
                                        'lowmid_vs_total': float(Ly[sel].mean() - (lufs(y) - lo_lufs))}
                            print(key, 'LUFS %+.2f  200-500 energy %+.2f, per 1/3 oct %+.2f..%+.2f' % (res[key]['lufs'], res[key]['lowmid_energy'], res[key]['lowmid_min'], res[key]['lowmid_max']))
    R['lowmid'] = res; save()

# ============================================================================ ATMOSPHERIC TAIL decays 1..30 s
def edc_db(y):
    e = (y ** 2).sum(1); s = np.cumsum(e[::-1])[::-1]; return db(s / s[0])

def t_from_edc(ed, a, b, fs=FS):
    i0, i1 = np.argmax(ed <= a), np.argmax(ed <= b)
    if i1 <= i0: return float('nan')
    t = np.arange(i0, i1) / fs; p = np.polyfit(t, ed[i0:i1], 1); return float(-60 / p[0])

def frames_db(y, w=480):
    e = (y[: len(y) // w * w] ** 2).sum(1).reshape(-1, w).mean(1); return db(e)

def peakiness(y, ed, fs=FS):
    """tail between EDC -10 and -40 dB: largest narrow spectral peak above a 1/3-octave smoothed spectrum (dB)"""
    i0, i1 = np.argmax(ed <= -10), np.argmax(ed <= -40)
    seg = y[i0:i1]
    if len(seg) < 16384: seg = y[i0:i0 + 16384]
    f, P = signal.welch(seg, fs, nperseg=16384, axis=0); P = db(P.sum(1))
    lf = np.log2(np.maximum(f, 1)); sm = np.empty_like(P)
    for k in range(len(f)):
        sel = np.abs(lf - lf[k]) < 1 / 6; sm[k] = db(np.mean(10 ** (P[sel] / 10)))
    band = (f > 150) & (f < 10000)
    return float((P - sm)[band].max())

def hf_ratio(y, a, b, fs=FS):
    seg = y[a:b]
    if len(seg) < 4096: return float('nan')
    f, P = signal.welch(seg, fs, nperseg=4096, axis=0); P = P.sum(1)
    return float(db(P[(f > 12000) & (f < 20000)].sum()) - db(P[(f > 1000) & (f < 4000)].sum()))

if 'decays' in PARTS:
    res = {}
    rng = np.random.default_rng(3)
    n = int(0.2 * FS); burst = pink(n, rng) * signal.windows.tukey(n, 0.3)
    burst = burst / np.sqrt(np.mean(burst ** 2)) * 10 ** (-18 / 20)           # -18 dBFS RMS
    variants = [('OFF', []), ('SOFT', HC(0, 0.75)), ('TUBE', HC(1, 0.75)), ('TAPE', HC(2, 0.75)), ('TUBE PRE', HC(1, 0.75, pos=1)),
                ('TAPE 100%', HC(2, 1.0))]
    hot = 'hot' in PARTS                                                    # stress: burst at -6 dBFS RMS, DRIVE 100 %
    if hot:
        burst = burst * 10 ** (12 / 20)
        variants = [('OFF', []), ('SOFT', HC(0, 1.0)), ('TUBE', HC(1, 1.0)), ('TAPE', HC(2, 1.0))]
    for dec in ((3.0, 12.0, 30.0) if hot else (1.0, 1.6, 3.0, 6.0, 12.0, 20.0, 30.0)):
        L = int((min(dec, 30) * 1.6 + 1.5) * FS)
        x = np.zeros((L, 2)); x[int(0.1 * FS):int(0.1 * FS) + n] = stereo(burst)
        st = int(0.3 * FS)
        rows = {}
        for vn, args in variants:
            y, _ = eng(x, ATMOS + [f'decay={dec}'] + args)
            yy = y[st:]
            ed = edc_db(yy)
            fr = frames_db(yy); valid = fr > fr.max() - 70
            last = np.max(np.nonzero(valid)[0]) if valid.any() else len(fr)
            fr = fr[:last]
            sm = signal.medfilt(fr, 21)
            jumps = np.abs(fr - sm)[10:-10]
            d1 = np.abs(np.diff(sm))[10:-10]
            i40 = np.argmax(ed <= -40); i60 = np.argmax(ed <= -60) or len(yy)
            M = 0.5 * (yy[:, 0] + yy[:, 1]); S = 0.5 * (yy[:, 0] - yy[:, 1])
            seg = slice(np.argmax(ed <= -5), np.argmax(ed <= -35))
            rows[vn] = {'T30': t_from_edc(ed, -5, -35), 'EDT': t_from_edc(ed, 0, -10),
                        'jump_p999_db': float(np.percentile(jumps, 99.9)) if len(jumps) else 0.0,
                        'step_max_db': float(d1.max()) if len(d1) else 0.0,
                        'peakiness_db': peakiness(yy, ed), 'hf_early': hf_ratio(yy, 0, int(0.5 * FS)),
                                                'side_mid_db': float(db(np.sum(S[seg] ** 2)) - db(np.sum(M[seg] ** 2))),
                        'corr': float(np.corrcoef(yy[seg, 0], yy[seg, 1])[0, 1]),
                        'lufs': float(lufs(yy)), '_edc': ed[::480].tolist(), '_y': None}
            # transparency through the decay: per-octave level vs OFF in 2 s windows, wherever OFF is above -100 dBFS
            # (below that only arithmetic noise is left); and HF grit: 12-20 kHz vs OFF in the first second
            if vn == 'OFF':
                off_y = yy
            else:
                worst, worst_at = 0.0, ''
                for j, c0 in enumerate([125, 250, 500, 1000, 2000, 4000, 8000, 16000]):
                    sos = signal.butter(4, [c0 / np.sqrt(2) / (FS / 2), min(c0 * np.sqrt(2), 23000) / (FS / 2)], 'band', output='sos')
                    fo, fy = signal.sosfilt(sos, off_y[:, 0]), signal.sosfilt(sos, yy[:, 0])
                    for w0 in range(0, len(yy) - 2 * FS, 2 * FS):
                        lo_, ly_ = db(np.mean(fo[w0:w0 + 2 * FS] ** 2)), db(np.mean(fy[w0:w0 + 2 * FS] ** 2))
                        if lo_ > -100 and abs(ly_ - lo_) > abs(worst): worst, worst_at = ly_ - lo_, f'{c0} Hz at {w0 / FS:.0f} s ({lo_:.0f} dBFS)'
                rows[vn]['oct_diff_max'] = float(worst); rows[vn]['oct_diff_where'] = worst_at
                sos = signal.butter(4, [12000 / (FS / 2), 20000 / (FS / 2)], 'band', output='sos')
                rows[vn]['hf_grit_db'] = float(db(np.mean(signal.sosfilt(sos, yy[:FS, 0]) ** 2)) - db(np.mean(signal.sosfilt(sos, off_y[:FS, 0]) ** 2)))
            del rows[vn]['_y']
        res[str(dec)] = rows
        print('decay', dec, ' | '.join('%s T30 %.2f EDT %.2f jump %.2f step %.2f peak %.1f S/M %.1f corr %.3f lufs %.1f oct %s grit %s' % (
            k, v['T30'], v['EDT'], v['jump_p999_db'], v['step_max_db'], v['peakiness_db'], v['side_mid_db'], v['corr'], v['lufs'],
            '%+.2f (%s)' % (v['oct_diff_max'], v['oct_diff_where']) if 'oct_diff_max' in v else '-',
            '%+.2f' % v['hf_grit_db'] if 'hf_grit_db' in v else '-') for k, v in rows.items()))
    R['decays_hot' if hot else 'decays'] = res; save()

# ============================================================================ CPU
if 'cpu' in PARTS:
    res = {}
    rng = np.random.default_rng(5)
    for fs in (48000, 96000):
        x = stereo(0.1 * pink(10 * fs, rng) / 3)
        for q, qn in ((0, 'LIVE'), (1, 'HQ')):
            for ci, cn in enumerate(CH):
                best = 1e9
                for _ in range(3):
                    _, out = hc(x, fs=fs, on=1, char=ci, drive=0.6, quality=q)
                    best = min(best, float(re.search(r'cpu ([\d.]+)', out).group(1)))
                res[f'module_{fs}_{qn}_{cn}'] = best
        xe = stereo(0.1 * pink(8 * fs, rng) / 3)
        for name, args in (('engine off', []), ('engine SOFT', HC(0, 0.6)), ('engine TUBE', HC(1, 0.6)), ('engine TAPE', HC(2, 0.6)),
                           ('engine TAPE HQ', HC(2, 0.6, q=1)), ('engine TUBE PRE', HC(1, 0.6, pos=1))):
            best = 0
            for _ in range(3):
                _, err = eng(xe, ATMOS + ['decay=6'] + args, fs=fs)
                best = max(best, float(re.search(r'rt ([\d.]+)x', err).group(1)))
            res[f'{name}_{fs}'] = 100.0 / best
    R['cpu'] = res; save()
    for k, v in res.items(): print('cpu', k, '%.2f %%' % v)

# ============================================================================ renders (loudness-matched A/B)
if 'renders' in PARTS:
    rd = os.path.join(OUT, 'renders_hc'); os.makedirs(rd, exist_ok=True)
    vox = load_vocal(-18.0)
    write_wav(os.path.join(rd, '00_VOX_dry_-18LUFS.wav'), vox)
    V = [('A_OFF_(=ATMOS_1.3)', []), ('B_SOFT_65', HC(0, 0.65)), ('C_TUBE_65', HC(1, 0.65)), ('D_TAPE_65', HC(2, 0.65)),
         ('E_TUBE_65_PRE', HC(1, 0.65, pos=1)), ('F_TAPE_90_warm', HC(2, 0.9, color=-0.4)), ('G_SOFT_85_bright', HC(0, 0.85, color=0.4))]
    for dec, tag in ((3.2, '01_vocal_ATMOS_3.2s_wet'), (6.0, '02_vocal_ATMOS_6s_wet')):
        for vn, args in V:
            y, _ = eng(vox, ATMOS + [f'decay={dec}'] + args)
            y = y * 10 ** ((-23 - lufs(y)) / 20)                  # the wet at -23 LUFS: compare colour, not level
            write_wav(os.path.join(rd, f'{tag}__{vn}.wav'), y)
    for vn, args in V[:4] + V[5:6]:                              # in context: Insert 30 %
        y, _ = eng(vox, ATMOS + ['decay=3.2', 'routing=0', 'mix=0.3'] + args)
        y = y * 10 ** ((-20 - lufs(y)) / 20)
        write_wav(os.path.join(rd, f'03_vocal_INSERT_30pct__{vn}.wav'), y)
    rng = np.random.default_rng(11)
    n = int(0.2 * FS); b = pink(n, rng) * signal.windows.tukey(n, 0.3); b = b / np.sqrt(np.mean(b ** 2)) * 10 ** (-18 / 20)
    b = b * 10 ** (6 / 20)                                       # -12 dBFS RMS burst
    for dec in (12.0, 30.0):
        x = np.zeros((int((dec * 1.3 + 2) * FS), 2)); x[int(0.2 * FS):int(0.2 * FS) + n] = stereo(b)
        for vn, args in (V[0], V[1], V[2], V[3], ('F_TAPE_100', HC(2, 1.0))):
            y, _ = eng(x, ATMOS + [f'decay={dec}'] + args)
            y = y * 10 ** ((-23 - lufs(y)) / 20)
            write_wav(os.path.join(rd, f'04_ATMOS_tail_{int(dec)}s_burst__{vn}.wav'), y)
    print('renders in', rd)

# ============================================================================ charts
if 'charts' in PARTS:
    import matplotlib; matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    BG, INK, INK2, GRID, OFFC = '#fcfcfb', '#0b0b0b', '#52514e', '#e8e7e3', '#8a8984'
    COL = {'SOFT': '#2a78d6', 'TUBE': '#eb6834', 'TAPE': '#1baf7a', 'LIVE': '#2a78d6', 'HQ': '#6250d6'}
    cd = os.path.join(OUT, 'charts_hc'); os.makedirs(cd, exist_ok=True)
    def style(ax, title=None, xl=None, yl=None):
        ax.set_facecolor(BG); ax.grid(color=GRID, lw=0.8); [s.set_visible(False) for s in ax.spines.values()]
        ax.tick_params(colors=INK2, labelsize=9)
        if title: ax.set_title(title, loc='left', fontsize=11, color=INK)
        if xl: ax.set_xlabel(xl, color=INK2)
        if yl: ax.set_ylabel(yl, color=INK2)
    def fig(w=9, h=4.2):
        f, ax = plt.subplots(figsize=(w, h), dpi=150); f.patch.set_facecolor(BG); return f, ax
    # 1. harmonics added, 500 Hz at -12 dBFS, DRIVE 50 %, LIVE
    f, ax = fig()
    k = np.arange(2, 10); wdt = 0.26
    for i, cn in enumerate(CH):
        H = np.array(R['spectra'][f'LIVE_{cn}_0.5']['H'])
        ax.bar(k + (i - 1) * wdt, H + 140, wdt - 0.03, bottom=-140, color=COL[cn], label=cn)
    ax.set_xticks(k, [f'H{j}' for j in k]); ax.set_ylim(-130, 0)
    style(ax, 'Harmonics added (500 Hz sine at -12 dBFS, DRIVE 50 %, LIVE), dB re fundamental', None, 'dB')
    ax.legend(frameon=False, fontsize=9, labelcolor=INK2); f.tight_layout(); f.savefig(os.path.join(cd, '1_harmonics_by_character.png')); plt.close(f)
    # 2. THD vs input level
    f, ax = fig()
    lv = np.arange(-60, 1, 6)
    for cn in CH:
        t = [R['level'][f'{cn}_{l}'] for l in lv]
        ax.plot(lv, t, color=COL[cn], lw=2, marker='o', ms=4, label=cn)
        ax.annotate(cn, (lv[-1], t[-1]), xytext=(6, 0), textcoords='offset points', color=INK2, fontsize=9, va='center')
    style(ax, 'Added harmonics vs level (DRIVE 50 %): the quiet end of a tail stays clean', 'input level, dBFS (sine 500 Hz)', 'THD, dB')
    ax.set_ylim(-130, 0); ax.legend(frameon=False, fontsize=9, labelcolor=INK2, loc='upper left'); f.tight_layout(); f.savefig(os.path.join(cd, '2_thd_vs_level.png')); plt.close(f)
    # 3. aliasing: 13 kHz at -12 dBFS, SOFT, DRIVE 80 %
    f, ax = fig()
    fa = np.array(R['_alias']['f'])
    ax.plot(fa / 1000, R['_alias']['naive'], color=OFFC, lw=1, label='no oversampling (same curve)')
    ax.plot(fa / 1000, R['_alias']['LIVE'], color=COL['LIVE'], lw=1.2, label='LIVE 4x IIR')
    ax.plot(fa / 1000, R['_alias']['HQ'], color=COL['HQ'], lw=1.2, label='STUDIO HQ 4x FIR', alpha=0.85)
    style(ax, 'Aliasing: 13 kHz sine at -12 dBFS, SOFT, DRIVE 80 % (a stress case): spectrum re fundamental', 'frequency, kHz', 'dB')
    ax.set_ylim(-150, 5); ax.set_xlim(0, 24); ax.legend(frameon=False, fontsize=9, labelcolor=INK2, loc='lower left'); f.tight_layout(); f.savefig(os.path.join(cd, '3_aliasing_13k.png')); plt.close(f)
    # 4. band balance on reverb (pink bursts, ATMOS 3 s, DRIVE 60 %, LEVEL COMP on) + TUBE raw
    f, ax = fig()
    c = np.array(R['_bands'])
    for cn in CH:
        ax.semilogx(c, R['lowmid'][f'vocal_3.0_POST_comp_{cn}_0.75']['curve'], color=COL[cn], lw=2, label=cn)
    ax.semilogx(c, R['lowmid']['vocal_3.0_POST_raw_TAPE_0.75']['curve'], color=COL['TAPE'], lw=1.2, ls='--', label='TAPE, LEVEL COMP off')
    ax.annotate('below 200 Hz this wet is ~50 dB under its main band:\nwhat the module adds there stays ~40 dB under', (70, 4.2), fontsize=8.5, color=INK2)
    ax.set_ylim(-4, 6)
    ax.axvspan(200, 500, color=GRID, alpha=0.6, lw=0)
    style(ax, 'Wet spectrum, ON vs OFF: vocal at -16 LUFS, ATMOS 3 s, DRIVE 75 %; shaded 200-500 Hz', 'Hz (1/3 octave)', 'dB')
    ax.set_xticks([63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000], ['63', '125', '250', '500', '1k', '2k', '4k', '8k', '16k'])
    ax.legend(frameon=False, fontsize=9, labelcolor=INK2); f.tight_layout(); f.savefig(os.path.join(cd, '4_band_balance.png')); plt.close(f)
    # 5. decays: EDC for 6 / 30 s
    f, axs = plt.subplots(1, 2, figsize=(11, 4.2), dpi=150); f.patch.set_facecolor(BG)
    for ax, dec in zip(axs, ('6.0', '30.0')):
        rows = R['decays'][dec]
        for vn, col, ls in (('OFF', OFFC, '-'), ('SOFT', COL['SOFT'], '-'), ('TUBE', COL['TUBE'], '-'), ('TAPE', COL['TAPE'], '-')):
            e = np.array(rows[vn]['_edc']); t = np.arange(len(e)) * 0.01
            ax.plot(t, e, color=col, lw=1.6 if vn != 'OFF' else 2.4, ls=ls, label=f'{vn}  T30 {rows[vn]["T30"]:.2f} s')
        style(ax, f'ATMOSPHERIC TAIL {float(dec):g} s: energy decay (DRIVE 75 %)', 's', 'dB')
        ax.set_ylim(-80, 2); ax.legend(frameon=False, fontsize=8.5, labelcolor=INK2)
    f.tight_layout(); f.savefig(os.path.join(cd, '5_decay_edc.png')); plt.close(f)
    # 6. group delay of the module, LIVE vs HQ
    f, ax = fig()
    g = R['_gd_curve']; fg = np.array(g['f'])
    ax.semilogx(fg, g['LIVE'], color=COL['LIVE'], lw=2, label='LIVE: reported 0, all-pass group delay')
    ax.semilogx(fg, g['HQ'], color=COL['HQ'], lw=2, label='STUDIO HQ: linear phase, exactly 71 (reported)')
    style(ax, 'HARMONIC COLOR alone: group delay of the coloured wet, samples at 48 kHz', 'Hz', 'samples')
    ax.set_ylim(0, 80); ax.legend(frameon=False, fontsize=9, labelcolor=INK2); f.tight_layout(); f.savefig(os.path.join(cd, '6_group_delay.png')); plt.close(f)
    print('charts in', cd)
