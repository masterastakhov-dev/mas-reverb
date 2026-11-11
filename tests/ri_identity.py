# MASTER SPACE 1.4: with SMART DUCKER and RESONANCE GUARD off the engine must equal ATMOS 1.3 bit for bit.
# usage: python3 tests/ri_identity.py <ATMOS 1.3 renderer> <1.4 renderer>
#   g++ -O2 -std=c++17 -DMAS_ATMOS -o render_atmos tests/render_space.cpp
#   g++ -O2 -std=c++17 -DMAS_ATMOS -DMAS_RI -o render_14 tests/render_space.cpp
import numpy as np, subprocess, sys, tempfile, os
tmp = tempfile.mkdtemp()
ok = True
for fs in (48000, 96000):
    x = (np.random.default_rng(5).standard_normal((fs * 4, 2)) * 0.1).astype('<f4')
    x[fs * 2:] *= 0.0
    inp = os.path.join(tmp, 'in.f32'); x.tofile(inp)
    cases = [['tailMode=1', 'decay=%g' % d] for d in (1.6, 3, 6, 12, 20, 30)]
    cases += [['tailMode=1', 'decay=20', 'routing=1', 'mix=1'],
              ['tailMode=0', 'algorithm=0', 'routing=0', 'mix=0.4'],
              ['tailMode=0', 'algorithm=1', 'decay=2.5'],
              ['tailMode=0', 'algorithm=2', 'decay=3']]
    for c in cases:
        outs = []
        for r in (sys.argv[1], sys.argv[2]):
            o = os.path.join(tmp, 'o.f32')
            subprocess.run([r, inp, o, str(fs), '256'] + c, check=True, capture_output=True)
            outs.append(np.fromfile(o, '<f4'))
        same = outs[0].shape == outs[1].shape and np.array_equal(outs[0], outs[1])
        ok &= same
        print(fs, c, 'identical' if same else 'DIFF max %.3g' % np.max(np.abs(outs[0] - outs[1])))
print('ALL IDENTICAL' if ok else 'MISMATCH')
sys.exit(0 if ok else 1)
