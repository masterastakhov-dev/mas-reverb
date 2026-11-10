"""Metallic-colouration tests for MASTER SPACE (Early and Late separately).

  ringing      spectral peaks that persist over time: every STFT frame is normalised by its own smoothed
               spectrum and the frames are averaged. A diffuse decay averages out flat; modes or combs
               that stand out of it stay as peaks (dB, 99.7th percentile, per band). Compared with an ideal
               reference: exponentially decaying Gaussian noise.
  mode decay   decay time in narrow bands (1/24 octave) against the smoothed decay curve: resonances that
               ring longer than their neighbours (95th percentile and max of RT_band / RT_smooth).
  cepstrum     regularity of the delay structure: largest real-cepstrum peak (quefrency 0.7-60 ms) of the
               impulse response; a regular delay pattern shows up as a strong peak at its period.
  transient    ringing of the response to a short snare-like burst.

Run:   g++ -O2 -std=c++17 -o tests/render_space tests/render_space.cpp && python3 tests/metal.py
A/B:   MS_OLD_RENDER=/path/to/old/render_space python3 tests/metal.py
CI:    python3 tests/metal.py --check   (fails if a limit is exceeded)
"""
import numpy as np, os, sys
from scipy import signal

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import analyze2 as a

fs = 48000
NEW = os.environ.get('MV2_RENDER', os.path.join(HERE, 'render_space'))
OLD = os.environ.get('MS_OLD_RENDER')
BANDS = [(150, 800), (800, 3000), (3000, 10000)]

def render(binary, x, **kw):
    a.R = binary
    return a.run(x, **kw)[0]

def ringing(h, t0=0.12, dur=1.0, nfft=4096, hop=1024):
    seg = h[int(t0 * fs): int((t0 + dur) * fs)].astype(np.float64)
    f, t, Z = signal.stft(seg, fs, nperseg=nfft, noverlap=nfft - hop, window='hann', boundary=None)
    logP = 10 * np.log10(np.abs(Z) ** 2 + 1e-30)
    env = np.empty_like(logP)
    for i in range(len(f)):
        w = max(3, int(f[i] * 0.06 / (fs / nfft)))
        env[i] = np.median(logP[max(0, i - w):i + w + 1], axis=0)
    mean = 10 * np.log10(np.mean(10 ** ((logP - env) / 10), axis=1))
    return [float(np.percentile(mean[(f >= lo) & (f < hi)], 99.7)) for lo, hi in BANDS]

def reference(rt=1.8, seed=1, n=None):
    r = np.random.default_rng(seed)
    n = n or int(fs * 1.4)
    return r.standard_normal(n) * np.exp(-6.91 * np.arange(n) / fs / rt)

def late_ringing(binary, kw):
    """three LFO phases x two channels"""
    acc = []
    for off in (0.0, 0.43, 0.91):
        x = np.zeros((int(fs * (1.6 + off)), 2), np.float32); i0 = 10 + int(off * fs); x[i0] = 1
        y = render(binary, x, routing=1, erOn=0, lockLevel=0, **kw)
        acc += [ringing(y[i0:, 0]), ringing(y[i0:, 1])]
    return np.mean(acc, 0)

def early_ringing(binary, kw):
    y = render(binary, a.impulse(fs, 0.4), routing=1, lateOn=0, lockLevel=0, **kw)
    return np.mean([ringing(y[10:, c], t0=0.004, dur=0.2, nfft=1024, hop=128) for c in (0, 1)], 0)

def mode_decay(h, f_lo=400, f_hi=8000, per_oct=12):
    """narrow-band RT (causal Butterworth band-pass, 1/12 octave; Schroeder -5..-25 dB) against its own
    smoothed curve: resonances that ring longer than their neighbours. Returns (95th pct, max) of the ratio."""
    cents = f_lo * 2 ** (np.arange(int(np.log2(f_hi / f_lo) * per_oct)) / per_oct)
    rts = []
    for c in cents:
        sos = signal.butter(2, [c * 2 ** (-0.5 / per_oct), c * 2 ** (0.5 / per_oct)], btype='band', fs=fs, output='sos')
        hb = signal.sosfilt(sos, h)
        e = np.cumsum(hb[::-1] ** 2)[::-1]; e = 10 * np.log10(e / e[0] + 1e-30)
        i5, i25 = np.argmax(e <= -5), np.argmax(e <= -25)
        rts.append((i25 - i5) / fs * 3.0 if i25 > i5 else np.nan)
    rts = np.array(rts)
    sm = signal.medfilt(np.nan_to_num(rts, nan=np.nanmedian(rts)), 7)
    ratio = rts / sm
    return float(np.nanpercentile(ratio, 95)), float(np.nanmax(ratio))

def cepstrum_peak(h, lo_ms=0.7, hi_ms=60):
    n = 1 << 16
    c = np.fft.irfft(np.log(np.abs(np.fft.rfft(h, n)) + 1e-9))
    lo, hi = int(lo_ms * fs / 1000), int(hi_ms * fs / 1000)
    k = lo + int(np.argmax(np.abs(c[lo:hi])))
    noise = np.median(np.abs(c[lo:hi]))
    return float(np.abs(c[k]) / noise), k / fs * 1000

def snare():
    r = np.random.default_rng(11)
    n = int(0.12 * fs); t = np.arange(n) / fs
    s = (r.standard_normal(n) * np.exp(-t / 0.035) + 0.8 * np.sin(2 * np.pi * 190 * t) * np.exp(-t / 0.05)) * 0.3
    x = np.zeros((int(fs * 1.8), 2), np.float32); x[10:10 + n, 0] = s; x[10:10 + n, 1] = s
    return x

CONFIGS = {
    'Chamber (default)': {},
    'Studio Room': dict(algorithm=0),
    'Hall 2.6 s': dict(algorithm=2, decay=2.6),
    'Vocal Booth room': dict(roomW=2.4, roomD=3.0, roomH=2.5, mat0=1, mat1=0, mat2=1, mat3=1, mat4=2, mat5=0, algorithm=0),
    'Stone Chamber room': dict(roomW=6, roomD=8, roomH=5, mat0=4, mat1=4, mat2=4, mat3=4, mat4=4, mat5=4),
    'Concert Hall room': dict(roomW=22, roomD=34, roomH=14, mat0=2, mat1=3, mat2=3, mat3=3, mat4=2, mat5=3, algorithm=2, decay=2.6),
    'Chamber, density 1': dict(density=1.0),
    'Studio Room, 5 s': dict(algorithm=0, decay=5.0),
}
LIMITS = {'late_ring': 6.5, 'early_ring': 4.0, 'mode_p95': 1.5}

def fmt(v): return '  '.join(f'{x:5.2f}' for x in v)

if __name__ == '__main__':
    check = '--check' in sys.argv
    bins = [('new', NEW)] + ([('1.0', OLD)] if OLD else [])
    fails = []
    ref = np.mean([ringing(reference(1.8, s)) for s in range(6)], 0)

    print('== LATE ringing, impulse (dB, 99.7 pct of time-averaged normalised spectrum)  150-800 / 0.8-3k / 3-10k')
    print(f'  {"ideal diffuse decay":26s}       {fmt(ref)}')
    for name, kw in CONFIGS.items():
        for tag, b in bins:
            r = late_ringing(b, kw)
            print(f'  {name:26s} {tag:4s}  {fmt(r)}')
            if tag == 'new' and max(r) > LIMITS['late_ring']: fails.append(f'late ringing {name} {max(r):.2f}')

    print('\n== LATE ringing, snare-like transient (Aux, late only)')
    for name in ('Chamber (default)', 'Studio Room', 'Hall 2.6 s', 'Vocal Booth room'):
        for tag, b in bins:
            y = render(b, snare(), routing=1, erOn=0, lockLevel=0, **CONFIGS[name])
            print(f'  {name:26s} {tag:4s}  {fmt(np.mean([ringing(y[10:, c], t0=0.15, dur=1.0) for c in (0, 1)], 0))}')

    print('\n== LATE mode decay: narrow-band RT / smoothed RT, 400 Hz-8 kHz, 1/12 oct (95th pct / max); ideal diffuse decay: %.2f / %.2f' % tuple(np.mean([mode_decay(reference(1.8, s, int(fs * 4.0))) for s in range(4)], 0)))
    for name in ('Chamber (default)', 'Studio Room', 'Hall 2.6 s', 'Vocal Booth room', 'Stone Chamber room'):
        for tag, b in bins:
            y = render(b, a.impulse(fs, 4.0), routing=1, erOn=0, lockLevel=0, modDepth=0, **CONFIGS[name])
            p95, mx = mode_decay(y[:, 0].astype(np.float64))
            print(f'  {name:26s} {tag:4s}  {p95:5.2f} / {mx:5.2f}   (mod off)')
            if tag == 'new' and p95 > LIMITS['mode_p95']: fails.append(f'mode decay {name} {p95:.2f}')

    print('\n== EARLY ringing (early only, Aux)')
    for diff in (0.0, 0.4, 0.7, 1.0):
        for tag, b in bins:
            r = early_ringing(b, dict(erDiffusion=diff))
            print(f'  {"ER diffusion " + str(diff):26s} {tag:4s}  {fmt(r)}')
            if tag == 'new' and max(r) > LIMITS['early_ring']: fails.append(f'early ringing diffusion {diff} {max(r):.2f}')
    for room in ('Stone Chamber room', 'Vocal Booth room', 'Concert Hall room'):
        for tag, b in bins:
            print(f'  {room:26s} {tag:4s}  {fmt(early_ringing(b, CONFIGS[room]))}')

    print('\n== Delay regularity: largest real-cepstrum peak (x median, quefrency) - Early only / Late only (first 300 ms)')
    for name in ('Chamber (default)', 'Studio Room', 'Hall 2.6 s', 'Stone Chamber room'):
        for tag, b in bins:
            ye = render(b, a.impulse(fs, 0.4), routing=1, lateOn=0, lockLevel=0, **CONFIGS[name])
            yl = render(b, a.impulse(fs, 0.6), routing=1, erOn=0, lockLevel=0, modDepth=0, **CONFIGS[name])
            ce, qe = cepstrum_peak(ye[10:int(0.2 * fs), 0].astype(np.float64))
            cl, ql = cepstrum_peak(yl[10:int(0.3 * fs), 0].astype(np.float64))
            print(f'  {name:26s} {tag:4s}  early {ce:5.1f}x @ {qe:5.1f} ms | late {cl:5.1f}x @ {ql:5.1f} ms')

    if check:
        print('\nLIMITS', LIMITS)
        if fails:
            print('FAILED:\n  ' + '\n  '.join(fails)); sys.exit(1)
        print('all within limits')
