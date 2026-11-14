# PolySwell II: project handoff for Claude Code

Second generation of PolySwell (gen 1: `..\PolySwell`, still installed side by side). Same engine, new look and a
smaller surface. Owner: Benjamin (guitarist, Windows, Visual Studio 2022). Use cases: smooth lead lines, ambient pads,
"amp in the room" feedback. Plugin name "PolySwell II", plugin code `Psw2` (gen 1 is `Pswl`), VST3 only.

The engine, its tests and every DSP lesson are documented in `docs/ENGINE.md` (gen 1's handoff, copied 2026-11-14).
Read it before touching `Source/dsp/SwelloEngine.h`. This file covers only what gen 2 changed.

## Build, test, install
- `.\build.ps1 engine_test ui_snapshot PolySwell_VST3` (finds Visual Studio's CMake; reuses gen 1's JUCE download).
- DSP tests: `build\Release\engine_test.exe`. **All tests must pass before installing** (33 tests; 33 = HOLD).
- UI check without a DAW: `build\ui_snapshot_artefacts\Release\ui_snapshot.exe shots` writes five PNGs
  (default, in use + pad held, modules off, pedal only, 150 %). Look at them after every UI change.
- Install: `.\install.ps1` from an **administrator** PowerShell with the DAW closed (copies the `.vst3` bundle to
  `C:\Program Files\Common Files\VST3`).
- `PolySwell\` in this folder is a stale build directory from 31 Oct 2026 (gitignored, not ours to delete).

## What gen 2 is
Rule for inclusion: an effect belongs only if it needs to know about individual notes.
- **Pedal** (always visible, oxblood): signal window, ATTACK / SUSTAIN / TONE (large), FEEL (`touch`) / FEEDBACK /
  DRY / LEVEL (`vol`) (small), a chip row (swell shape, PACE, EVEN, SYNC), footswitches ON (`bypass`) and HOLD.
- **Panel** (hideable; six modules, each max 4 knobs + two chip rows, each with its own muted colour):
  FILTER (teal; `filterOn`; MOD, RES, DYN, slope, BODY menu + MIX), FEEDBACK (rust; `feedbackOn`; RISE, TONE, MAX,
  harmonic, mode), MOTION (gold; `motionOn`; VIBRATO, TREMOLO, SPREAD, DETUNE + rates and modes),
  LAYER (blue; `voiceOn`; MIX, TONE, GLIDE, CHARACTER, waves, octave, MONO, UNISON), HOLD (violet; FADE, LEVEL, LATCH),
  SPACE (sage; `smearOn`; SIZE, LEVEL, DRIVE, SEND, DARK, GATE + HOLD/CUT/THR).
- **Settings (gear)**: smooth re-pick, limiter, low latency, MIDI out, window size, panel.
- **Not on the face** (parameters still exist, presets may set them, gen-1 presets load): tape/tube DRIVE, chorus,
  phaser, PWM, the single vocal knobs (CHARACTER covers them), voice filter RES/ENV/DECAY, WARMTH, STRING, tuning values.

## Processor (Source/PluginProcessor.*)
Gen 1's processor, kept whole so sessions' parameter ids and user presets (`Documents\PolySwell\Presets`) stay
compatible. Added: `juce::UndoManager undo` (passed to the APVTS; must be declared before it), parameters `holdOn`,
`holdFade` (0.02..4 s), `holdLevel`, `holdLatch`, state `uiScale`; `uiPedalsOpen` now means "panel shown".
HOLD (`Source/dsp/Hold.h`, test 33): 2048-point spectral freeze of the wet signal after SMEAR; the pad is ADDED, so new
notes swell over it; no latency. Phases advance at each bin's centre frequency + a little jitter (shimmer, not buzz).
`kNorm` was set by measurement so the pad matches the level of what was ringing (+0.2 dB in the test).

## Editor (Source/PluginEditor.*)
Approved mockup: https://claude.ai/artifact/EZmkrUJoXqKFvVsBdjZ8Yd (oxblood finish, tinted modules).
- `PolySwellEditor` only sizes things; `Board` (1076x720, or 436 wide with the panel hidden) holds everything and is
  scaled with a transform (`uiScale` 0.75..2).
- **Baked artwork**: `Board::bake` draws enclosure, plates, lettering once into an image at the physical pixel scale;
  `Look::body` bakes knob bodies per size (brushed aluminium = 240 wedges shaded by angle, one light from the top
  left). Per frame only indicators, value rings, chips, LEDs and the window repaint. Keep it that way (owner hears
  pops with a busy UI).
- Controls: `Knob` (a `juce::Slider`; kind 0/1 aluminium, 2 bakelite with a value ring, 3 = a value chip you drag),
  `Chip` (toggle / menu / custom), `Seg`, `Led`, `FootSwitch`, `Lcd`. Everything has a tooltip. Hovering or turning
  a knob shows its name and value in the window (no popup bubbles). Values are tidied by `tidyValue`.
- `SignalWindow`: ribbons from the engine's live output spectrum (64 log bands): a band that is a local peak draws
  a ribbon, thickness = level, pale where output exceeds input (sustain / feedback holding the note up). It stops
  repainting once everything has scrolled off.
- A module that is off is darkened under its header (`paintOverChildren`); TONE shows "FILTER IS OFF".

## Not built yet (owner approved the plan, in this order)
1. FEEDBACK rebuilt as an amp-in-the-room model: the harmonic takes over from the fundamental, a DISTANCE control
   picks which harmonic wins and lets it drift, one note wins in a chord, muting kills it. Needs a DI + amp recording
   of real feedback from the owner to tune. Today the module exposes gen 1's bloom (RISE, TONE, MAX, harmonic, mode).
2. Per-knob curves in the window (today: name and value only).
3. Preset browser tagged Lead / Pad / Feedback, and new factory presets for the three use cases (the list is gen 1's).
4. LAYER characters as curated presets of the voice (today: four wave buttons + MORE).
5. Trim the processor of parameters gen 2 never shows (only once presets no longer need them).

## Working agreements
- Ask the owner before big design changes; they like a mockup first for UI work.
- No feature may add latency to the main path without a switch. Keep CPU low (owner runs amp sims).
- Keep tests green and add a test for every new DSP behaviour.
- Commit messages end with the Co-Authored-By trailer Claude Code uses by default.
