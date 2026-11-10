#!/usr/bin/env python3
"""Renders every factory preset through the offline engine and checks it.

usage: MV2_RENDER=<render_space 1.2> MS_LEGACY_RENDER=<render_space 1.1> check_presets.py [--trims out.json]
Prints loudness re input (pink noise, K-weighted), RT (T20), peak, NaN; with --trims writes the
output trims that put presets of each kind at the target loudness."""
import glob, json, os, sys
import numpy as np
from scipy import signal
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'tests'))
import analyze2 as a

FS = 48000
CHOICES = {"algo": ["Studio Room", "Chamber", "Hall"], "routing": ["Insert", "Aux"],
           "mat": ["Absorber", "Curtain", "Wood", "Plaster", "Concrete", "Glass"],
           "modchar": ["Smooth", "Random", "Vintage", "Modern"]}
PCT = dict(depth="depth", size="size", diffusion="diffusion", density="density", shape="shape", width="width", mix="mix",
           srcx="srcX", lisx="lisX", lisy="lisY", erdensity="erDensity", erdiffusion="erDiffusion", ercolor="erColor",
           erwidth="erWidth", latewidth="lateWidth", latecolor="lateColor", moddepth="modDepth", modrandom="modRandom",
           modsmooth="modSmooth", balance="balance", crossfeed="crossfeed", decorr="decorrelation")
PLAIN = dict(decay="decay", predelay="preDelayMs", erlevel="erLevelDb", latelevel="lateLevelDb", spread="spreadMs",
             output="outputDb", roomw="roomW", roomd="roomD", roomh="roomH", erstart="erStartMs", erstop="erStopMs",
             erpredelay="erPreDelayMs", latedelay="lateDelayMs", lfmult="lfMult", lmmult="lmMult", hmmult="hmMult",
             hfmult="hfMult", xlow="xLow", xmid="xMid", xhigh="xHigh", lowcut="lowCutHz", highcut="highCutHz", modrate="modRate")
BOOL = dict(locklevel="lockLevel", locktone="lockTone", lockdecay="lockDecay", lockwidth="lockWidth", eron="erOn", lateon="lateOn")
MATS = ["matfront", "matback", "matleft", "matright", "matfloor", "matceiling"]


def render_args(p):
    kw = {"algorithm": CHOICES["algo"].index(p["algo"]), "routing": CHOICES["routing"].index(p["routing"]),
          "modCharacter": CHOICES["modchar"].index(p["modchar"])}
    if "tail" in p:   # ATMOSPHERIC TAIL presets (experimental build)
        kw.update(tailMode=1 if p["tail"] == "Atmospheric Tail" else 0, bloom=p["bloom"] / 100.0, motion=p["motion"] / 100.0,
                  air=p["air"] / 100.0, charLock=1 if p["charlock"] else 0)
    for k, v in PCT.items(): kw[v] = p[k] / 100.0
    for k, v in PLAIN.items(): kw[v] = p[k]
    for k, v in BOOL.items(): kw[v] = 1 if p[k] else 0
    for i, m in enumerate(MATS): kw[f"mat{i}"] = CHOICES["mat"].index(p[m])
    return kw


def kweight(x):
    f0, G, Q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    K = np.tan(np.pi * f0 / FS); Vh = 10 ** (G / 20); Vb = Vh ** 0.4996667741545416; a0 = 1 + K / Q + K * K
    b1 = [(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0]
    a1 = [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0]
    f0, Q = 38.13547087602444, 0.5003270373238773
    K = np.tan(np.pi * f0 / FS); a0 = 1 + K / Q + K * K
    a2 = [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0]
    return signal.lfilter([1, -2, 1], a2, signal.lfilter(b1, a1, x, axis=0), axis=0)


def lufs(x):
    y = kweight(x.astype(np.float64))
    return -0.691 + 10 * np.log10(np.sum(np.mean(y ** 2, axis=0)) + 1e-20)


def pink(n, seed=1):
    rng = np.random.default_rng(seed)
    w = rng.standard_normal(n)
    b, aa = [0.049922035, -0.095993537, 0.050612699, -0.004408786], [1, -2.494956002, 2.017265875, -0.522189400]
    p = signal.lfilter(b, aa, w)
    return p / np.sqrt(np.mean(p ** 2)) * 10 ** (-18 / 20)


def main():
    sub = 'factory-atmos' if '--atmos' in sys.argv else 'factory'
    files = sorted(glob.glob(os.path.join(os.path.dirname(__file__), '..', 'presets', sub, '*.json')))
    new_r, old_r = os.environ.get('MV2_RENDER', '/tmp/r12b'), os.environ.get('MS_LEGACY_RENDER', '/tmp/r11')
    if '--atmos' in sys.argv: new_r = os.environ.get('MS_ATMOS_RENDER', '/tmp/ra')
    x = pink(4 * FS); xs = np.stack([x, x], 1).astype(np.float32)
    lin = lufs(xs[FS:])
    rows, ok = [], True
    for f in files:
        d = json.load(open(f)); p = d["parameters"]
        a.R = old_r if p["engine"].startswith("1.1") else new_r
        kw = render_args(p)
        y, _ = a.run(xs, fs=FS, block=512, **kw)
        h, _ = a.run(a.impulse(FS, max(3.0, min(p["decay"] * 2.5, p["decay"] + 8))), fs=FS, block=512, **kw)
        nan = not np.all(np.isfinite(y)) or not np.all(np.isfinite(h))
        loud = lufs(y[FS:]) - lin
        peak = 20 * np.log10(np.max(np.abs(y)) + 1e-12)
        rt = a.rt60(h.mean(1), FS) if p["lateon"] else float('nan')
        rows.append((d["name"], d["category"], p["routing"], p["engine"], loud, rt, peak, nan))
        ok &= not nan and peak < -0.5
        print(f'{d["name"]:36s} {d["category"]:18s} {p["routing"]:6s} {p["engine"]:10s} loud {loud:+6.1f} LU  RT {rt:5.2f} s'
              f' (decay {p["decay"]:.2f})  peak {peak:6.1f} dBFS  nan {nan}')
    if '--trims' in sys.argv:
        # Aux presets (100 % wet): wet at -6 LU re input; Insert presets with direct: equal loudness to the input
        trims = {}
        for name, cat, routing, eng, loud, *_ in rows:
            if name.startswith("LeadVox"): continue          # the user's settings stay as they were
            target = -6.0 if routing == "Aux" else 0.0
            trims[name] = round(target - loud, 1)
        json.dump(trims, open(sys.argv[sys.argv.index('--trims') + 1], 'w'), indent=1)
        print('trims written')
    print('ALL OK' if ok else 'PROBLEMS FOUND')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
