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
- **Pedal** (always visible, oxblood): signal window, ATTACK / SUSTAIN / TONE (large), FEEL (`touch`) / PACE (`follow`) /
  FEEDBACK / DRY / LEVEL (`vol`, the output volume) (small), a chip row (swell shape, MATCH, EVEN, SYNC), footswitches ON (`bypass`) and HOLD.
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
HOLD (`Source/dsp/Hold.h`, test 33): 4096-point spectral freeze of the wet signal after SMEAR; the pad is ADDED, so new
notes swell over it; no latency. Two frames one hop apart are analysed at the press: magnitudes and phases from the
newer one, each bin's phase step per hop from the difference; resynthesis keeps stepping the phases, so each partial
keeps its pitch and its bins stay in step. Owner: the first version (random start phases, bin-centre steps + jitter)
was "a garbled mess" - do not go back. The test checks level (-0.4 dB) and that the pad is steady.
MATCH (`levelMatch`, engine step 3a, test 34): level-matched swell. Per bin, the pick's level = loudest frame 30..110 ms
after the onset; while the note swells the make-up `pick / level now` (max +18 dB) is blended in with the swell phase
and then kept. It goes through the same `boost` as SUSTAIN (max of the two), so a note is never louder than its pick.
A switch, default off (owner: sometimes wants loose playing to stay quieter). 1 s swell: -8.1 dB without, -1.8 dB with.

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
- Live view (owner liked gen 1's animated curves): `paintLive` draws the swell curve of the current settings, the part
  the latest note has already played lit with a dot travelling along it (`latestNote()` age and its own attack after
  FEEL / PACE), and the filter curve in teal at where its sweep is right now; the ribbons sit dimmed behind.
- Knob pictures (owner: it must be obvious what a knob does): while the mouse is over a knob or it is turned,
  `SignalWindow::setFocus` fades the ribbons back and draws that knob's picture with a one-line caption:
  `paintSwell` (level over 3.5 s: dashed = the guitar, amber = after the pedal; own pictures for FEEL hard/soft pick,
  PACE slow/fast notes, EVEN, FEEDBACK overtone, DRY, LEVEL) and `paintFilter` (response over frequency, the MOD
  sweep as dashed start -> solid end with an arrow, DYN). They use the engine's formulas; keep them in step with it.
  `ui_snapshot` renders them (`*_view_<id>.png`) through `PolySwellEditor::showKnobView`.
- The name plate glows (`bakeGlow`: blurred amber halos under warm-white letters) while the pedal is on; bypassed, only
  the dim printed name shows.
- A module that is off is darkened under its header (`paintOverChildren`); TONE shows "FILTER IS OFF".

## Not built yet (owner approved the plan, in this order)
1. FEEDBACK rebuilt as an amp-in-the-room model: the harmonic takes over from the fundamental, a DISTANCE control
   picks which harmonic wins and lets it drift, one note wins in a chord, muting kills it. Needs a DI + amp recording
   of real feedback from the owner to tune. Today the module exposes gen 1's bloom (RISE, TONE, MAX, harmonic, mode).
2. Knob pictures for the panel modules other than FILTER (HOLD, MOTION, LAYER, SPACE show name and value only).
3. Preset browser tagged Lead / Pad / Feedback, and new factory presets for the three use cases (the list is gen 1's).
4. LAYER characters as curated presets of the voice (today: four wave buttons + MORE).
5. Trim the processor of parameters gen 2 never shows (only once presets no longer need them).

## Working agreements
- Ask the owner before big design changes; they like a mockup first for UI work.
- No feature may add latency to the main path without a switch. Keep CPU low (owner runs amp sims).
- Keep tests green and add a test for every new DSP behaviour.
- Commit messages end with the Co-Authored-By trailer Claude Code uses by default.

## Round 3 (2026-11-22)
- FEEDBACK amp model (`fbAmp` on by default, `fbDistance`, `fbOneWins`; engine `updateFbModel` + `addFeedback`, test 35),
  built without a recording: DISTANCE walks a ladder note / octave / 12th / 2 oct / 2 oct + third (crossfading between
  rungs, +-0.06 slow drift); at full bloom the note drops to 35 % (`fbDuck`) while the harmonic takes over; ONE WINS gives
  each partial (level x bloom / loudest)^1.5 of the feedback; a partial 12 dB under its smoothed level (muted) resets
  its bloom. `fbHarmonic` is only used with AMP off. Tune against a real DI + amp recording when there is one.
- MIDI learn: right-click a knob or footswitch. CC in (`acceptsMidi`, NEEDS_MIDI_INPUT); `handleCc` on the audio
  thread: knobs follow, ON and latched HOLD flip on a press, other switches follow the pedal. Saved as `ccMap` in the
  state. Settings menu lists them.
- Presets: 9 PolySwell II presets (`newPresets()`: 3 Lead, 3 Pad, 3 Feedback) after the 18 classic ones;
  `programTag()`; the preset menu groups by tag and the LCD shows it. Classic presets reset the gen-2 controls.
- Pedal: ATTACK and TONE large, SUSTAIN medium (100 px); small knobs in printed groups PLAYING (FEEL, PACE),
  AMP (FEEDBACK), OUTPUT (DRY, LEVEL). Ticks only on the big knobs; round hit areas; double-click resets.
- Module header icons (`drawModIcon`); EFFECTS pull-out tab on the pedal's right edge (`DrawerTab`, dots = modules in use).
- Window live view (owner: wanted one ribbon per played note): `SignalWindow` keeps 105 columns (3.5 s) of the tracked
  notes (`readVoiceNotes`; the engine now tracks while the editor is open, `editorOpen` -> `Params::uiTrack`, and
  publishes notes without the voice sounding). Pitch lane 72 Hz..1.5 kHz: ribbons (thickness = level, pale = held up
  above the guitar), named at "now"; filter cutoff on the same axis with the cut region shaded; feedback harmonic in rust;
  HOLD pad violet; SPACE tail sage; LAYER blue core. Envelope lane: output vs guitar level, the latest note's swell
  projected dashed past "now". Legend row, input meter with a -24..-6 dB zone. Pictures for every module's controls.
- POG2 measurement plan: `docs/POG2_TEST_PLAN.md`, test signal from `tools/pog2_testsignals.py`
  (`measurements/pog2_test_signals.wav` + `pog2_cues.json`; WAVs are gitignored). Analysis script still to write once
  recordings exist.

## Round 4 (2026-11-22)
- Only what PolySwell II shows is heard: the processor fixes the gen-1-only controls at neutral (DRIVE off, chorus, phaser,
  vowel/scoop/breath/warmth/string/life/stab/voice RES+DECAY, drift, unison detune 15 c, PWM rate, tremolo sync, the
  FEEL/DYN/PACE range+depth+tight+strum fine-tunes at their defaults, BLOOM pedal on). `gen2Defaults()` switches the
  per-knob off switches back on after a session or user preset loads (gen 2 has no control for them; FEEDBACK's LED is
  the one kept). The parameters stay so old sessions load.
- SUSTAIN: fall rate 0.5 + 8 (1 - s)^1.3 dB/s (was 0.5 + 23.5 (1 - s)^2: nothing happened below ~0.43 on an 8 dB/s
  string). PACE: the paced swell (0.4 x the gap) is blended in by PACE (was a cap from 2x to 0.4x the gap that did
  nothing until it fell below ATTACK). GLIDE: long jumps slide faster (time / (1 + semitones / 7)) and leave quickly
  (offset x^2.2). MATCH: one make-up per note (its energy at the pick vs now, `matchSlots` by note group), not per bin
  (per bin lifted fast-fading upper partials and pick noise up to 18 dB: heard as drive); with MATCH the sustain hold
  waits for the swell to end. `ui_snapshot --peaks`: every preset's peak with the limiter off (all under -5 dBFS).
- UI: FEEDBACK knob moved into its module (FEEDBACK, DISTANCE, RISE, TONE); PLAYING = FEEL, PACE, EVEN; HOLD module
  removed: FADE and LEVEL minis beside the HOLD footswitch, LATCH in its right-click menu. Panel: FILTER | FEEDBACK,
  MOTION full width (each knob boxed with its own settings), LAYER | SPACE (gate + HOLD/CUT boxed, faded while off).
  Fine tuning (filter slope, classic feedback overtone, MAX boost) moved to the gear menu.
- Window: bigger plots (100 px), thicker lines, FEEL/PACE scaled to fill and FEEL zoomed in time; SPACE picture in dB so
  SIZE shows; live view: the filter is a soft veil above the cutoff with the notes under it dimming (no line), the
  SPACE tail length follows SIZE.
- POG2: `tools/pog2_record.py` plays the test file out of the Apollo (ASIO via sounddevice) and records the pedal
  back run by run (`--plan`), with `--ping`, `--meter` for setup.

## POG2 measurements and model (2026-11-22)
Recorded through the owner's EHX POG2 (effects side only: ATTACK, LP FILTER, DETUNE; DRY FX amber, DRY OUTPUT max =
unity) with `tools/pog2_record.py --out 1` (Line Out 3 mirrors the monitor mix on this Apollo; Hi-Z monitor muted or it
feeds back). Analysis `tools/pog2_analyze.py` -> `measurements/pog2_results.json`; model check `tools/pog2_compare.py`
against renders from `ui_snapshot --render`. Findings:
- Delay 19 ms (DRY FX on), band limit ~7 kHz, no dependence on playing level, per note (a second note leaves a held one
  alone), fast re-picks (125-250 ms) do not restart the swell. DRY FX off = untouched dry.
- ATTACK: level = A (1 - e^(-t/tau)) (fit error ~1.5 %), tau 14 ms (0) / 16 / 31 / 72 / 166 ms (1/2) / 1.45 s (3/4) /
  >= 4 s (top). The gain A grows with tau (dB = 5.75 + 4.83 (log10 tau + 0.745)): plucks come back to about their
  pick level, held notes come out 6-12 dB louder. Noise and dense spectra get damped during long swells (per band).
- LP FILTER: third-order Butterworth (-17..-18 dB/oct, Q 1 in its 2-pole part); -3 dB at 123 Hz (0), 247 (1/4),
  830 (1/2), 3.5 kHz (3/4), open (top).
- DETUNE: two steady copies at +-10.1 cents; the slider is their level (-18 dB at 3/8, -5..-7 dB at 1/2, the original
  gone at the top), mono.
Model in PolySwell: swell shape POG2 (`swellShape` 2: (1 - e^(-3 ph)) / (1 - e^-3), tau = ATTACK / 3, plus the
measured gain unless MATCH is on; test 36), ATTACK up to 8 s, filter slope 18 dB/oct (`slope` 4; RES 0.23 = Q 1),
DETUNE MIX (`detuneMix`, 0.5 = the old fixed blend), preset "POG2 swell" fitted to the owner's G1 setting: held notes
within ~2 dB, plucks within ~4 dB (the POG2 holds a pluck a little longer), detune within 3 dB.
- Defaults since 2026-11-22 (owner): swell shape POG2, filter slope 18 dB/oct (RES 0 = Q 1, the pedal's curve), Init uses
  both; the gen-1 "Classic" presets keep their own shapes. DETUNE: the knob is now `detuneMix` (how loud the copies
  are, through `detuneCopyLevel()`, the POG2's measured slider: 0.21 at 3/8, 0.5 at 1/2, only the copies at the top);
  the cents (`detune`, default 10.1) moved to gear > Fine tuning. ATTACK and TONE use the POG2 sliders' tapers
  (`taper()` in PluginProcessor.cpp: ATTACK 95 ms at 1/4, 0.5 s at 1/2, 4.4 s at 3/4, 12 s at the top; TONE 247 Hz,
  830 Hz, 3.5 kHz, 12 kHz). Saved sessions keep their values (the state stores plain values). `ui_snapshot --tapers`
  prints them.


## Skins (2026-11-22)
The pedal follows the owner's 70s/80s inspiration image (approved mockup: the Design canvas
https://claude.ai/artifact/EZmkrUJoXqKFvVsBdjZ8Yd, Pedal.dc.html). `resources/skin/` (BinaryData): the worn plate per skin
(`plate_*.png`, 617x1024 = the 392x646 pedal), the exact logo per skin (`logo_*.png`) and the owner's footswitch photo.
`Skin` / `skinAt()` in PluginEditor.cpp: Sunburst (orange, default), Lagoon (turquoise / blue / purple on cream),
Midnight (the same on navy). A skin sets plate, logo, ink, the knobs' three ring colours, LED tint, the pedal chips and the
window's glow (`SignalWindow::setSkin`; the effects' own colours stay). Pedal knobs (kinds 0/1) are ivory with three value
rings (outer = lightest). Switch: SKIN chip in the top bar or gear > Skin; saved as `uiSkin` in the state.
`ui_snapshot` writes `skin_0..2.png`. The effects panel keeps its dark look.

## Cleanup for release (2026-11-22, owner)
- HOLD removed entirely (parameters, `dsp/Hold.h`, test 33, footswitch, minis, window drawing): may become its own plugin.
- Pedal face: no chip row. Swell curve (POG2 default, Classic, Bowed), MATCH and ATTACK tempo sync are in gear menu
  submenus. Every preset uses the POG2 curve (`swellShape` 2) and none sets MATCH (the POG2's own gain is in its curve).
- DETUNE WOBBLE removed (the POG2's copies are steady): the processor runs it at 0, the parameter stays for old sessions.
- Knobs: one ring that fades through the skin's three colours (no overlap between neighbours). ON footswitch centred,
  LED with room for its glow, logo placed by its visible part (the PNG has empty space under the rings).
- The pedal's drop shadow is clipped outside the plate: JUCE 8's Direct2D renderer drew it over the plate (cream -> grey).
- Release: GitHub repo https://github.com/Vinnerholt/PolySwell, `.github/workflows/build.yml` (Windows + macOS, release
  on a `v*` tag), `pages.yml` (site/ -> GitHub Pages), AGPLv3.
- Pedal: SUSTAIN 84 px (ATTACK / TONE 119 are the main knobs); logo 300 wide over three racing stripes in the skin's
  colours (frame to frame, under the lettering through the oval's lower part); ON lower.
- Window live view (owner: the swell matters more than which notes): one plot over the whole glass. In front: output
  level (filled, bright edge) against the guitar (dashed), the latest note's swell projected past "now", or with nothing
  playing a "NEXT NOTE" preview (a -12 dB pick through the current settings) so ATTACK always shows. Behind, in a 30 %
  transparency layer: the note ribbons, the pitch grid and the filter veil; the filter's Hz marker stays bright.
- Window live view, round 2 (owner: gen 1's per-note view was more informative): the swell of the current settings over
  time since the pick (window = 1.4 x ATTACK + 1.2 s, 1.5..9 s), filled, the guitar dashed, an ATTACK marker; the filter's
  sweep as a teal curve on its own 80 Hz..12 kHz scale; one glowing dot per sounding note (`liveT` = VoiceNote.timeSec)
  on both, named; a held note past the picture waits at its edge. NOTES switch (top-left of the glass, `uiWindowNotes`,
  saved) overlays the tracked-note ribbons faint underneath. `swellGain` takes the shape (POG2 was drawn as Bowed).
- Knob pictures scale to their tallest curve (the POG2 make-up gain lifted long swells off the top).
- JUCE `drawImage` uses the current colour's alpha: set `g.setOpacity (1.f)` before drawing the plate / logo.
- Stripes moved to the footswitch row, 42 % alpha. Panel: FILTER | FEEDBACK, MOTION | SPACE, LAYER full width (knobs +
  octave/MONO/UNISON left, all nine waves in a WAVE box right; no MORE menu).
- Window live view, round 3 (owner: the per-note view was static; it must show how the playing is shaped): LEVEL | TONE.
  LEVEL (left 63 %): one swell per pick from `latestNote()` (last 4 kept in `picks`: time, attack / ATTACK = `mul`,
  strength, pickStrength), so ATTACK still acts live; level after EVEN (half the dB, so soft picks stay readable);
  the latest lit up to "now" with a travelling dot, the earlier ones as faint ghosts; caption HARD/SOFT/PICK + its attack
  + PACE or FEEL when they changed it. Nothing played: an average pick's swell dashed. TONE (right): the filter response
  on the latest note now (RES peak, slope, MOD sweep position with its two ends dashed, DYN shift by the pick), "OPEN"
  when off. Input meter moved beside the NOTES switch. FootSwitch photo needed `setOpacity (1.f)` too.
- Round 5 UI: a pick's dot stops when its note stops (no tracked note whose time since pick matches: `Pick::endAge`);
  LEVEL takes the whole glass with the filter off; caption beside the NOTES switch; input meter = thin bar on the left
  edge. Window 309 x 167: the plates' printed glass was stretched 38 image rows (rows 160..198 repeated, cream rows
  620..658 removed), pedal rows below it moved down 24, logo 282 wide.
- Panel rows 150 / 232 / 216: FILTER | FEEDBACK short (settings at y+120); MOTION = four boxed columns, each setting
  named above its chip (`named`); SPACE = three knobs + IN box (SEND, THRESHOLD, DARK) | GATE box (ON, HOLD, CUT);
  LAYER: knobs, OCTAVE / PLAY (MONO) / UNISON fader (Knob kind 4, `Look::drawLinearSlider`); WAVE box = `WaveScope`
  (the wave after TONE and OCTAVE, UNISON copies drifting, animates only while UNISON > 0) over `WaveGrid` (nine
  buttons with wave pictures; `waveHarm` recipes are pictures, not the synth).
- Presets (owner, 2026-11-22): the 18 gen-1 "Classic" presets are gone. Ten PolySwell II presets (`newPresets()`), each =
  every parameter at its default (setup params kept) + `defaultSetup()` (ATTACK 500 ms, POG2 curve, filter on, open at
  12 kHz, 18 dB/oct, so TONE works) + its own list: Default, Default dynamic (tag Default), Singing lead, Violin lead,
  Slow bloom lead (Lead), POG2 swell, Warm pad, Glass pad (Pad), Amp in the room, Slow howl (Feedback). A new instance
  starts as Default. `--peaks`: all under -6 dBFS.
- BODY is part of FILTER: off with it (processor), one choice in its menu (Off / VG-8 tone / a violin IR / yours), MIX
  faded unless an IR is chosen. Window caption says FEEL+PACE when both changed a pick's attack; the hardest pick peaks
  at 95 % (was clipped). MOTION = 2 x 2 boxes (VIBRATO | TREMOLO with settings beside, SPREAD | DETUNE), tremolo SHAPE
  as pictures (`ShapeSeg`). Midnight plate regenerated from Sunburst (only warm cream paint -> navy; rim and worn frame kept).
- POG2 make-up gain removed (owner: a 12 s ATTACK still jumped back to full level within ~1 s; LEVEL is the volume).
  Measured: with only DRY OUTPUT up and ATTACK at 0 the POG2 is unity (1.00); the gain grows with its ATTACK (+2.2 dB at
  1/4, +5.9 at 1/2, +8.3 at 3/4). Only the curve shape is modelled now; MATCH is the per-note "back to pick level".
  Test 36 checks there is no gain. Presets POG2 swell (+8.5 dB LEVEL) and Warm pad (+4.5) rebalanced (`--peaks`).
- Fourth skin Sunburst Night (`kNumSkins`; black knob faces with cream pointers via `Skin::face*`/`pointer`): charcoal plate (`plate_night.png`, cream paint -> #221e1a), the Sunburst
  logo with cream lettering (`logo_night.png`), cream ink, Sunburst rings and glow.
- MATCH on the pedal: a mini bat toggle (`MiniToggle`, lever up = on) right of the ON footswitch, mirroring the LED;
  right-click = MIDI learn; still in gear > Swell curve too. MATCH is ON by default (param default + `defaultSetup()`):
  on plucks it is what the POG2's gain did, per note, without the held-note overshoot. `--peaks`: Default = input level;
  POG2 swell LEVEL +5, Warm pad +3.

## TODO (owner)
- Pass through all ten presets by ear: after the POG2 gain removal and MATCH-by-default, most ATTACK times (and some
  LEVELs) probably need adjusting.
- 2.0.1: Lagoon and Midnight logos recoloured from the Sunburst logo with Sunburst's soft steps mapped to light blue
  (212 deg) -> indigo (242 deg) (no turquoise); their rings/stripes #699bd6 / #5466ce / #4435c0, Lagoon's window blue,
  Midnight navy knobs with cream pointers.
