"""PolySwell against the POG2: the same test file, each against its own neutral reference.

    ui_snapshot --render measurements/pog2_test_signals.wav measurements/render/pog2_swell.wav "POG2 swell"
    ui_snapshot --render measurements/pog2_test_signals.wav measurements/render/init.wav "Init" attack=0
    python tools/pog2_compare.py [G1 pog2_swell]     -> numbers, and shots/pog2_compare.png

POG2 run (and its reference A1) vs PolySwell render (and its reference init): swell on held and plucked notes, tone
(pink noise) and detune (440 Hz sine).
"""
import json, os, sys
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pog2_record import read_wav

SR = 48000
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "measurements")
CUES = {c["name"]: c for c in json.load(open(os.path.join(ROOT, "pog2_cues.json")))["sections"]}
res = json.load(open(os.path.join(ROOT, "pog2_results.json")))
OFF_POG = 0.5 + res["round_trip_s"] + res["pedal_delay_s"]
OFF_PS = 0.5 + 1024 / SR


def env(x, off, name, dur, win=0.15):
    c = CUES[name]; a = int((c["start"] + off) * SR); w = int(win * SR); h = int(0.01 * SR)
    seg = x[a:a + int(dur * SR) + w]
    return np.arange(0, int(dur * SR), h) / SR + win / 2, np.array([np.sqrt(np.mean(seg[i:i + w] ** 2)) for i in range(0, int(dur * SR), h)])


def main():
    run = sys.argv[1] if len(sys.argv) > 1 else "G1"
    rend = sys.argv[2] if len(sys.argv) > 2 else "pog2_swell"
    pog, pog0 = read_wav(os.path.join(ROOT, "pog2", run + ".wav")), read_wav(os.path.join(ROOT, "pog2", "A1.wav"))
    ps, ps0 = read_wav(os.path.join(ROOT, "render", rend + ".wav")), read_wav(os.path.join(ROOT, "render", "init.wav"))
    curves = {}
    print("level against the plain guitar (dB), POG2 %s | PolySwell %s:" % (run, rend))
    for sec, dur, times in (("steady_A3", 2.9, (0.1, 0.25, 0.5, 1.0, 2.0, 2.8)), ("pluck_A3", 3.9, (0.1, 0.25, 0.5, 1.0, 2.0, 3.0))):
        out = {}
        for nm, x, x0, off in (("POG2", pog, pog0, OFF_POG), ("PolySwell", ps, ps0, OFF_PS)):
            t, e = env(x, off, sec, dur); _, e0 = env(x0, off, sec, dur)
            refl = np.median(e0[int(1.5 / 0.01):int(2.5 / 0.01)]) if sec.startswith("steady") else e0.max()
            db = 20 * np.log10(e / refl + 1e-9)
            out[nm] = (t, db)
        curves[sec] = out
        print("  %-10s t: %s" % (sec, " ".join("%6.2f" % v for v in times)))
        for nm, (t, db) in out.items():
            print("  %-10s    %s" % (nm, " ".join("%6.1f" % db[np.argmin(abs(t - v))] for v in times)))

    c = CUES["pink_noise"]
    def spec(x, off):
        a = int((c["start"] + off + 1.5) * SR); b = a + int(3.5 * SR)
        P = np.abs(np.fft.rfft(x[a:b] * np.hanning(b - a))) ** 2; f = np.fft.rfftfreq(b - a, 1 / SR)
        return f, P
    fcs = [100, 200, 300, 500, 700, 1000, 1500, 2000, 3000, 4000, 6000]
    print("\ntone, pink noise against the plain guitar (dB):")
    print("  Hz        " + " ".join("%6d" % v for v in fcs))
    for nm, x, x0, off in (("POG2", pog, pog0, OFF_POG), ("PolySwell", ps, ps0, OFF_PS)):
        f, P = spec(x, off); _, P0 = spec(x0, off)
        print("  %-9s " % nm + " ".join("%6.1f" % (10 * np.log10(P[(f > v / 1.12) & (f < v * 1.12)].sum() / P0[(f > v / 1.12) & (f < v * 1.12)].sum())) for v in fcs))

    c = CUES["sine_440"]
    print("\ndetune, 440 Hz sine: original, -10 c and +10 c copies, relative to the loudest:")
    for nm, x, off in (("POG2", pog, OFF_POG), ("PolySwell", ps, OFF_PS)):
        a = int((c["start"] + off + 0.5) * SR); b = a + int(4.0 * SR)
        N = 1 << 20; X = np.abs(np.fft.rfft(x[a:b] * np.hanning(b - a), N)); f = np.fft.rfftfreq(N, 1 / SR)
        lv = [X[np.argmin(abs(f - 440 * 2 ** (cc / 1200))) - 3:np.argmin(abs(f - 440 * 2 ** (cc / 1200))) + 4].max() for cc in (0, -10.1, 10.1)]
        print("  %-9s %s" % (nm, "  ".join("%6.1f dB" % (20 * np.log10(v / max(lv))) for v in lv)))

    # picture: the two swell curves
    W, H = 900, 260
    img = Image.new("RGB", (W, 2 * H), "white"); d = ImageDraw.Draw(img)
    for k, sec in enumerate(curves):
        y0 = k * H
        d.text((6, y0 + 4), "%s  (dB against the plain guitar, -40..+15; black POG2 %s, orange PolySwell)" % (sec, run), fill=(0, 0, 0))
        for db in range(-40, 16, 5):
            y = y0 + H - 12 - (db + 40) / 55 * (H - 30); d.line([(0, y), (W, y)], fill=(225, 225, 225) if db else (160, 160, 160))
        for nm, colr in (("POG2", (0, 0, 0)), ("PolySwell", (235, 120, 0))):
            t, db = curves[sec][nm]; T = t[-1]
            d.line([(v / T * W, y0 + H - 12 - (min(max(u, -40), 15) + 40) / 55 * (H - 30)) for v, u in zip(t, db)], fill=colr, width=2)
    os.makedirs(os.path.join(ROOT, "..", "shots"), exist_ok=True)
    img.save(os.path.join(ROOT, "..", "shots", "pog2_compare.png"))


if __name__ == "__main__":
    main()
