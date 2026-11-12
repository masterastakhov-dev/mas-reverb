#!/usr/bin/env python3
"""Compares the reference render hashes of the 1.4 and 1.5 test binaries (--refhash).
Every 1.5 render with the REVERB EQ off / idle must equal the 1.4 render of the same scenario bit for bit."""
import sys
r14 = {}
for l in open(sys.argv[1]):
    if l.startswith("REF"):
        _, fs, sc, var, h = l.split()
        r14[(fs, sc)] = h
ok = bad = 0
for l in open(sys.argv[2]):
    if not l.startswith("REF"):
        continue
    _, fs, sc, var, h = l.split()
    same = r14.get((fs, sc)) == h
    ok += same
    bad += not same
    print("%-9s %6s Hz  %-20s %-20s %s" % ("identical" if same else "DIFFERENT", fs, sc, var, h))
print("1.4 scenarios: %d   1.5 renders identical to 1.4: %d   different: %d" % (len(r14), ok, bad))
sys.exit(1 if bad or not ok else 0)
