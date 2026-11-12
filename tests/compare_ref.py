#!/usr/bin/env python3
"""Null test of the plugin builds: MASTER SPACE 1.5 with the REVERB EQ off / idle against MASTER SPACE 1.4.
usage: compare_ref.py <dir of 1.4 renders> <dir of 1.5 renders>   (written by  <tests> --refhash <dir>)
The engine sources are proven bit-identical by tests/eq_identity.py (strict floating point). The plugin
binaries are built with the compiler's fast-math, so two separately compiled plugins may round differently:
here the difference must be zero or far below audibility (limit -120 dB re peak)."""
import sys, os, glob
import numpy as np
d14, d15 = sys.argv[1], sys.argv[2]
worst = -999.0; n = ident = 0; bad = 0
for f in sorted(glob.glob(os.path.join(d15, "*.f32"))):
    name = os.path.basename(f)[:-4]
    fs, sc, var = name.split("_", 2)
    a = np.fromfile(os.path.join(d14, "%s_%s_base.f32" % (fs, sc)), "<f4")
    b = np.fromfile(f, "<f4")
    n += 1
    if np.array_equal(a, b):
        ident += 1; res = "bit-identical"
    else:
        db = 20 * np.log10(np.max(np.abs(a - b)) / np.max(np.abs(a)) + 1e-30)
        worst = max(worst, db); res = "difference %.1f dB re peak" % db
        bad += db > -120.0
    print("%6s Hz  %-20s %-20s %s" % (fs, sc, var, res))
print("renders: %d   bit-identical: %d   worst difference: %s" % (n, ident, "none" if ident == n else "%.1f dB re peak" % worst))
sys.exit(1 if bad or n == 0 else 0)
