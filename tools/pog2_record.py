"""Plays the POG2 test signal out of the Apollo and records the pedal back, run by run (docs/POG2_TEST_PLAN.md).

Needs: pip install sounddevice    (uses the Apollo's ASIO driver; close REAPER first, ASIO serves one program)

    python tools/pog2_record.py --list                 the audio devices (find the Apollo's ASIO entry)
    python tools/pog2_record.py --ping 3               a short beep on output channel 3: which jack is it?
    python tools/pog2_record.py --meter                1 kHz tone out, the input level live: set the Hi-Z gain / unity
    python tools/pog2_record.py --run A1               play + record one run -> measurements/pog2/A1.wav
    python tools/pog2_record.py --plan                 every run in order: shows what to set, Enter records it

Options: --out 3 4 (output channels, 1-based; the jack into the pedal), --in 1 (the input the pedal comes back on),
--gain -12 (playback level in dB: the test file is at -18 dBFS, a line out is much hotter than a guitar).
"""
import argparse, json, os, sys, time, wave
os.environ.setdefault("SD_ENABLE_ASIO", "1")      # sounddevice: use the PortAudio build with ASIO
import numpy as np
try:
    import sounddevice as sd
except ImportError:
    sys.exit("sounddevice is not installed: pip install sounddevice")

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "measurements")
SR = 48000

RUNS = [
    ("R0", "Pedal BYPASSED (footswitch LED off). The interface chain itself."),
    ("A1", "Base: DRY OUTPUT at unity (tape mark), octave sliders down, DRY FX lit, ATTACK down, LP FILTER up, Q off, DETUNE down."),
    ("A2", "Base, but DRY FX OFF and ATTACK at 1/2."),
    ("B1", "Base, ATTACK at 1/8."), ("B2", "Base, ATTACK at 1/4."), ("B3", "Base, ATTACK at 3/8."),
    ("B4", "Base, ATTACK at 1/2."), ("B5", "Base, ATTACK at 3/4."), ("B6", "Base, ATTACK at the top."),
    ("C1", "Base, LP FILTER at the bottom."), ("C2", "Base, LP FILTER at 1/4."), ("C3", "Base, LP FILTER at 1/2."),
    ("C4", "Base, LP FILTER at 3/4."), ("C5", "Base, LP FILTER at the top."),
    ("C6", "Base, Q ON, LP FILTER at the bottom."), ("C7", "Base, Q ON, LP FILTER at 1/4."), ("C8", "Base, Q ON, LP FILTER at 1/2."),
    ("C9", "Base, Q ON, LP FILTER at 3/4."), ("C10", "Base, Q ON, LP FILTER at the top."),
    ("D1", "Base, ATTACK at 1/2 and LP FILTER at 1/2."), ("D2", "Base, ATTACK 1/2, LP FILTER 1/2, Q ON."),
    ("E1", "Base, DETUNE at 1/4."), ("E2", "Base, DETUNE at 1/2."), ("E3", "Base, DETUNE at the top."),
    ("F1", "DRY OUTPUT down, -2 OCTAVES at unity, everything else Base."), ("F2", "DRY OUTPUT down, -1 OCTAVE at unity."),
    ("F3", "DRY OUTPUT down, +1 OCTAVE at unity."), ("F4", "DRY OUTPUT down, +2 OCTAVES at unity."),
    ("G1", "Your favourite POG2 sound #1."), ("G2", "Your favourite POG2 sound #2."), ("G3", "Your favourite POG2 sound #3."),
]


def find_device(name):
    for i, d in enumerate(sd.query_devices()):
        api = sd.query_hostapis(d["hostapi"])["name"]
        if name.lower() in d["name"].lower() and "ASIO" in api and d["max_input_channels"] > 0 and d["max_output_channels"] > 0:
            return i
    sys.exit("No ASIO device matching '%s' (try --list, and close REAPER)" % name)


def read_wav(path):
    with wave.open(path, "rb") as wf:
        n, w = wf.getnframes(), wf.getsampwidth()
        raw = wf.readframes(n)
    b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
    x = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
    x = np.where(x >= 2 ** 23, x - 2 ** 24, x)
    return (x / (2 ** 23 - 1)).astype(np.float32)


def write_wav(path, x):
    pcm = np.round(np.clip(x, -1, 1 - 2 ** -23) * (2 ** 23 - 1)).astype("<i4").tobytes()
    b24 = b"".join(pcm[i:i + 3] for i in range(0, len(pcm), 4))
    with wave.open(path, "wb") as wf:
        wf.setnchannels(1); wf.setsampwidth(3); wf.setframerate(SR); wf.writeframes(b24)


def out_block(sig, n_out):
    return np.repeat(sig[:, None], n_out, axis=1).astype(np.float32)


def report(rec, name):
    peak = 20 * np.log10(np.max(np.abs(rec)) + 1e-12)
    cues = json.load(open(os.path.join(ROOT, "pog2_cues.json")))["sections"]
    cal = next(c for c in cues if c["name"] == "calib_1k")
    seg = rec[int((cal["start"] + 0.5) * SR):int((cal["start"] + cal["length"] - 0.5) * SR)]
    tone = 20 * np.log10(np.sqrt(np.mean(seg ** 2)) * np.sqrt(2) + 1e-12)
    print("  %s: peak %.1f dBFS, 1 kHz tone %.1f dBFS%s" % (name, peak, tone,
          "   CLIPPED: turn the Hi-Z gain down and redo" if peak > -0.5 else "   very quiet: check the cables / gain" if peak < -40 else ""))


def record(dev, name, gain_db, outs, inp):
    sig = read_wav(os.path.join(ROOT, "pog2_test_signals.wav")) * 10 ** (gain_db / 20)
    sig = np.concatenate([np.zeros(SR // 2, np.float32), sig, np.zeros(SR, np.float32)])
    print("  recording %s (%.0f s)..." % (name, len(sig) / SR), flush=True)
    rec = sd.playrec(out_block(sig, len(outs)), SR, input_mapping=[inp], output_mapping=outs, device=(dev, dev), dtype="float32")
    sd.wait()
    os.makedirs(os.path.join(ROOT, "pog2"), exist_ok=True)
    write_wav(os.path.join(ROOT, "pog2", name + ".wav"), rec[:, 0])
    report(rec[:, 0], name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--device", default="Universal Audio")
    ap.add_argument("--out", type=int, nargs="+", default=[3])
    ap.add_argument("--in", dest="inp", type=int, default=1)
    ap.add_argument("--gain", type=float, default=-12.0)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--ping", type=int)
    ap.add_argument("--meter", action="store_true")
    ap.add_argument("--run")
    ap.add_argument("--plan", action="store_true")
    a = ap.parse_args()
    if a.list:
        for i, d in enumerate(sd.query_devices()):
            print("%3d  %-50s %-22s in %2d  out %2d" % (i, d["name"], sd.query_hostapis(d["hostapi"])["name"], d["max_input_channels"], d["max_output_channels"]))
        return
    dev = find_device(a.device)
    if a.ping:
        t = np.arange(int(0.4 * SR)) / SR
        sd.play(out_block((np.sin(2 * np.pi * 660 * t) * 0.05).astype(np.float32), 1), SR, mapping=[a.ping], device=dev); sd.wait()
        return
    if a.meter:
        t = np.arange(SR * 30) / SR
        tone = out_block((np.sin(2 * np.pi * 1000 * t) * 10 ** ((-18 + a.gain) / 20)).astype(np.float32), len(a.out))
        print("1 kHz tone out for 30 s; input level (aim for about -18 dBFS):")
        def cb(indata, frames, ti, status):
            print("\r  in %6.1f dBFS " % (20 * np.log10(np.max(np.abs(indata)) + 1e-12)), end="", flush=True)
        with sd.InputStream(device=dev, channels=a.inp, samplerate=SR, callback=lambda i, f, t2, s: cb(i[:, -1:], f, t2, s)):
            sd.play(tone, SR, mapping=a.out, device=dev); sd.wait()
        print()
        return
    if a.run:
        record(dev, a.run, a.gain, a.out, a.inp)
        return
    if a.plan:
        for name, what in RUNS:
            done = os.path.exists(os.path.join(ROOT, "pog2", name + ".wav"))
            ans = input("\n%s%s: %s\n  Enter = record, s = skip, q = quit > " % (name, " (already recorded)" if done else "", what)).strip().lower()
            if ans == "q": break
            if ans == "s": continue
            record(dev, name, a.gain, a.out, a.inp)
        return
    ap.print_help()


if __name__ == "__main__":
    main()
