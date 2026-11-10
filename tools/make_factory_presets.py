#!/usr/bin/env python3
"""Writes presets/factory/*.json (MASTER SPACE factory library, schema 1).

Each preset = the plugin defaults + the settings listed here. Output trims are calibrated by
tools/check_presets.py so that presets of the same kind have similar loudness."""
import json, os, re, sys

DEFAULTS = {
    "engine": "1.2", "algo": "Chamber", "mode": "Algorithmic", "routing": "Insert",
    "depth": 35, "decay": 1.8, "size": 50, "predelay": 0, "erlevel": 0, "latelevel": 0,
    "diffusion": 70, "density": 70, "shape": 35, "spread": 60, "width": 100, "mix": 35, "output": 0,
    "locklevel": True, "locktone": False, "lockdecay": True, "lockwidth": False,
    "roomw": 7.5, "roomd": 10, "roomh": 4, "srcx": 50, "lisx": 50, "lisy": 72,
    "matfront": "Plaster", "matback": "Wood", "matleft": "Plaster", "matright": "Plaster", "matfloor": "Wood", "matceiling": "Plaster",
    "eron": True, "erdensity": 60, "erdiffusion": 40, "ercolor": 0, "erwidth": 100, "erstart": 0, "erstop": 150, "erpredelay": 0,
    "lateon": True, "latedelay": 20, "latewidth": 100, "latecolor": 0,
    "lfmult": 1.2, "lmmult": 1.0, "hmmult": 0.85, "hfmult": 0.55, "xlow": 200, "xmid": 1500, "xhigh": 6000,
    "lowcut": 20, "highcut": 20000,
    "modchar": "Smooth", "modrate": 0.6, "moddepth": 35, "modrandom": 50, "modsmooth": 60,
    "balance": 0, "crossfeed": 20, "decorr": 85, "irlength": 100,
}

# settings used for "MASTER SPACE test 2" (Track 19 LeadVox1), read from REAPER on 2026-11-10
LEADVOX = dict(algo="Studio Room", routing="Insert", depth=24, decay=0.69, size=34, predelay=0, erlevel=-5.9, latelevel=-4.0,
               diffusion=25, density=60, shape=35, spread=69.7697, width=100, mix=100, output=-3.6,
               locklevel=True, locktone=False, lockdecay=False, lockwidth=False,
               roomw=7.5, roomd=10, roomh=4.0, srcx=49, lisx=50, lisy=72,
               matfront="Plaster", matback="Wood", matleft="Plaster", matright="Plaster", matfloor="Wood", matceiling="Plaster",
               erdensity=60, erdiffusion=40, ercolor=0, erwidth=101, erstart=0, erstop=139.921, erpredelay=0,
               latedelay=30.8616, latewidth=100, latecolor=0,
               lfmult=1.06, lmmult=0.80, hmmult=0.87, hfmult=0.55, xlow=200, xmid=1500, xhigh=6000,
               modchar="Smooth", modrate=0.60, moddepth=35, modrandom=50, modsmooth=60, crossfeed=15, decorr=40)

# common 1.2 settings for rooms: open stereo field, natural high-frequency decay
OPEN = dict(decorr=85, crossfeed=15, lfmult=1.1, lmmult=1.0, hmmult=0.92, hfmult=0.78, moddepth=30, modrate=0.5)
AUX = dict(routing="Aux", mix=100)

P = []
def preset(name, category, description, *bases, **kw):
    merged = {}
    for b in bases: merged.update(b)
    merged.update(kw)
    P.append((name, category, description, merged))

# ---- Studio Rooms (Studio Room algorithm)
preset("Studio A Natural", "Studio Rooms",
       "Neutral tracking room, about 0.7 s. Plaster walls, wood floor and back wall. Open around the source.",
       AUX, OPEN, algo="Studio Room", decay=0.72, size=34, depth=30, erlevel=-4, latelevel=-4, diffusion=50, density=60,
       spread=40, latedelay=14, erstop=120, roomw=7.5, roomd=10, roomh=4)
preset("Wood Live Room", "Studio Rooms",
       "Larger live room with wooden walls and floor: warm, about 0.9 s.",
       AUX, OPEN, algo="Studio Room", decay=0.9, size=46, depth=40, erlevel=-3, latelevel=-4, diffusion=55, density=65,
       spread=50, latedelay=18, roomw=9, roomd=12, roomh=4.5, matfront="Wood", matback="Wood", matleft="Plaster", matright="Plaster",
       matfloor="Wood", matceiling="Wood", hfmult=0.74)
preset("Small Tight Room", "Studio Rooms",
       "Small treated room, about 0.4 s: presence without a tail.",
       AUX, OPEN, algo="Studio Room", decay=0.4, size=20, depth=22, erlevel=-2, latelevel=-8, diffusion=45, density=55,
       spread=25, latedelay=8, erstop=80, roomw=4.5, roomd=5.5, roomh=2.8, matfront="Wood", matback="Curtain", matleft="Plaster",
       matright="Plaster", matfloor="Wood", matceiling="Absorber", hfmult=0.8)
preset("Bright Plaster Room", "Studio Rooms",
       "Hard plaster room, about 0.6 s, with air on top.",
       AUX, OPEN, algo="Studio Room", decay=0.6, size=30, depth=30, erlevel=-3, latelevel=-5, diffusion=50, density=60,
       spread=35, latedelay=12, erstop=110, roomw=6, roomd=8, roomh=3.2, matfront="Plaster", matback="Plaster", matleft="Plaster",
       matright="Plaster", matfloor="Wood", matceiling="Plaster", hmmult=0.95, hfmult=0.9)

# ---- Chambers (Chamber algorithm)
preset("Plaster Chamber", "Chambers",
       "Classic plaster echo chamber, about 1.4 s: dense and smooth.",
       AUX, OPEN, algo="Chamber", decay=1.4, size=50, depth=40, erlevel=-5, latelevel=-2, diffusion=75, density=75,
       spread=60, latedelay=20, roomw=7.5, roomd=10, roomh=4, matfront="Plaster", matback="Plaster", matleft="Plaster",
       matright="Plaster", matfloor="Concrete", matceiling="Plaster", hfmult=0.8)
preset("Stone Chamber", "Chambers",
       "Concrete chamber, about 1.8 s: hard, bright early field.",
       AUX, OPEN, algo="Chamber", decay=1.8, size=45, depth=40, erlevel=-5, latelevel=-2, diffusion=70, density=75,
       spread=60, latedelay=22, roomw=6, roomd=8, roomh=5, matfront="Concrete", matback="Concrete", matleft="Concrete",
       matright="Concrete", matfloor="Concrete", matceiling="Concrete", hfmult=0.85)
preset("Echo Chamber Classic", "Chambers",
       "Bigger chamber with a short pre-delay, about 2.2 s: for vocals and strings.",
       AUX, OPEN, algo="Chamber", decay=2.2, size=58, depth=45, predelay=15, erlevel=-6, latelevel=-2, diffusion=80,
       density=78, spread=80, latedelay=25, roomw=8, roomd=11, roomh=4.5, matfront="Plaster", matback="Wood", matleft="Plaster",
       matright="Plaster", matfloor="Concrete", matceiling="Plaster", hfmult=0.72)

# ---- Halls (Hall algorithm)
preset("Concert Hall", "Halls",
       "Large concert hall, about 2.4 s, wood and plaster.",
       AUX, OPEN, algo="Hall", decay=2.4, size=60, depth=55, predelay=20, erlevel=-7, latelevel=-2, diffusion=80,
       density=80, spread=120, latedelay=40, roomw=22, roomd=34, roomh=14, matfront="Wood", matback="Plaster", matleft="Plaster",
       matright="Plaster", matfloor="Wood", matceiling="Plaster", lfmult=1.2, hfmult=0.65)
preset("Small Hall", "Halls",
       "Small hall, about 1.6 s: a hall sound that stays out of the way.",
       AUX, OPEN, algo="Hall", decay=1.6, size=45, depth=45, predelay=10, erlevel=-6, latelevel=-2, diffusion=75,
       density=75, spread=90, latedelay=28, roomw=14, roomd=20, roomh=8, matfront="Wood", matback="Plaster", matleft="Plaster",
       matright="Plaster", matfloor="Wood", matceiling="Plaster", hfmult=0.7)
preset("Cathedral Stone", "Halls",
       "Stone cathedral, about 4.5 s, long low end.",
       AUX, OPEN, algo="Hall", decay=4.5, size=80, depth=60, predelay=30, erlevel=-8, latelevel=-2, diffusion=85,
       density=85, spread=180, latedelay=60, roomw=18, roomd=40, roomh=18, matfront="Concrete", matback="Concrete",
       matleft="Concrete", matright="Glass", matfloor="Concrete", matceiling="Concrete", lfmult=1.3, hfmult=0.55)

# ---- Vocal Spaces
preset("LeadVox Studio Room (1.1 Legacy)", "Vocal Spaces",
       "The LeadVox1 settings of 'MASTER SPACE test 2' on the 1.1 engine: sounds exactly like MASTER SPACE 1.1.",
       LEADVOX, engine="1.1 Legacy")
preset("LeadVox Studio Room (1.2)", "Vocal Spaces",
       "The same LeadVox1 settings on the 1.2 engine: for A/B against the 1.1 Legacy version.",
       LEADVOX, engine="1.2")
preset("LeadVox Studio Room 1.2 Open", "Vocal Spaces",
       "LeadVox1 settings on 1.2 with the changes suggested by the reference comparison: decorrelation 85 %, "
       "longer HF decay, Late 1.5 dB lower (early / late balance of the reference).",
       LEADVOX, decorr=85, hfmult=0.8, hmmult=0.92, latelevel=-5.5, engine="1.2")
preset("Vocal Plaster Room", "Vocal Spaces",
       "Short plaster room with a little pre-delay, about 0.9 s: depth for a lead vocal.",
       AUX, OPEN, algo="Studio Room", decay=0.9, size=40, depth=35, predelay=8, erlevel=-5, latelevel=-3, diffusion=55,
       density=65, spread=45, latedelay=16, roomw=7, roomd=9, roomh=3.8, matfront="Plaster", matback="Wood", matleft="Plaster",
       matright="Plaster", matfloor="Wood", matceiling="Plaster", hfmult=0.82)
preset("Ballad Chamber", "Vocal Spaces",
       "Chamber with 25 ms pre-delay, about 1.9 s: long vocal tail that leaves the words clear.",
       AUX, OPEN, algo="Chamber", decay=1.9, size=55, depth=40, predelay=25, erlevel=-8, latelevel=-2, diffusion=80,
       density=78, spread=80, latedelay=24, lowcut=100, hfmult=0.75)
preset("Vocal Hall", "Vocal Spaces",
       "Hall for vocals, 35 ms pre-delay, about 2 s.",
       AUX, OPEN, algo="Hall", decay=2.0, size=55, depth=50, predelay=35, erlevel=-8, latelevel=-2, diffusion=80,
       density=80, spread=100, latedelay=32, roomw=16, roomd=24, roomh=10, lowcut=90, hfmult=0.7)

# ---- Drum Spaces
preset("Drum Room Tight", "Drum Spaces",
       "Tight drum room, about 0.5 s: punchy early reflections.",
       AUX, OPEN, algo="Studio Room", decay=0.5, size=30, depth=30, erlevel=0, latelevel=-6, diffusion=60, density=60,
       spread=30, latedelay=10, erstop=90, roomw=6, roomd=7, roomh=3.2, matfront="Plaster", matback="Wood", matleft="Plaster",
       matright="Plaster", matfloor="Wood", matceiling="Wood", hfmult=0.85)
preset("Live Drum Room", "Drum Spaces",
       "Big live room for drums, about 1 s.",
       AUX, OPEN, algo="Studio Room", decay=1.0, size=55, depth=45, erlevel=-2, latelevel=-3, diffusion=65, density=70,
       spread=50, latedelay=18, roomw=10, roomd=12, roomh=5, matfront="Wood", matback="Plaster", matleft="Concrete",
       matright="Plaster", matfloor="Wood", matceiling="Plaster", hfmult=0.8)
preset("Snare Chamber", "Drum Spaces",
       "Bright chamber for snare and toms, about 1.2 s.",
       AUX, OPEN, algo="Chamber", decay=1.2, size=40, depth=35, predelay=5, erlevel=-4, latelevel=-2, diffusion=75,
       density=80, spread=50, latedelay=16, roomw=6, roomd=8, roomh=4, matfront="Plaster", matback="Concrete", matleft="Plaster",
       matright="Plaster", matfloor="Concrete", matceiling="Plaster", hfmult=0.88)

# ---- Ambiences
preset("Ambience Wood", "Ambiences",
       "Short wooden ambience, about 0.35 s: glue without a tail.",
       AUX, OPEN, algo="Studio Room", decay=0.35, size=25, depth=25, erlevel=0, latelevel=-10, diffusion=50, density=55,
       spread=20, latedelay=6, erstop=60, roomw=5, roomd=6, roomh=3, matfront="Wood", matback="Wood", matleft="Wood",
       matright="Wood", matfloor="Wood", matceiling="Plaster", hfmult=0.8)
preset("Ambience Plaster", "Ambiences",
       "Short plaster ambience, about 0.45 s, a little brighter.",
       AUX, OPEN, algo="Studio Room", decay=0.45, size=28, depth=28, erlevel=0, latelevel=-9, diffusion=50, density=55,
       spread=24, latedelay=8, erstop=70, roomw=5.5, roomd=7, roomh=3, matfront="Plaster", matback="Plaster", matleft="Plaster",
       matright="Plaster", matfloor="Wood", matceiling="Plaster", hfmult=0.9)

# ---- Early Reflections (late field off; DEPTH WITHOUT REVERB, on the channel)
preset("ER Studio Depth", "Early Reflections",
       "Early reflections only, on the channel: moves the source back in a studio room without adding a tail.",
       OPEN, algo="Studio Room", routing="Insert", mix=55, lateon=False, depth=45, erlevel=0, decay=0.7, size=34,
       roomw=7.5, roomd=10, roomh=4, erstop=120)
preset("ER Stage", "Early Reflections",
       "Early reflections of a stage, on the channel: depth and width without a tail.",
       OPEN, algo="Hall", routing="Insert", mix=55, lateon=False, depth=55, erlevel=0, decay=1.5, size=50,
       roomw=12, roomd=16, roomh=8, erstop=160, matfront="Wood", matback="Plaster", matfloor="Wood")

# ---- Creative Spaces
preset("Glass Room", "Creative Spaces",
       "Glass walls, about 1.3 s: glassy, bright early field.",
       AUX, OPEN, algo="Chamber", decay=1.3, size=40, depth=40, erlevel=-3, latelevel=-3, diffusion=60, density=65,
       spread=50, latedelay=16, roomw=8, roomd=10, roomh=3.5, matfront="Glass", matback="Glass", matleft="Glass", matright="Glass",
       matfloor="Concrete", matceiling="Plaster", hfmult=1.0, latecolor=30)
preset("Concrete Tunnel", "Creative Spaces",
       "Long narrow concrete space, about 2.8 s.",
       AUX, OPEN, algo="Hall", decay=2.8, size=55, depth=60, erlevel=-4, latelevel=-2, diffusion=70, density=70,
       spread=120, latedelay=40, roomw=4, roomd=40, roomh=4, matfront="Concrete", matback="Concrete", matleft="Concrete",
       matright="Concrete", matfloor="Concrete", matceiling="Concrete", hfmult=0.7)
preset("Far Away", "Creative Spaces",
       "Source at the back of a hall, band-limited: distant, behind the mix.",
       AUX, OPEN, algo="Hall", decay=3.0, size=65, depth=95, erlevel=-4, latelevel=-1, diffusion=85, density=85,
       spread=150, latedelay=45, roomw=18, roomd=28, roomh=12, lowcut=200, highcut=6000, hfmult=0.6)


# ---- ATMOSPHERIC TAIL (experimental build only: presets/factory-atmos)
ATMOS_P = []
ATM = dict(tail="Atmospheric Tail", charlock=True, routing="Aux", mix=100, erlevel=-12, latelevel=0, size=50,
           lfmult=0.9, lmmult=1.0, hmmult=1.0, hfmult=1.0, xlow=250, xmid=2000, xhigh=7000,
           decorr=85, crossfeed=15, modrate=0.6, moddepth=35, modrandom=50, modsmooth=60, latedelay=12, diffusion=70, density=70,
           shape=35, spread=60)
def atmos(name, category, description, **kw):
    merged = dict(ATM); merged.update(kw)
    ATMOS_P.append((name, category, description, merged))

atmos("ATMOS Vocal Glow", "Vocal Spaces", "Atmospheric Tail, 1.6 s: soft, moving air around a lead vocal.",
      decay=1.6, bloom=25, motion=50, air=55, lowcut=90)
atmos("ATMOS Smoky Ballad", "Vocal Spaces", "Atmospheric Tail, 2.4 s with 20 ms pre-delay: warm, smoky vocal tail.",
      decay=2.4, bloom=35, motion=60, air=45, density=75, predelay=20, erlevel=-14, lowcut=90)
atmos("ATMOS Sax Haze", "Vocal Spaces", "Atmospheric Tail, 2.8 s: lush motion for sax and solo instruments.",
      decay=2.8, bloom=30, motion=65, air=50, density=75, lowcut=80)
atmos("ATMOS Wide Hall", "Halls", "Atmospheric Tail, 3 s: wide, transparent hall-like space.",
      decay=3.0, bloom=30, motion=50, air=55, size=65)
atmos("ATMOS Floating Hall", "Halls", "Atmospheric Tail, 6 s: the tail opens up and floats.",
      decay=6.0, bloom=50, motion=60, air=55, shape=45, spread=100)
atmos("ATMOS Soft Ambience", "Ambiences", "Atmospheric Tail, 1 s: short, dense and soft.",
      decay=1.0, bloom=10, motion=35, air=60, density=80, spread=30)
atmos("ATMOS Drum Bloom", "Drum Spaces", "Atmospheric Tail, 1.4 s: dense bloom behind drums.",
      decay=1.4, bloom=40, motion=30, air=50, density=85, diffusion=80)
atmos("ATMOS Cinematic 12 s", "Creative Spaces", "Atmospheric Tail, 12 s, late engine only: huge cinematic space.",
      decay=12.0, bloom=70, motion=65, air=60, shape=55, spread=140, eron=False)
atmos("ATMOS Late Only 20 s", "Creative Spaces", "Atmospheric Tail, 20 s, early reflections off: long, soft atmosphere.",
      decay=20.0, bloom=60, motion=55, air=55, shape=50, spread=160, eron=False)
atmos("ATMOS Endless 30 s", "Creative Spaces", "Atmospheric Tail, 30 s, late engine only: endless but controlled.",
      decay=30.0, bloom=80, motion=55, air=50, shape=60, spread=180, eron=False)


def slug(s):
    return re.sub(r"[^A-Za-z0-9]+", "_", s).strip("_")


def build(trims):
    out = []
    for name, cat, desc, kw in P:
        params = dict(DEFAULTS)
        params.update(kw)
        if name in trims:
            params["output"] = round(params.get("output", 0) + trims[name], 1)
        doc = {"format": "MASTER SPACE Preset", "schemaVersion": 1, "pluginVersion": "1.2.0", "name": name, "category": cat,
               "author": "masterastakhovsound", "description": desc, "engine": params["engine"], "parameters": params}
        out.append(doc)
    return out


def write(folder, docs, prefix=""):
    os.makedirs(folder, exist_ok=True)
    for f in os.listdir(folder):
        if f.endswith(".json"): os.remove(os.path.join(folder, f))
    for i, doc in enumerate(docs):
        with open(os.path.join(folder, f"{prefix}{i:02d}_{slug(doc['name'])}.json"), "w") as fh:
            json.dump(doc, fh, indent=2, ensure_ascii=False)
            fh.write("\n")


def build_atmos(trims):
    out = []
    for name, cat, desc, kw in ATMOS_P:
        params = dict(DEFAULTS); params.update(kw)
        params["output"] = round(params.get("output", 0) + trims.get(name, 0.0), 1)
        out.append({"format": "MASTER SPACE Preset", "schemaVersion": 1, "pluginVersion": "1.3.0",
                    "name": name, "category": cat, "author": "masterastakhovsound", "description": desc,
                    "engine": "1.2", "dspVersion": "1.3.0 (engine 1.2, ATMOSPHERIC TAIL prototype)", "algorithm": "Atmospheric Tail",
                    "parameters": params})
    return out


if __name__ == "__main__":
    here = os.path.dirname(__file__)
    trims = json.load(open(sys.argv[1])) if len(sys.argv) > 1 and sys.argv[1] != "-" else {}
    trims_atmos = json.load(open(sys.argv[2])) if len(sys.argv) > 2 else {}
    write(os.path.join(here, "..", "presets", "factory"), build(trims))
    write(os.path.join(here, "..", "presets", "factory-atmos"), build_atmos(trims_atmos), "A")
    print(len(P), "presets +", len(ATMOS_P), "ATMOS presets written")
