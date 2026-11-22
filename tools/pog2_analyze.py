"""Measures the POG2 recordings (measurements/pog2/*.wav, see docs/POG2_TEST_PLAN.md) and prints what PolySwell needs.

    python tools/pog2_analyze.py            -> report on stdout and measurements/pog2_results.json

Every run is the same test file played through the pedal, lined up by the round trip measured on R0 plus the pedal's
own delay measured on A1, so a section of one run can be compared with the same section of another.
"""
import json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pog2_record import read_wav

SR = 48000
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "measurements")
CUES = {c["name"]: c for c in json.load(open(os.path.join(ROOT, "pog2_cues.json")))["sections"]}
PAD = 0.5                                   # the recorder plays 0.5 s of silence first
ATTACK_POS = {"A1": 0.0, "B1": 1 / 8, "B2": 2 / 8, "B3": 3 / 8, "B4": 4 / 8, "B5": 6 / 8, "B6": 1.0}
FILTER_POS = {"C1": 0.0, "C2": 2 / 8, "C3": 4 / 8, "C4": 6 / 8, "A1": 1.0}
DETUNE_POS = {"A1": 0.0, "E1": 3 / 8, "E2": 4 / 8, "E3": 1.0}
NOTES = ["A2", "E3", "A3", "A4", "E5"]


def load(name):
    p = os.path.join(ROOT, "pog2", name + ".wav")
    return read_wav(p) if os.path.exists(p) else None


def click_time(x):
    st = CUES["sync_start"]["start"] + PAD + 0.25
    a, b = int((st - 0.01) * SR), int((st + 0.2) * SR)
    seg = np.abs(x[a:b])
    return (a + int(np.argmax(seg > 0.1 * seg.max()))) / SR - st


def envelope(x, t0, dur, win=0.010, hop=0.0025):
    a = int(t0 * SR); n = int(dur * SR); w = int(win * SR); h = int(hop * SR)
    seg = x[a:a + n + w]
    e = np.array([np.sqrt(np.mean(seg[i:i + w] ** 2)) for i in range(0, n, h)])
    return np.arange(len(e)) * hop + win / 2, e


def fit_swell(t, g):
    """g(t) ~ clamp((t - d) / T, 0, 1) ** p   (amplitude, normalised to the settled level)."""
    best = (1e9, 0, 0, 0)
    for d in np.arange(0.0, 0.031, 0.0025):
        for T in np.exp(np.linspace(np.log(0.015), np.log(8.0), 160)):
            u = np.clip((t - d) / T, 0, 1)
            for p in np.arange(0.4, 3.01, 0.05):
                m = u ** p
                err = np.mean((np.sqrt(np.maximum(g, 0)) - np.sqrt(m)) ** 2)   # sqrt: weighs the quiet start too
                if err < best[0]: best = (err, d, T, p)
    return best


def main():
    r0, a1 = load("R0"), load("A1")
    rt = click_time(r0)
    lat = click_time(a1) - rt
    res = {"round_trip_s": rt, "pedal_delay_s": lat}
    print("round trip %.1f ms, POG2 delay (DRY FX amber) %.1f ms" % (rt * 1000, lat * 1000))
    off = PAD + rt + lat                     # where a section's input reaches the output, in a recording

    # ---- settled level per held note, from A1 (ATTACK 0): every swell is normalised to it
    ref = {}
    for n in NOTES:
        c = CUES["steady_" + n]
        _, e = envelope(a1, c["start"] + off, 2.9)
        ref[n] = np.median(e[int(1.5 / 0.0025):int(2.7 / 0.0025)])

    # ---- ATTACK: swell curve per run, fitted
    print("\nATTACK (held tones, normalised to A1's settled level; fit g = clamp((t-d)/T)^p):")
    res["attack"] = {}
    for run, pos in ATTACK_POS.items():
        x = load(run)
        if x is None: continue
        ts, gs = [], []
        for n in NOTES:
            c = CUES["steady_" + n]
            t, e = envelope(x, c["start"] + off, 2.9)
            ts.append(t); gs.append(e / ref[n])
        t = ts[0]; g = np.median(np.array(gs), axis=0)
        err, d, T, p = fit_swell(t, g)
        settled = float(np.median(g[int(2.4 / 0.0025):]))
        t50 = float(t[np.argmax(g > 0.5)]) if (g > 0.5).any() else float("nan")
        t90 = float(t[np.argmax(g > 0.9)]) if (g > 0.9).any() else float("nan")
        res["attack"][run] = dict(pos=pos, T=T, p=p, d=d, t50=t50, t90=t90, level_at_2_8s=settled, rms_err=float(np.sqrt(err)))
        print("  %s slider %.3f: T %6.3f s  p %.2f  dead %2.0f ms | 50%% %.3f s  90%% %.3f s | level at 2.8 s %.2f | fit err %.3f"
              % (run, pos, T, p, d * 1000, t50, t90, settled, np.sqrt(err)))

    # ---- does the swell depend on how hard a note is played?  (B4, plucks at six levels)
    b4 = load("B4")
    if b4 is not None:
        print("\nPlaying level (B4, plucked A3 at six levels; time to reach 50 % of its own peak):")
        res["level_dependence"] = {}
        for lv in (36, 30, 24, 18, 12, 6):
            c = CUES["dyn_A3_%d" % lv]
            t, e = envelope(b4, c["start"] + off, 2.5)
            t50 = float(t[np.argmax(e > 0.5 * e.max())]); tp = float(t[np.argmax(e)])
            res["level_dependence"][lv] = dict(t50=t50, tpeak=tp)
            print("  -%2d dBFS: 50%% at %.3f s, peak at %.3f s" % (lv, t50, tp))

    # ---- polyphony: does a new note disturb a ringing one?  (B4: A2 held, E4 joins at 1.5 s)
    if b4 is not None:
        c = CUES["poly_A2_then_E4"]
        a = int((c["start"] + off) * SR); seg = b4[a:a + int(6.0 * SR)]
        def band_level(f0, t):
            i = int(t * SR); s = seg[i:i + 9600] * np.hanning(9600)
            X = np.abs(np.fft.rfft(s)); f = np.fft.rfftfreq(9600, 1 / SR); k = np.argmin(abs(f - f0))
            return 20 * np.log10(X[k - 2:k + 3].max() + 1e-12)
        a2 = [band_level(110.0, t) for t in (1.2, 1.6, 1.8, 2.2, 3.0)]
        e4 = [band_level(329.63, t) for t in (1.6, 1.8, 2.2, 3.0)]
        res["polyphony"] = dict(A2_db=a2, E4_db=e4)
        print("\nPolyphony (B4): A2 level at 1.2/1.6/1.8/2.2/3.0 s: %s dB; E4 (joins at 1.5 s) at 1.6/1.8/2.2/3.0 s: %s dB"
              % (" ".join("%.1f" % v for v in a2), " ".join("%.1f" % v for v in e4)))

    # ---- re-picks (B4)
    if b4 is not None:
        print("\nRe-picks (B4): level of each of four picks, 125..1000 ms apart, relative to the first pick's peak:")
        res["repick"] = {}
        for gap in (1000, 500, 250, 125):
            c = CUES["repick_%dms" % gap]
            t, e = envelope(b4, c["start"] + off, 4 * gap / 1000 + 1.0)
            pk = []
            for k in range(4):
                w = (t >= k * gap / 1000) & (t < (k + 1) * gap / 1000)
                pk.append(float(e[w].max()) if w.any() else 0.0)
            rel = [20 * np.log10(v / pk[0] + 1e-9) for v in pk]
            res["repick"][gap] = rel
            print("  %4d ms apart: %s dB" % (gap, " ".join("%+.1f" % v for v in rel)))

    # ---- LP FILTER: response from pink noise, against A1
    print("\nLP FILTER (pink noise against A1): -3 dB point, slope between -6 and -30 dB:")
    res["filter"] = {}
    c = CUES["pink_noise"]; a = int((c["start"] + off + 1.0) * SR); b = a + int(4.0 * SR)
    def spec(x):
        P = np.abs(np.fft.rfft(x[a:b] * np.hanning(b - a))) ** 2; f = np.fft.rfftfreq(b - a, 1 / SR)
        edges = 40 * 2 ** (np.arange(0, 9.0, 1 / 12)); out = []
        for lo, hi in zip(edges[:-1], edges[1:]): out.append(P[(f >= lo) & (f < hi)].sum())
        return np.sqrt(edges[:-1] * edges[1:]), np.array(out)
    fc_, P0 = spec(a1)
    for run, pos in FILTER_POS.items():
        if run == "A1": continue
        x = load(run)
        if x is None: continue
        _, P = spec(x); H = 10 * np.log10(P / P0 + 1e-20)
        i3 = int(np.argmax(H < -3)); f3 = float(fc_[i3])
        i6, i30 = int(np.argmax(H < -6)), int(np.argmax(H < -30))
        slope = (H[i30] - H[i6]) / np.log2(fc_[i30] / fc_[i6]) if i30 > i6 else float("nan")
        res["filter"][run] = dict(pos=pos, f3=f3, slope_db_oct=float(slope))
        print("  %s slider %.2f: -3 dB at %6.0f Hz, slope %.1f dB/oct" % (run, pos, f3, slope))

    # ---- does the filter move during the swell?  (D1 vs C3)
    d1, c3 = load("D1"), load("C3")
    if d1 is not None and c3 is not None:
        c = CUES["buzz_55Hz"]; st = c["start"] + off
        def bright(x, t):
            i = int((st + t) * SR); s = x[i:i + 4096] * np.hanning(4096)
            P = np.abs(np.fft.rfft(s)) ** 2; f = np.fft.rfftfreq(4096, 1 / SR)
            return 10 * np.log10(P[(f > 900) & (f < 2000)].sum() / P[(f > 100) & (f < 400)].sum())
        times = (0.03, 0.06, 0.1, 0.15, 0.2, 0.3, 0.5, 1.0, 2.0)
        bd, bc = [bright(d1, t) for t in times], [bright(c3, t) for t in times]
        res["filter_during_swell"] = dict(times=times, D1=bd, C3=bc)
        print("\nFilter during the swell (55 Hz buzz, 0.9-2 kHz vs 100-400 Hz):")
        print("  time  " + " ".join("%6.2f" % v for v in times))
        print("  D1    " + " ".join("%6.1f" % v for v in bd))
        print("  C3    " + " ".join("%6.1f" % v for v in bc))

    # ---- DETUNE: components around a 440 Hz sine
    print("\nDETUNE (440 Hz sine): level of the original and of the copies, relative to the loudest:")
    res["detune"] = {}
    c = CUES["sine_440"]; a = int((c["start"] + off + 0.3) * SR); b = a + int(4.2 * SR)
    for run, pos in DETUNE_POS.items():
        x = load(run)
        if x is None: continue
        N = 1 << 20; X = np.abs(np.fft.rfft(x[a:b] * np.hanning(b - a), N)); f = np.fft.rfftfreq(N, 1 / SR)
        def at(fr): k = np.argmin(abs(f - fr)); return X[k - 3:k + 4].max()
        orig, dn, up = at(440.0), at(440 * 2 ** (-10.1 / 1200)), at(440 * 2 ** (10.1 / 1200))
        pk = max(orig, dn, up)
        res["detune"][run] = dict(pos=pos, orig_db=20 * np.log10(orig / pk), down_db=20 * np.log10(dn / pk), up_db=20 * np.log10(up / pk))
        print("  %s slider %.2f: original %6.1f dB, -10 c %6.1f dB, +10 c %6.1f dB" % (run, pos, *[20 * np.log10(v / pk) for v in (orig, dn, up)]))

    json.dump(res, open(os.path.join(ROOT, "pog2_results.json"), "w"), indent=1, default=float)


if __name__ == "__main__":
    main()
