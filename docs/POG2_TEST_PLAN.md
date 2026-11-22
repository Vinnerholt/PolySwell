# Measuring the POG2 so PolySwell can be modelled on it

Goal: learn exactly what the POG2's ATTACK, LP FILTER, Q and DETUNE do, so PolySwell's swell, filter and detune can
match it (and, later, the octave voices for a companion plugin). Instead of playing guitar into it, a fixed test
signal is re-amped through the pedal, so every recording has the same input and can be measured to the sample.

Scope (owner, 2026-11-22): only the right-hand EFFECTS side is modelled: ATTACK, LP FILTER, DETUNE. Q is skipped
(PolySwell has its own RES knob) and so are the octave voices. **DRY FX** has four states; as understood (not checked):
off = no effects on the dry signal, green = ATTACK, red = LP FILTER, **amber = both** (used for every run); run A2
confirms it.

## What you need
- The POG2 on its own 9 V supply, nothing else in the chain.
- REAPER with two tracks: **PLAY** holds `measurements/pog2_test_signals.wav` (2 min 43 s), **REC** records the pedal.
- Cables: an Apollo **line out** into the POG2 **input**, the POG2 **output** into an Apollo **Hi-Z input**.
  A re-amp box between the line out and the pedal is best. Without one, turn the PLAY track down about 12 dB so the
  pedal gets roughly guitar level.
- A phone for one photo of the sliders per run (they have no numbers, so the photo is the record of the setting).

## Calibrate once, then touch nothing
1. Bypass the POG2 (footswitch LED off). Play the file and record it: this is run **R0**, the interface chain itself.
2. Look at the 1 kHz tone at 0:05 on REC. Set the Hi-Z gain so it peaks around -18 dBFS.
3. Switch the pedal on: DRY OUTPUT up, all four octave sliders down, DRY FX amber, ATTACK, DETUNE down, LP FILTER up,
   Q off. Move DRY OUTPUT until the 1 kHz tone is as loud as in bypass. That is **unity**. (Measured: DRY OUTPUT at max
   is exactly unity, -23.1 dBFS on and bypassed.)
4. From here on never change the Hi-Z gain, the PLAY level, or DRY OUTPUT.

Record at 48 kHz, 24-bit, no plugins on REC. Start each recording a second before the file and stop after it ends.

## The runs
"Base" = the unity setting from step 3. Slider positions as a fraction of the travel, by the printed grid lines:
0 = bottom, 1/4, 1/2, 3/4, 1 = top. Name each file as in the first column and save the photo with the same name.

| File | Change from Base | What it tells me |
|---|---|---|
| R0 | pedal bypassed | the interface chain, to subtract |
| A1 | nothing (Base) | latency, level, whether the dry path is clean |
| A2 | DRY FX off, ATTACK 1/2 | that ATTACK stops acting on the dry signal (confirms DRY FX) |
| B1 to B6 | ATTACK at 1/8, 1/4, 3/8, 1/2, 3/4, 1 | attack time per slider position and the shape of the swell |
| C1 to C5 | LP FILTER at 0, 1/4, 1/2, 3/4, 1 | cutoff per position, slope |
| D1 | ATTACK 1/2, LP FILTER 1/2 | whether the filter moves during the swell (envelope filter or fixed) |
| E1 to E3 | DETUNE at 1/4, 1/2, 1 | detune amount, and whether it is a chorus or a pitch shift |
| G1 to G3 | your three favourite POG2 sounds | a reference to A/B the plugin against by ear |

That is 21 runs of 2 min 43 s each (R0 and A1 done 2026-11-22), about an hour with the changes. B and C matter most.

Recorded by `tools/pog2_record.py --out 1` (Line Out 3 mirrors the monitor mix on this Apollo, so the signal goes out
on Monitor L; keep the Hi-Z input muted in Console's monitor mix or it feeds back). First results: round trip 83.1 ms;
the POG2 adds ~20 ms with DRY FX on (a held note reaches full level ~32 ms after bypass) and cuts above ~7 kHz even
with LP FILTER fully up.

For G1 to G3, also record about 30 s of your own playing (a lead line, chords, a swell), DI only, on its own track.
Then re-amp that DI through the pedal at the same three settings, so I get the same playing dry and through the
POG2, and can run it through PolySwell too.

## What the test file contains
The cue sheet `measurements/pog2_cues.json` lists every section and its start time. In order: alignment clicks, a
1 kHz level tone, silence for the noise floor, held tones on six notes (the attack without string decay), plucks on
the same notes, one note at six levels from -36 to -6 dBFS (does it react to playing strength?), re-picks 1 s to
125 ms apart and loud-then-quiet, a second note joining a held one (does each note swell on its own?), strummed
and block chords, staccato plucks and noise bursts (what slips through), pink noise, a buzzy 55 Hz tone and a sine
sweep (the filter), a 440 Hz sine (detune), and the clicks again (drift check).

## Sending the recordings
Put the WAVs and photos in `C:\Users\Benjamin\source\repos\PolySwell2\measurements\pog2\` and tell me. I will
then write an analysis script that lines every run up on the clicks and measures, per run: latency; the attack
curve on held tones and plucks, fitted to a time and a shape for each slider position; whether a second note
disturbs the first; the effect of playing level; the filter's cutoff, slope and resonance from the noise and sweep;
any filter movement during the swell; the detune offset and method.

The results go into PolySwell's `namespace tuning` (ATTACK range and curve, filter slope and Q range), a "POG2"
swell shape, and a factory preset that matches your G runs, checked by ear against them.

## Results (2026-11-22)
All runs recorded (A1, B1, B2, B3 redone after DETUNE was found not fully at zero; the first takes are in
`measurements/pog2/detuned/`). Findings and the model built from them: see CLAUDE.md, "POG2 measurements and model".

