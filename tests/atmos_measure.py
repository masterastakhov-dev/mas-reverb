#!/usr/bin/env python3
"""ATMOSPHERIC TAIL measurements (late engine only, Aux, 100 % wet).

usage: MS_ATMOS_RENDER=<render_space built with -DMAS_ATMOS> atmos_measure.py [--quick] [--json out.json]
Measures at Decay 1 / 1.6 / 3 / 6 / 12 / 20 / 30 s: RT60 per octave, ETC, echo density (NED),
L/R correlation and mono compatibility, spectral balance (200-500 Hz vs 0.5-2 kHz), early and late
ringing, narrow-band mode decay, modulation (pitch wobble / spread of a decaying sine), initial level;
plus a Decay sweep 1 -> 30 -> 1 s while playing, and CPU at 48 / 96 kHz."""
import json, os, re, subprocess, sys, tempfile
import numpy as np
from scipy import signal
from scipy.special import erfc

HERE = os.path.dirname(os.path.abspath(__file__))
R = os.environ.get('MS_ATMOS_RENDER', '/tmp/ra')
FS = 48000
DECAYS = [1.0, 1.6, 3.0, 6.0, 12.0, 20.0, 30.0]

# reference ATMOSPHERIC TAIL settings (render keys); late engine only
BASE = dict(tailMode=1, erOn=0, routing=1, mix=1.0, lateLevelDb=0, lockLevel=0, lockDecay=1,
            bloom=0.3, motion=0.5, air=0.5, density=0.7, diffusion=0.7, shape=0.35, spreadMs=60, lateDelayMs=10,
            lfMult=0.9, lmMult=1.0, hmMult=1.0, hfMult=1.0, xLow=250, xMid=2000, xHigh=7000,
            modRate=0.6, modDepth=0.35, modRandom=0.5, modSmooth=0.6, decorrelation=0.85, charLock=1)


def run(x, fs=FS, block=512, changes=(), **kw):
    p = {**BASE, **kw}
    with tempfile.TemporaryDirectory() as d:
        fi, fo = os.path.join(d, 'i.f32'), os.path.join(d, 'o.f32')
        x.astype('<f4').tofile(fi)
        args = [R, fi, fo, str(fs), str(block)] + [f'{k}={v}' for k, v in p.items()] + list(changes)
        res = subprocess.run(args, capture_output=True, text=True, check=True)
        y = np.fromfile(fo, dtype='<f4').reshape(-1, 2)
    m = re.search(r'rt ([\d.]+)x', res.stderr)
    return y.astype(np.float64), float(m.group(1)) if m else float('nan')


def impulse(secs, fs=FS):
    x = np.zeros((int(fs * secs), 2), np.float32); x[10] = 1.0
    return x


def ir(decay, fs=FS, **kw):
    secs = max(3.0, decay * 1.45 + 1.2)
    y, rt = run(impulse(secs, fs), fs=fs, decay=decay, **kw)
    return y[10:], rt


OCT = [125, 250, 500, 1000, 2000, 4000, 8000]


def edc_rt(hb, fs, lo=-5, hi=-35):
    e = np.cumsum((hb ** 2).sum(1)[::-1])[::-1]
    e = 10 * np.log10(e / e[0] + 1e-30)
    i0, i1 = np.argmax(e <= lo), np.argmax(e <= hi)
    if i1 <= i0: return float('nan')
    t = np.arange(len(e)) / fs
    return float(-60 / np.polyfit(t[i0:i1], e[i0:i1], 1)[0])


def rt_bands(h, fs=FS):
    out = []
    for c in OCT:
        sos = signal.butter(3, [c / np.sqrt(2), min(c * np.sqrt(2), fs * 0.45)], btype='band', fs=fs, output='sos')
        out.append(edc_rt(signal.sosfiltfilt(sos, h, axis=0), fs))
    return out


def expected_band_rt(decay, kw=None):
    """what the engine is asked for (DECAY x DECAY EQ x AIR, with the AIR limit and Character Lock LF taper)"""
    p = {**BASE, **(kw or {})}
    air = p['air']
    lf = p['lfMult']
    if p['charLock'] and lf > 1: lf = 1 + (lf - 1) * min(1.0, max(0.3, 1 - 0.35 * np.log2(decay / 3.0)))
    hf = p['hfMult'] * (0.8 + 0.4 * air); hm = p['hmMult'] * (0.9 + 0.2 * air)
    cap = 4 + 16 * air
    capped = lambda r, c: 1 / np.sqrt(1 / r ** 2 + 1 / c ** 2)
    band = lambda f: (decay * lf if f < p['xLow'] else decay * p['lmMult'] if f < p['xMid']
                      else capped(decay * hm, 2.5 * cap) if f < p['xHigh'] else capped(decay * hf, cap))
    return [band(c) for c in OCT]


def etc(h, fs=FS, bin_ms=10, secs=None):
    e = (h ** 2).sum(1)
    w = int(fs * bin_ms / 1000)
    n = len(e) // w if secs is None else min(len(e) // w, int(secs * 1000 / bin_ms))
    b = e[:n * w].reshape(n, w).sum(1)
    return 10 * np.log10(b / b.max() + 1e-30)


def ned(h, fs=FS, win_ms=20, secs=0.5):
    x = h.sum(1)[: int(fs * secs)]
    w = int(fs * win_ms / 1000); out = []
    win = np.hanning(w); win /= win.sum()
    for i in range(0, len(x) - w, w // 4):
        seg = x[i:i + w]; sd = np.sqrt(np.sum(win * seg ** 2))
        out.append(np.sum(win * (np.abs(seg) > sd)) / erfc(1 / np.sqrt(2)) if sd > 0 else 0)
    t = (np.arange(len(out)) * (w // 4) + w / 2) / fs
    return np.array(out), t


def corr(a, b):
    d = np.sqrt(np.sum(a * a) * np.sum(b * b))
    return float(np.sum(a * b) / d) if d > 0 else 0.0


def stereo(h, fs=FS, decay=1.8):
    late = h[int(0.08 * fs): int((0.08 + min(decay, 3.0)) * fs)]
    bands = [(60, 150), (150, 350), (350, 800), (800, 2000), (2000, 6000), (6000, 16000)]
    coh, mono = [], []
    for lo, hi in bands:
        sos = signal.butter(3, [lo, hi], btype='band', fs=fs, output='sos')
        y = signal.sosfiltfilt(sos, late, axis=0)
        coh.append(corr(y[:, 0], y[:, 1]))
        mono.append(10 * np.log10(np.sum((y[:, 0] + y[:, 1]) ** 2) / 2 / np.sum(y ** 2) + 1e-30))   # 0 dB = mono-safe, -3 = uncorrelated
    return corr(late[:, 0], late[:, 1]), coh, mono


def balance(h, fs=FS, decay=1.8):
    """power spectral density of the tail (white input) in bands, dB re 0.5-2 kHz"""
    seg = h[int(0.03 * fs): int((0.03 + min(decay, 4.0)) * fs)]
    f, P = signal.welch(seg, fs, nperseg=8192, axis=0)
    P = P.sum(1)
    ref = P[(f >= 500) & (f < 2000)].mean()
    bands = [(60, 200), (200, 500), (500, 2000), (2000, 6000), (6000, 12000)]
    return [float(10 * np.log10(P[(f >= lo) & (f < hi)].mean() / ref)) for lo, hi in bands]


def ringing(x, fs=FS, t0=0.12, dur=1.0, nfft=4096, hop=1024):
    seg = x[int(t0 * fs): int((t0 + dur) * fs)]
    f, t, Z = signal.stft(seg, fs, nperseg=nfft, noverlap=nfft - hop, window='hann', boundary=None)
    logP = 10 * np.log10(np.abs(Z) ** 2 + 1e-30)
    env = np.empty_like(logP)
    for i in range(len(f)):
        w = max(3, int(f[i] * 0.06 / (fs / nfft)))
        env[i] = np.median(logP[max(0, i - w):i + w + 1], axis=0)
    mean = 10 * np.log10(np.mean(10 ** ((logP - env) / 10), axis=1))
    return [float(np.percentile(mean[(f >= lo) & (f < hi)], 99.7)) for lo, hi in [(150, 800), (800, 3000), (3000, 10000)]]


def mode_decay(x, fs=FS, f_lo=200, f_hi=8000, per_oct=12):
    cents = f_lo * 2 ** (np.arange(int(np.log2(f_hi / f_lo) * per_oct)) / per_oct)
    rts = []
    for c in cents:
        sos = signal.butter(2, [c * 2 ** (-0.5 / per_oct), c * 2 ** (0.5 / per_oct)], btype='band', fs=fs, output='sos')
        hb = signal.sosfilt(sos, x)
        e = np.cumsum(hb[::-1] ** 2)[::-1]; e = 10 * np.log10(e / e[0] + 1e-30)
        i5, i25 = np.argmax(e <= -5), np.argmax(e <= -25)
        rts.append((i25 - i5) / fs * 3.0 if i25 > i5 else np.nan)
    rts = np.array(rts)
    sm = signal.medfilt(np.nan_to_num(rts, nan=np.nanmedian(rts)), 7)
    ratio = rts / sm
    return float(np.nanpercentile(ratio, 95)), float(np.nanmax(ratio))


def modulation(decay, f0=440.0, fs=FS, **kw):
    """sine for 2 s, then the tail: pitch wobble (std of the peak frequency, cents) and spread (rms width, cents)"""
    secs = 2.0 + min(decay, 4.0) + 0.5
    n = int(fs * secs); t = np.arange(n) / fs
    x = np.zeros((n, 2), np.float32)
    s = 0.25 * np.sin(2 * np.pi * f0 * t) * (t < 2.0) * np.minimum(1, t / 0.02) * np.minimum(1, np.maximum(0, (2.0 - t) / 0.02))
    x[:, 0] = s; x[:, 1] = s
    y, _ = run(x, fs=fs, decay=decay, **kw)
    seg = y[int(2.05 * fs): int((2.05 + min(decay, 3.0)) * fs)].sum(1)
    nper = int(fs * 0.25)
    f, tt, Z = signal.stft(seg, fs, nperseg=nper, noverlap=nper * 3 // 4, nfft=nper * 8)
    P = np.abs(Z) ** 2
    sel = (f > f0 * 2 ** (-150 / 1200)) & (f < f0 * 2 ** (150 / 1200))
    cents = 1200 * np.log2(f[sel] / f0)
    Ps = P[sel]
    good = Ps.sum(0) > Ps.sum(0).max() * 1e-3
    cen = (cents[:, None] * Ps).sum(0) / (Ps.sum(0) + 1e-30)
    spread = np.sqrt(((cents[:, None] - cen) ** 2 * Ps).sum(0) / (Ps.sum(0) + 1e-30))
    return float(np.std(cen[good])), float(np.median(spread[good]))


def initial_level(h, fs=FS):
    e = (h[int(0.05 * fs): int(0.2 * fs)] ** 2).sum()
    return float(10 * np.log10(e + 1e-30))


def _sweep_changes(fs, a=1.0, b=30.0, t1=3.0, t2=9.0, steps=50, dur=1.0):
    ch = []
    for k in range(steps + 1):
        ch.append(f'@{int((t1 + k * dur / steps) * fs)}:decay={a * (b / a) ** (k / steps):.4f}')
        ch.append(f'@{int((t2 + k * dur / steps) * fs)}:decay={b * (a / b) ** (k / steps):.4f}')
    return ch


def _pitch_track(y, fs, f0=440.0):
    """power-weighted frequency centroid within +-100 cents of f0, both channels (0.2 s frames): a moving read
    head shifts it; the beating of neighbouring modes (nulls of one channel or of a sum) does not"""
    nper = int(fs * 0.2)
    f, tt, Z = signal.stft(y, fs, nperseg=nper, noverlap=nper * 3 // 4, nfft=nper * 16, axis=0)
    P = (np.abs(Z) ** 2).sum(1)                     # (freq, frames), channels summed in power
    sel = (f > f0 * 2 ** (-100 / 1200)) & (f < f0 * 2 ** (100 / 1200))
    cents = 1200 * np.log2(f[sel] / f0)
    Ps = P[sel]
    e = Ps.sum(0)
    cen = (cents[:, None] * Ps).sum(0) / (e + 1e-30)
    cen[e < e.max() * 1e-5] = np.nan                # too far down to say anything
    return cen, tt


def decay_sweep(fs=FS):
    """Decay 1 -> 30 s over 1 s at 3 s and 30 -> 1 s over 1 s at 9 s, while playing.
    level: pink noise, 10 ms RMS, largest rise above the previous 60 ms (after the first 0.5 s);
    pitch: 440 Hz sine, motion 0 (so only the sweep can move it), against the same render without sweep."""
    secs = 14.0
    n = int(fs * secs); t = np.arange(n) / fs
    rng = np.random.default_rng(3)
    nz = signal.lfilter([0.049922035, -0.095993537, 0.050612699, -0.004408786], [1, -2.494956002, 2.017265875, -0.522189400], rng.standard_normal(n))
    nz = (nz / np.sqrt(np.mean(nz ** 2)) * 10 ** (-18 / 20)).astype(np.float32)
    y, _ = run(np.stack([nz, nz], 1), fs=fs, decay=1.0, changes=_sweep_changes(fs))
    finite = bool(np.all(np.isfinite(y)))
    w = int(0.01 * fs)
    lev = 10 * np.log10((y[: len(y) // w * w] ** 2).sum(1).reshape(-1, w).mean(1) + 1e-30)
    jump = max(lev[k] - np.median(lev[k - 6:k]) for k in range(50, len(lev)))
    ys, _ = run(np.stack([nz, nz], 1), fs=fs, decay=1.0)
    levs = 10 * np.log10((ys[: len(ys) // w * w] ** 2).sum(1).reshape(-1, w).mean(1) + 1e-30)
    jump_static = max(levs[k] - np.median(levs[k - 6:k]) for k in range(50, len(levs)))
    # pitch: a 440 Hz tone for 2 s, then its tail while Decay moves 6 -> 30 s (2.4..3.4 s) and back to 3 s
    # (5.0..6.0 s); the tail's frequency is compared with the same render without automation (motion 0)
    ts = int(fs * 8.0)
    s = (0.3 * np.sin(2 * np.pi * 440 * np.arange(ts) / fs) * (np.arange(ts) < 2 * fs)).astype(np.float32)
    x = np.stack([s, s], 1)
    ch = []
    for k in range(51):
        ch.append(f'@{int((2.4 + k * 0.02) * fs)}:decay={6.0 * 5.0 ** (k / 50):.4f}')
        ch.append(f'@{int((5.0 + k * 0.02) * fs)}:decay={30.0 * 0.1 ** (k / 50):.4f}')
    yp, _ = run(x, fs=fs, decay=6.0, motion=0.0, changes=ch)
    y0, _ = run(x, fs=fs, decay=6.0, motion=0.0)
    cp, tt = _pitch_track(yp, fs); c0, _ = _pitch_track(y0, fs)
    sel = (tt > 2.2) & (tt < 6.5)
    dev = float(np.nanmax(np.abs(cp[sel] - c0[sel])))
    return dict(finite=finite, peak=float(np.max(np.abs(y))), max_level_jump_db=float(jump), static_level_jump_db=float(jump_static),
                pitch_dev_cents_max=dev, pitch_static_cents_max=float(np.nanmax(np.abs(c0[sel] - np.nanmedian(c0[sel])))),
                pitch_t=tt[sel].tolist(), pitch_ref_cents=c0[sel].tolist(),
                level_db_100ms=lev[::10].tolist(), pitch_cents=cp[sel].tolist())


def cpu():
    out = {}
    for fs in (48000, 96000):
        rng = np.random.default_rng(1)
        x = (rng.standard_normal((fs * 20, 2)) * 0.1).astype(np.float32)
        rts = [run(x, fs=fs, block=256, decay=3.0)[1] for _ in range(3)]
        out[fs] = 100.0 / max(rts)
    return out


def main():
    quick = '--quick' in sys.argv
    res = dict(decays=DECAYS, rt=[], rt_expected=[], etc=[], ned=[], corr=[], coh=[], mono=[], balance=[], ring_early=[], ring_late=[],
               modes=[], wobble=[], spread=[], init=[], ned_t=None)
    for d in DECAYS:
        h, _ = ir(d)
        res['rt'].append(rt_bands(h)); res['rt_expected'].append(expected_band_rt(d))
        res['etc'].append(etc(h, secs=min(4.0, d * 1.3)).tolist())
        nd, nt = ned(h); res['ned'].append(nd.tolist()); res['ned_t'] = nt.tolist()
        c, coh, mono = stereo(h, decay=d); res['corr'].append(c); res['coh'].append(coh); res['mono'].append(mono)
        res['balance'].append(balance(h, decay=d))
        res['ring_early'].append(float(np.mean([ringing(h[:, c]) for c in (0, 1)], 0).max()))
        res['ring_late'].append(float(np.mean([ringing(h[:, c], t0=0.4 * d, dur=min(1.0, 0.5 * d)) for c in (0, 1)], 0).max()))
        res['modes'].append(mode_decay(h.sum(1)) if not quick or d in (1.6, 12.0) else (float('nan'), float('nan')))
        wob, spr = modulation(d)
        res['wobble'].append(wob); res['spread'].append(spr)
        res['init'].append(initial_level(h))
        print(f'decay {d:5.1f}: RT oct {" ".join(f"{v:5.2f}" for v in res["rt"][-1])} | exp {" ".join(f"{v:5.2f}" for v in res["rt_expected"][-1])}')
        print(f'             corr {c:+.2f} coh {" ".join(f"{v:+.2f}" for v in coh)} mono {" ".join(f"{v:+.1f}" for v in mono)}')
        print(f'             balance {" ".join(f"{v:+.1f}" for v in res["balance"][-1])}  ring early {res["ring_early"][-1]:.2f} late {res["ring_late"][-1]:.2f}'
              f'  modes {res["modes"][-1][0]:.2f}/{res["modes"][-1][1]:.2f}  wobble {wob:.1f} c  spread {spr:.1f} c  init {res["init"][-1]:+.2f} dB'
              f'  NED@50/100/200ms {np.interp([0.05, 0.1, 0.2], nt, nd).round(2).tolist()}')
    if not quick:
        sw = decay_sweep(); res['sweep'] = sw
        print(f'sweep 1->30->1 s: finite {sw["finite"]}, peak {sw["peak"]:.2f}, largest 10 ms level rise {sw["max_level_jump_db"]:.2f} dB '
              f'(no sweep {sw["static_level_jump_db"]:.2f}), tail pitch vs. no automation: max {sw["pitch_dev_cents_max"]:.2f} cents')
        c = cpu(); res['cpu'] = c
        print(f'CPU (late engine only, one core): 48 kHz {c[48000]:.1f} %, 96 kHz {c[96000]:.1f} %')
    if '--json' in sys.argv:
        json.dump(res, open(sys.argv[sys.argv.index('--json') + 1], 'w'))
    if '--check' in sys.argv:
        bad = []
        for d, rt in zip(DECAYS, res['rt']):
            for oct_i in (2, 3):                                    # 500 Hz, 1 kHz octaves: the Decay reference
                if abs(rt[oct_i] / d - 1) > 0.08: bad.append(f'RT {OCT[oct_i]} Hz at {d} s = {rt[oct_i]:.2f}')
        if max(res['ring_early'] + res['ring_late']) > 5.6: bad.append('ringing above 5.6')
        if any(m[0] > 1.3 for m in res['modes'] if m[0] == m[0]): bad.append('mode decay p95 above 1.3')
        if any(v[0] < -2.5 for v in res['mono']): bad.append('low-frequency mono sum below -2.5 dB')
        if max(res['init']) - min(res['init']) > 1.0: bad.append('initial level varies more than 1 dB')
        if 'sweep' in res:
            if not res['sweep']['finite'] or res['sweep']['peak'] > 4: bad.append('sweep not stable')
            if res['sweep']['pitch_dev_cents_max'] > 10: bad.append('pitch moves with Decay')
            if res['sweep']['max_level_jump_db'] > res['sweep']['static_level_jump_db'] + 1.5: bad.append('level jump during Decay automation')
        print('ATMOS CHECK:', 'OK' if not bad else 'FAILED: ' + '; '.join(bad))
        sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
