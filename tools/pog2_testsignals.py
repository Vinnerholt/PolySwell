"""Writes the test signal for measuring the EHX POG2 (see docs/POG2_TEST_PLAN.md).

    python tools/pog2_testsignals.py            -> measurements/pog2_test_signals.wav (48 kHz, 24-bit, mono, ~2.5 min)
                                                   measurements/pog2_cues.json       (where each section starts)

Every run through the pedal plays this same file, so recordings can be lined up sample-exactly (clicks at both ends)
and compared section by section. Synthetic notes, not a guitar, so the input is known exactly.
"""
import json, os, wave
import numpy as np

SR = 48000
rng = np.random.default_rng(7)
out, cues = [], []
t_now = 0.0


def db(x):
    return 10 ** (x / 20)


def add(name, sig, **info):
    global t_now
    cues.append(dict(name=name, start=round(t_now, 4), length=round(len(sig) / SR, 4), **info))
    out.append(sig.astype(np.float64))
    t_now += len(sig) / SR


def silence(sec, name="silence"):
    add(name, np.zeros(int(sec * SR)))


def tt(sec):
    return np.arange(int(sec * SR)) / SR


def fade(sig, fin=0.005, fout=0.02):
    n_in, n_out = int(fin * SR), int(fout * SR)
    sig = sig.copy()
    if n_in: sig[:n_in] *= np.linspace(0, 1, n_in)
    if n_out: sig[-n_out:] *= np.linspace(1, 0, n_out)
    return sig


def steady(f, sec, peak_db=-18, harmonics=8):
    """A held, non-decaying harmonic tone with a sharp (5 ms) start: shows the pedal's attack without string decay."""
    t = tt(sec)
    s = sum(np.sin(2 * np.pi * f * h * t) / h for h in range(1, harmonics + 1) if f * h < 16000)
    s = fade(s)
    return s / np.max(np.abs(s)) * db(peak_db)


def pluck(f, sec, peak_db=-18):
    """A plucked-string-like note: 12 decaying harmonics (upper ones faster), slight inharmonicity, a 3 ms pick click."""
    t = tt(sec)
    s = np.zeros_like(t)
    for h in range(1, 13):
        fh = f * h * np.sqrt(1 + 0.00008 * h * h)
        if fh > 16000: break
        s += np.sin(2 * np.pi * fh * t + rng.uniform(0, 2 * np.pi)) / h * np.exp(-t * (0.9 + 0.45 * h))
    click = rng.standard_normal(int(0.003 * SR)) * np.linspace(1, 0, int(0.003 * SR)) * 0.15
    s[:len(click)] += click
    s = fade(s, 0.0005, 0.02)
    return s / np.max(np.abs(s)) * db(peak_db)


def clicks():
    c = np.zeros(int(2.0 * SR))
    for k in (0.25, 0.75, 1.25):
        c[int(k * SR)] = 0.5
    return c


NOTES = [("E2", 82.41), ("A2", 110.0), ("E3", 164.81), ("A3", 220.0), ("A4", 440.0), ("E5", 659.26)]

# 0 alignment and level
silence(1.0)
add("sync_start", clicks(), note="3 single-sample clicks 0.5 s apart")
add("calib_1k", fade(np.sin(2 * np.pi * 1000 * tt(3.0)) * db(-18)), level_dbfs=-18)
silence(1.0)
silence(2.0, "noise_floor")

# 1 held tones: the attack curve without string decay
for n, f in NOTES:
    add("steady_" + n, steady(f, 3.0), note=n, hz=f)
    silence(2.0)

# 2 plucks: the attack on a decaying note
for n, f in NOTES:
    add("pluck_" + n, pluck(f, 4.0), note=n, hz=f)

# 3 dynamics: does the pedal react to how hard a note is played (threshold, attack, filter)?
for lv in (-36, -30, -24, -18, -12, -6):
    add("dyn_A3_%d" % -lv, pluck(220.0, 3.0, lv), level_dbfs=lv)

# 4 re-picks: the same note again while it rings, faster and faster; then loud -> quiet
for gap in (1.0, 0.5, 0.25, 0.125):
    seq = np.zeros(int((4 * gap + 1.5) * SR))
    for k in range(4):
        p = pluck(220.0, 1.5)
        i = int(k * gap * SR)
        seq[i:i + len(p)] += p
    add("repick_%dms" % int(gap * 1000), seq, gap_s=gap)
    silence(1.0)
seq = np.zeros(int(4.0 * SR))
for k, lv in enumerate((-12, -24)):
    p = pluck(220.0, 2.5, lv)
    seq[int(k * 1.2 * SR):int(k * 1.2 * SR) + len(p)] += p
add("repick_loud_then_quiet", seq, levels_dbfs=[-12, -24], times=[0.0, 1.2])

# 5 polyphony: a second note joins a held one. Does the first dip / restart (mono envelope) or ring on (per note)?
a = steady(110.0, 6.0, -20)
b = np.zeros_like(a)
e4 = steady(329.63, 4.5, -20)
b[int(1.5 * SR):int(1.5 * SR) + len(e4)] = e4
add("poly_A2_then_E4", a + b, first="A2 at 0 s", second="E4 at 1.5 s")
silence(1.5)

# 6 chords
for name, spread in (("chord_strum", 0.015), ("chord_block", 0.0)):
    ch = np.zeros(int(5.0 * SR))
    for k, f in enumerate((82.41, 123.47, 164.81, 207.65, 246.94, 329.63)):
        p = pluck(f, 4.5, -22)
        i = int(k * spread * SR)
        ch[i:i + len(p)] += p
    add(name, ch / np.max(np.abs(ch)) * db(-14), notes="E major, 6 strings", strum_s=spread)

# 7 short sounds: staccato plucks and string-noise-like bursts
seq = np.zeros(int(4.0 * SR))
for k in range(8):
    p = pluck(220.0, 0.08)
    seq[int(k * 0.15 * SR):int(k * 0.15 * SR) + len(p)] += p
for k in range(4):
    nb = fade(rng.standard_normal(int(0.03 * SR)), 0.001, 0.005) * db(-30)
    i = int((1.6 + k * 0.5) * SR)
    seq[i:i + len(nb)] += nb
add("short_sounds", seq, plucks="8 x 80 ms, 150 ms apart", noise="4 x 30 ms from 1.6 s")
silence(1.5)

# 8 filter: broadband steady sounds and a sweep (use with ATTACK at 0)
w = rng.standard_normal(int(6.0 * SR))
f = np.fft.rfft(w); f[1:] /= np.sqrt(np.arange(1, len(f))); pink = np.fft.irfft(f, len(w))
add("pink_noise", fade(pink / np.sqrt(np.mean(pink ** 2)) * db(-24), 0.05, 0.05), rms_dbfs=-24)
silence(1.5)
add("buzz_55Hz", steady(55.0, 6.0, -18, harmonics=250), hz=55, harmonics="to 16 kHz")
silence(1.5)
T, f1, f2 = 12.0, 20.0, 20000.0
t = tt(T)
L = T / np.log(f2 / f1)
add("log_sweep", fade(np.sin(2 * np.pi * f1 * L * (np.exp(t / L) - 1)) * db(-18), 0.05, 0.05), f_from=f1, f_to=f2)
silence(1.5)

# 9 detune: a pure tone, so the detuned copy shows as sidebands / beating
add("sine_440", fade(np.sin(2 * np.pi * 440 * tt(5.0)) * db(-18), 0.05, 0.05), hz=440)
silence(1.0)

add("sync_end", clicks(), note="3 clicks: checks the recording has no drift")
silence(1.0)

sig = np.concatenate(out)
assert np.max(np.abs(sig)) < 1.0
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "measurements")
os.makedirs(root, exist_ok=True)
pcm = np.round(np.clip(sig, -1, 1 - 2 ** -23) * (2 ** 23 - 1)).astype(np.int32)
b = pcm.astype("<i4").tobytes()
b24 = b"".join(b[i:i + 3] for i in range(0, len(b), 4))
with wave.open(os.path.join(root, "pog2_test_signals.wav"), "wb") as wf:
    wf.setnchannels(1); wf.setsampwidth(3); wf.setframerate(SR); wf.writeframes(b24)
with open(os.path.join(root, "pog2_cues.json"), "w") as fh:
    json.dump(dict(sample_rate=SR, length_s=round(len(sig) / SR, 3), sections=cues), fh, indent=1)
print("wrote %.1f s, %d sections" % (len(sig) / SR, len(cues)))
