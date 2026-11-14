# PolySwell: project handoff for Claude Code

A Windows VST3 guitar plugin (JUCE 8, C++17) that emulates the attack/swell section of the Electro-Harmonix
Pico Swello / POG2: a **polyphonic attack envelope** (every note swells on its own) plus an envelope-swept
low-pass filter. Owner: Benjamin (guitarist, Windows, Visual Studio 2022, rehearsal-room use in front of an amp).
"Swello" and "POG" are EHX names, so the plugin is called PolySwell and the pedal face reads BENNO POLYSWELL.

## Build, test, install
- Build (Developer PowerShell or the bundled CMake): `cmake -B build -G "Visual Studio 17 2022"` then
  `cmake --build build --config Release --target PolySwell_VST3`. JUCE 8.0.6 is fetched by CMake.
- Output: `build\PolySwell_artefacts\Release\VST3\PolySwell.vst3` (a folder bundle). Copy the whole folder to
  `C:\Program Files\Common Files\VST3` (needs admin; the DAW must be closed).
- `Update-PolySwell.ps1` / `.bat` (kept next to the repo, not inside it) automates unzip, DSP tests, build, install.
- DSP tests need no JUCE: `cmake --build build --config Release --target engine_test` (or
  `g++ -O2 -std=c++17 -I Source tests/engine_test.cpp`). **All tests must pass before installing.**
- Build in the user's own folder (not under Program Files; CMake cannot write there).
- Batch 1 compiles cleanly (VS 2022, no warnings) and runs in the owner's DAW.
- UI check without a DAW: build target `ui_snapshot` and run it with an output folder (tools/ui_snapshot.cpp):
  writes PNGs of the closed, filter-view, drawer-open and bypassed states.
- Files copied from a zip keep old timestamps; touch them or MSBuild skips the rebuild (the update script now does).

## Architecture (Source/dsp/SwelloEngine.h, no JUCE dependency)
1. Mono in (guitar), STFT, 75 % overlap, sqrt-Hann. SwelloEngineT<Order>: SwelloEngine = 1024 points (latency
   1024 samples, ~21 ms @ 48 kHz), SwelloEngineLL = 512 points (~10.7 ms, LOW LATENCY switch). Only the ACTIVE
   engine runs (owner: set-and-forget, a pop on switching is fine); a switch resets the other engine and reports
   the latency via AsyncUpdater. The dry path is delayed by the active engine's latency.
2. Per FFT bin: bins are **armed** (gain 0) while quiet. Any sound that appears (string noise, ghost notes,
   staccato plucks) must swell in. Activity threshold -80 dBFS, re-arm after 3 quiet frames. Re-picks over a
   ringing note are detected by a jump > 6 dB against two trackers (3-bin smoothed fast tracker, raw slow tracker).
   Attack gain = phase^2 over ATTACK time.
3. SUSTAIN (extra, not on the pedal): per bin, a smoothed level (300 ms, the decay trend) may fall no faster than
   0.5 + 23.5*(1-s)^2 dB/s; the shortfall is made up (max +24*s dB, gated near the noise floor). Notes ring longer,
   never louder than their own peak; tremolo/beating passes through (test 12). The first version (fixed 6 dB/s hold
   on the raw magnitude) mostly filled in dips and sounded like a volume boost; do not go back to raw magnitudes.
3b. NOTE EVENTS (Batch 2): onset bins of a frame with >= -60 dBFS energy start an event; onsets within 30 ms of the
   previous one join it (strum). While open (~30 ms), the event's level = loudest frame of its own bins.
   TOUCH: strength = (level - running average of event levels) / 12 dB, attack x 2^(-touch*strength*2).
   FOLLOW: attack <= (2 .. 0.4 by FOLLOW) x time since the previous event (gaps > 2 s: no cap).
   Each bin keeps its own attackMul, so the ATTACK knob still acts live. Small onsets (string noise) keep plain
   ATTACK. latestNote() publishes the event for the UI (tests 13, 14).
   An event also needs >= 15 % of the frame's energy (eventMinShare): otherwise beating / wobbling inside a ringing
   note re-triggered events and PACE collapsed attacks to almost nothing. PACE ignores gaps < 60 ms. Strum window
   default 60 ms (range 5..250). Known limit: a quieter re-pick of the SAME note after a much louder one doesn't
   stand out against its own tail (per-bin onset), so it isn't a new event. Test 24.
3c. FEEDBACK (Batch 3, an approximation; no reference recording yet): per bin, fbAmt grows once the swell is done
   (rise time fbRiseSec); bloom = smoothstep after the first quarter of the rise. The bloom holds the note up
   (0.5 dB/s hold, max +24 dB, like SUSTAIN) and every spectral peak below 3 kHz gets an overtone at ratio x its
   phase-vocoder frequency (octave 2x, 12th 3x, unison 1x), resynthesised with the window's exact spectrum (kernel
   table) and phase = ratio x the partial's phase, so it stays coherent. Level 0.8 x bloom x tone roll-off above
   fbToneHz. Copying bins (peak-locked shift) was tried first and lost ~7 dB to frame cancellation: do not go back.
   Test 15. When the owner records real DI + amp feedback, tune fbLevel, the bloom curve and the harmonic choice.
   fbMode 1 = Immediate (harmonizer / octave pedal): bloom = FEEDBACK from the pick, no hold, the overtone follows
   the note's own gain (swell + tremolo). In bloom mode the overtone must NOT use the note gain (it already holds
   the hold boost; doing so doubled it and failed the runaway check). Test 20.
   Sub-octave (fbHarmonic 3, ratio 0.5): a phase can't be halved, so it keeps its own running phase per partial
   (subPhPrev/Cur) instead of locking to the note (test 19).
3d. TREMOLO (per note): each bin keeps noteTime since its onset; gain *= 1 - depth * lfo(noteTime), lfo 0 at the
   pick (sine / triangle / soft square). Rate from RATE or host tempo (tremSync). Limitation: notes < ~140 Hz apart
   share bins and so share a phase (test 17 uses 220/550 Hz).
3e. SPREAD: per-event pan: alternate / random / by pitch (lowest note from findFundamentals, E2 left .. E4 right) /
   by pick strength (soft left, hard right) / loud in the middle / soft in the middle (the others alternate sides);
   refined while the event is open, like TOUCH. Per-bin
   panning by frequency was tried first and put every note in the middle (a note's harmonics span the whole
   spectrum): do not go back. Equal-power, unity in the centre, 3-tap smoothed over bins. Only the difference R - L is computed (second IFFT into dRing, only while spread > 0, then 4 frames
   to drain); processStereo() returns L and R = L + dRing. Test 18.
4. Per-bin low-pass in the spectral domain, so the filter sweep is **per note** (a new note's sweep never touches
   ringing notes). FILTER Off / Low Res / High Res; MOD (-1..+1, 0 = off) sweeps the cutoff up to 6 octaves, timed
   by ATTACK; FREQ = end of an up-sweep / start of a down-sweep. SLOPE 6/12/24/48 dB/oct: resonance only in the
   first 2-pole stage, extra stages flat Butterworth-style.
5. Post-engine (PluginProcessor.cpp): VOL, DETUNE (Source/dsp/Detuner.h, two-tap crossfading delay-line shifter,
   +cents left / -cents right, blend fades in over the first 5 cents, only the copy is delayed ~20 ms),
   DRY (latency-aligned unswelled signal), safety LIMITER (Source/dsp/Limiter.h, instant attack, ceiling 0.97),
   bypass crossfade to the latency-aligned dry path. Output is stereo; a mono output gets the L/R average.
   SMEAR (Round 8, Source/dsp/Smear.h, owner's reference: Rainger FX Reverb-X as used by mk.gee): after DETUNE and the
   voice mix, before DRY/limiter. 8-line FDN (Householder, per-line damping 7 kHz or 2.2 kHz DARK + 2.5 kHz tone) with
   4 input all-passes; decay 0.3..6 s (SIZE); DRIVE = tanh soft clip on the reverb only; GATE keyed by the
   latency-aligned dry input (open > ~-50 dBFS, 1 ms / 20 ms follower), stays open HOLD ms, closes over CUT ms
   (smoothstep edge), opens in 3 ms. LEVEL adds 0..2.5 x the reverb on top (it is the mix; no separate MIX knob,
   owner agreed). SEND: Always / Follows swell (send = latest note's swell phase^2, Igor-like bloom) / Pedal
   (smearPress, for an expression pedal). smearOn off = not processed (reset when switched on). Test 27.
   THRESH (smearThresh, -80 = off): input gate before the reverb, like the Reverb-X's default gate: only playing
   above the threshold feeds it (2 ms open / 60 ms close, same key follower as the tail GATE). Test 29.
   (Earlier advice was to keep reverb out of this plugin; the owner chose to add SMEAR.)
5b. BOWED (Round 9, from the "REA agent setup" session's spec C:\Users\Benjamin\Voyager bow test\vg8-bowed-spec.md, v2 values
   measured from Voyager's V-8 bow mode): the Roland VG-8 BOWED sound is PROCESSING of the guitar's own signal (not
   synthesis), so it lives in the main swell path, not in VOICE. Built as a factory preset "Bowed (VG-8)" plus three
   general controls: swellShape (Classic = phase^2 / Bowed = ((1-e^(-3.1 ph))/(1-e^-3.1))^2.45, i.e. the measured
   (1-e^(-t/0.161))^2.45 over a 0.5 s attack; swellGain/swellPhaseFor, tuning::bowedK/bowedExp), smoothRepick (a
   re-pick over a ringing note re-swells from zero but the output is held >= 0.7 x the old note's level until the
   new swell catches up: small dip, the new pick never jumps out; tuning::repickFloor) and EVEN (FEEL panel: per note
   gain 2^(-even x strength x touchRange / 6 dB), so EVEN 1 levels picks to the running average; VG-8 DYNAMICS 100 =
   EVEN 0). Preset: attack 500 ms Bowed, filter on 6 dB/oct 3.8 kHz with MOD 0.15 (opens ~0.8 oct over the attack),
   no resonance, DYN/DYNAMICS 0, vibrato +-7 c 4.5 Hz on the guitar from the pick, tremolo 0.2 at 4.5 Hz (the
   measured +-1..1.5 dB wobble), DETUNE 3 c + wobble for the slight width, no reverb. Not built yet: P-BEND (bends
   darken/quieten the note; needs per-note pitch deviation on the guitar path). Test 28.
   BODY (Round 10, param bowedBody, gear menu, its own switch - owner's choice): the measured VG-8 BOWED tone (spec
   section 3.3, smoothed third-octave table in bowedBodyDb(), +3 dB makeup tuning::bodyMakeupDb) as a fixed gain per
   FFT bin on the guitar path (bodyGain, no latency). With it the preset's filter moved 3.8 -> 9 kHz (still MOD 0.15:
   starts ~5 kHz at the pick, matching the measured attack darkening). Test 28d. A bin of the latest note now keeps its note settings (attack, DYN, EVEN) even
   after the note group closed (late onset flags while a pluck enters the window used to reset them).
5c. Round 13: processBlock runs in two passes around a block step: pass 1 = engine, voice, detune -> wetBuf / dryBuf;
   then BODY IR (juce::dsp::Convolution, NonUniform head 256 = zero latency) on wetBuf blended by bodyMix; pass 2 =
   DRIVE, SMEAR, DRY, limiter, bypass. BODY IR (param bodyIr: None, 7 owner-supplied violin body IRs embedded via
   juce_add_binary_data from resources/ir/violin_*.wav (stereo 48 kHz, 223 ms), Your IR = processor customIr file
   saved in the state; IRs load on the message thread in handleAsyncUpdate / setCustomIr, never on the audio thread).
   DRIVE (Source/dsp/Drive.h, driveOn/driveType Tape|Tube/driveAmt/driveTone/driveWow): first-order ADAA soft clip (no
   oversampling); TAPE = 2 kHz pre/de-emphasis around tanh, head bump, top roll-off darkening with drive, wow 0.6 Hz +
   flutter 7 Hz as a ~1 ms modulated delay; TUBE = tanh(x + 0.25) with DC blocker (even harmonics). Test 31.
   Round 14: IRs are decoded by us and normalised to unity energy (JUCE's Normalise::yes left them 15-18 dB quiet;
   the owner heard the level drop). DRIVE now sits BEFORE the BODY IR (end of pass 1), like amp -> cab: a body IR alone
   on a plucked guitar does not sound like a violin (owner: it sounds most violin-like with the voice's saw through it -
   the excitation matters: a bowed string is a sustained, buzzy sawtooth). The IR runs on guitar + voice together.
   Preset "Violin body": bowed swell, EVEN .5, Violin A (mix 1), TUBE drive .65 (tone .4), voice BOW 45 % + STRING .5
   (tone 6 kHz) + GLIDE 0.12 s through the body, HUMAN vibrato 14 c after 0.35 s on the guitar.
   Round 16 (violin tools, test 32): GLIDE (voiceGlide 0..0.3 s): a single new note (newCount == 1, not a chord) slides
   from the last lead note if that sounded within tuning::glideMemorySec (0.4 s) and is 0.04..1.05 oct away; smoothstep
   over voiceGlide, counted from confirmation. MONO (voiceMono): lead = newest confirmed track; others fade x0.55/frame
   (guitar stays poly; MIDI unaffected). BOW wave (voiceWave 8): |sin(pi h / 7.3)| / h (bow-position notch at h 7),
   per-harmonic slip-stick phase/amp jitter, rosin noise through the BREATH path; bow pressure = pick strength
   (brighter tone + more jitter/noise). HUMAN vibrato (vibHuman): depth smoothstep over 0.9 s, rate 85 % -> 100 % over
   1.2 s, +-5 % wander at 0.37 Hz (voice and guitar). UI: 9 wave buttons (5 + 4), GLIDE knob + MONO toggle in the
   VOICE bottom row, VIBRATO chip menu FEEL: Steady / Human.
   MIDI bends: a held note follows bends / slides up to ~2.5 semitones between analyses (tuning::bendFollowRatio, only
   when no pick just happened), so a bend stays one note with MPE pitch bend instead of new notes (test 30). Switching
   to MPE sends the MPE configuration (RPN 6 = 15 on ch 1) and member bend range 48 (RPN 0) on ch 2..16. JUCE sends
   VST3 pitch bend as LegacyMIDICCOut events; the host must pass those on.
6. Tempo SYNC: ATTACK = note length from host BPM (120 if unknown); divisions 1/16, 1/8, 1/4, 1/2, 1 bar, 2 bars.
7. Seven factory presets via the host program list (Init, Violin swell, Slow gear, Wah bloom, Pad, Closing door,
   Pick attack). Presets never touch bypass or limiter. Values are starting points.

All tunable constants live in `namespace tuning` in SwelloEngine.h (activateLevel, releaseLevel, quietFrames,
onsetRatio, refDecayDbPerSec, refRawDecayDbPerSec, attackShapeExp, sustain constants, sweepOctaves, resMinQ/MaxQ).
CPU (tools/engine_bench.cpp, target engine_bench: per feature, average and worst 64-sample block at 48 kHz):
swell only ~0.75 %, everything on ~2.8 % of a core; voice UNISON (6 Detuners) ~1 %. Round 4 optimisations: tanPi()
lookup table instead of std::tan per bin / per voice harmonic, ph*ph instead of pow, voice filter magnitude without
complex maths (filterMagFast), wave weights once per frame (except PWM), tracker every ~20 ms (trackerEvery) with
peak picking on power (logs only around peaks), phaser coefficients every 16 samples, DETUNE at 0 only pushes the
delay lines (Detuner::push). Editor: full repaint only when a parameter / preset / bypass glow changed, otherwise
just the window (repainting the editor repainted every pedal 30x a second). Pops in a DAW: buffer >= 128 helps,
since a whole STFT frame (and every 4th frame the tracker) lands in one host block.

3f. PITCH (PitchTracker, 4096 points, ~10 ms hop, only for VOICE and SPREAD by pitch, adds no audio latency):
   candidate f0 = each peak, /2, /3; salience = sum of matched partials / sqrt(h) x (1 + 0.05 count); best wins,
   its partials are removed, repeat (max 8, next >= 12 % of the first). Without its own fundamental a candidate
   needs 2nd, 3rd and 5th harmonics in tune within 0.5 % (rejects power-chord and major-triad phantoms). Tests 21.
   The voice uses noteBin() (most swelled of the note's first three 1024-bins) for gain / noteTime / filter.
   The voice reacts to a new note's pitch ~40 ms late (long window), which the swell mostly hides. Voice tracks
   must be seen by two analyses in a row (low notes < 150 Hz: three) before sounding, then fade in over 3 frames;
   a note missing from one or two analyses is held (amp x0.97/frame) and only then fades; the level is smoothed
   (notes sharing partials, e.g. root/fifth/ninth, traded them between analyses and the voice "gargled").
   A single-partial note below 180 Hz is ignored (string noise made low "thumps"). Test 24.
   OLD (replaced) findFundamentals: spectral peaks grouped into notes (a peak at an integer multiple of an accepted note
   joins it); a missing fundamental is recovered when a peak has partners at 1.5x and 2.5x/3.5x (a power chord's
   fifth matches only 1.5x). Limit: notes / harmonics < ~140 Hz apart are not resolved at 1024 points (a low E with
   no fundamental at all is not recovered; with its weak fundamental present it is found directly). Tests 20-21.
3g. VOICE (synthVoice): per detected note, harmonics weighted by the wave (normalised to the note's energy) at
   note x octave x vibrato, running phase per source bin, x the source bin's gain (swell, sustain, tremolo), its own
   filter magnitude (fcBin), TONE roll-off and STAB (tone x (1 + 8 stab x E), E = the note's swell phase^2 while it
   swells, then e^-(t - attack)/0.18 s: the stab follows the swell, so it is heard with long attacks too); written to vSpec, panned like
   the guitar, own rings (processStereo voiceL/R). Processor: VoiceFx (Source/dsp/VoiceFx.h: UNISON via Detuners
   + DRIFT random walk, CHORUS I/II/I+II, PHASER) on the voice only, then MIX = equal-power guitar <-> voice.
   Vocal controls (Round 6, test 26), all per note inside synthVoice:
   WARMTH: harmonic weights x (1 + w (0.7 for h2, 0.4 for h3)) / sqrt(1 + w ((h-1)/5)^2).
   VOWEL (voiceVowel, 0 = off, else oo..oh..ah..eh..ee): 3-formant gain vowelGain(pos, f) per harmonic, energy-normalised
   so the level stays; pos = knob + BY PICK (voiceVowelPick) x pick strength (binStrength) x 0.35 - 0.25 x (1 - swell).
   SCOOP (Round 7, owner: the first version, 120 cents with an exponential, sounded false): up to 60 cents below,
   eased up with a smoothstep over clamp(0.35 x the note's attack, 70..300 ms), counted from when the voice note is
   HEARD (track age past confirmation), not from the pick.
   BREATH (Round 7, owner: the first, fully random broadband noise was grainy even at low levels): narrow-band air
   around each of the first 16 harmonics (more on the upper ones) plus a soft 2..7 kHz air band, level breath^1.5,
   summed per bin (breathAcc) and given a slowly wandering phase per bin (breathPh, +-75 degrees a frame) after the
   note loop, so it is smooth rather than frame-grainy.
   VG wave = "bowed pad" (owner reference: Roland VG-8 bowed pad; approximation, not matched to a recording):
   1/h^0.92 harmonics shaped by bowBody(f), a fixed bowed-string body (285/470 Hz air and wood peaks, ~1.45 kHz dip,
   ~2.8 kHz bridge hill, soft above 5.5 kHz), energy-normalised.
   Round 11 ("more alive", owner wanted more VG-8 / mk.gee): STRING (voiceString) blends each note's harmonic weights
   with the guitar note's own current harmonics (mag at h x f0, the louder of the 2 nearest bins, normalised), so the
   voice brightens at the pick and darkens with the string. LIFE (voiceLife): each Track has its own slow random
   walk (new target every ~0.3..0.9 s, 0.35 s smoothing) for pitch (+-tuning::lifeCents), brightness (+-lifeOct)
   and level (+-lifeDb), plus its own PWM phase. Voice filter: TONE = 2-pole low-pass |H|^2 = 1/((1-r^2)^2 + r^2/Q^2),
   Q = 0.707 + 7 res^2 (RES 0 = the old Butterworth roll-off), ENV (= voiceStab, label renamed from STAB) opens it
   with the swell and settles over DECAY (voiceDecay, default 0.18 s). Test 29.
   Round 12 (owner: consolidate, keep WARMTH, add a macro): LIFE folded into DRIFT (processor: p.voiceLife =
   max(voiceLife, DRIFT) + 0.6 CHARACTER; VoiceFx whole-voice wow = 0.4 DRIFT). CHARACTER (voiceCharacter, macro) adds
   +0.6 STRING, +0.6 per-note wander, +0.3 SCOOP, +0.25 BREATH and, if VOWEL is off, a vowel 0.05..0.5 (oh..ah).
   MIDI OUT (midiOut Off/Notes/MPE): updateMidi() from the voice tracks (tracking also runs with the voice off when MIDI
   is on, synthVoice(audio = false)). Note-on when a track is confirmed (2 analyses, low notes 3), velocity 80 + 42 x
   pick strength, off when the track is gone; MPE: channel 2..16 per note, pitch bend +-48 st following the tracked
   pitch. Events stamped with the sample count, placed in the block by the processor. Latency ~20-25 ms after the pick
   (test 30): tuning::fastAnalysesAfterPick extra analyses on the frames after each note event, and confirmation
   counts analyses (Track::seen) instead of frames - this also brings the voice in sooner.
   CPU (pops were not in renders = real-time load): the pitch analysis now runs on the half hop (processStereo,
   trackerDue), so it never lands in the same small host buffer as the STFT frame; Detuner uses Bhaskara's sin
   approximation for its crossfade (6 unison copies + DETUNE). Bench at 128 @ 44.1 kHz: everything on p99 465 -> 265 us,
   avg 5.7 -> 4.4 %; voice p99 258 -> 136 us. Owner runs amp sims + many plugins: keep CPU low.
   VOCAL switch (param vocalOn): off = VOWEL, SCOOP, BREATH act as 0. PHASER has an on/off (voicePhaserOn).
   readVoiceNotes() publishes up to 8 sounding notes (pitch incl. scoop/vibrato, level, time since pick, scoop left
   0..1, vowel position or -1) for the VOICE window.
3h. VIBRATO: engine vibTarget 0 voice, 1 guitar, 2 both; the parameter's choices are Voice / Guitar + voice /
   Guitar (order kept for old sessions; the processor maps them). Voice: per note frequency factor from noteTime
   (after vibDelay, 0.35 s fade). Guitar:
   guitarVibrato() = short modulated delay on the whole wet guitar, restarted at each new note's output time,
   fading out smoothly on a new note. (Rebuilding single partials per note made artifacts on low notes: removed.)
   Test 22 incl. low-note level steadiness. MAX BOOST (sustainMax) caps the sustain AND feedback holds.

## Parameters (APVTS ids)
vol (-24..12 dB, 0; two linear halves so 0 dB is at 12 o'clock) · attack (0..3000 ms, skew .35, 300; 0 = no swell: an onset
starts at phase 1 - T/frame, so only the STFT frame ramps it, ~10 ms, test 25) · freq (80..12000 Hz, skew .3, 2000) · mod (-1..1, 0) ·
filterOn (false) · res (0..1, 0) · bypass (false) · sustain (0..1, 0) · OLD, not automatable, only read to convert sessions and
presets saved before Round 5 (convertOldFilter): filter (Off/Low/High), resLow, resHigh. ·
slope (6/12/24/48, default 12) · detune (0..25 cents, 0) · dry (0..1, 0) · sync (false) · syncDiv (default 1/4) ·
limiter (true) · touch (-1..1, 0) · follow (0..1, 0) · feedback (0..1, 0) · lowLatency (false).
Fine-tune (gear TUNING panel): touchRange (dB, FEEL RANGE) · touchDepth (oct, FEEL DEPTH) · pickRange (dB, 9, DYN RANGE) ·
pickDepth (oct, 4, DYN DEPTH: filter DYN has its own strength, independent of FEEL) · followTight · chordMs (60) · sustainMax (dB) ·
fbMode (Bloom/Immediate) · fbHarmonic (Octave/12th/Unison/Sub octave) · fbRise (s) · fbTone (Hz) · tremShape ·
spreadMode (Alternate/Random/By pitch/By pick strength). Tremolo sync: tremSyncOn + tremDiv (1/1 .. 1/32 with dotted
and triplet), shown as a SYNC + division pair like the attack's.
Motion: tremolo (0..1, 0) · tremRate (0.5..12 Hz, 5) · spread (0..1, 0) · vibDepth (0..50 c) · vibRate · vibDelay ·
vibTarget · vibHuman. Voice: voiceMix · voiceWave (… VG, Bow) · voiceGlide · voiceMono · voiceOct · voiceTone · voiceUnison · voiceDrift · voiceDetune · voicePwmRate ·
voicePhaser · voiceStab · voiceChorus · voiceWarmth · voiceVowel (0 = off) · voiceVowelPick (-1..1, .5) · voiceScoop ·
voiceBreath · vocalOn · voicePhaserOn. SMEAR: smearOn (false) · smearLevel (.5) · smearSize (.5) · smearDrive (0) ·
smearGate · smearDark · smearHold (0..1000 ms, 150) · smearCut (2..500 ms, 40) · smearSend (Always/Follows swell/Pedal) ·
smearPress (1) · smearThresh (-80..0 dB, -80 = off). Voice (Round 11): voiceString · voiceLife · voiceRes ·
voiceDecay (0.03..2 s, .18) · voiceCharacter (macro) · midiOut (Off/Notes/MPE; NEEDS_MIDI_OUTPUT in CMake). Swell: swellShape (Classic/Bowed) · smoothRepick (false) · even (0..1, 0) · bowedBody (false). Pedals: bloomOn, motionOn, voiceOn, smearOn. Display names: touch = "Dynamics",
follow = "Pace", dry = "Dry Mix" (ids unchanged so old sessions load). On/off per knob (right-click > Switch
off): modOn, sustainOn, detuneOn, dryOn, touchOn, followOn, feedbackOn, tremoloOn, spreadOn, voiceMixOn, vibDepthOn (off = the knob acts as its neutral value; presets switch
all back on). Every control has a tooltip stating what it does, its range, and its default (read from the
parameter itself). Double-click resets a knob.

## UI design (PluginEditor.cpp / PedalUI.h): Board v4
Mockup: https://claude.ai/artifact/B2R8vWXbW4iDEnesehzTTW (layout "A" + window 1 chosen; older Board v3:
https://claude.ai/artifact/JWUGuekiJewS6y9qRCKdzc). Owner prefers: physical/analog look with warmth, details next to the
control they affect, icons over text, nothing cluttered; developer-only settings out of sight. Display scaling 100 %.
Font: pedalui::font = Bahnschrift (Windows) at 0.84 horizontal scale (JUCE ignores the variable font's Condensed
instances), Segoe UI fallback. Name plate keeps Georgia.
- Window 720 high, 1076 wide (sections shown) or 454 (EFFECTS tab hides them). Warm dark board (#221c18).
- Top bar: PRESET < name > (menu: factory + your presets), SAVE, gear (TUNING OptionsBlock: slope, limiter, low latency,
  FEEL RANGE/DEPTH, DYN RANGE/DEPTH, PACE TIGHT, STRUM, MAX BOOST; dot
  when any is off default).
- Main cream pedal (24,50,380x656): VOL with DRY (small cream knob) under it, FEEL panel (DYNAMICS, PACE, EVEN) with a dark
  title tab, ATTACK with an amber bar (= ENV colour), a SWELL chip (tiny curve + CLASSIC/BOWED; its menu also holds
  RE-PICK restart/smooth, a dot = smooth) and SYNC chips. Swell window: warm glass, amber glow; curves ENV
  amber, FILTER teal, BLOOM sage (the held-up part after the swell), VOICE blue spectrum, SMEAR rust (the reverb building
  behind the note); a legend under it, items faded
  when not in use. FILTER panel: dark tab with a teal filter icon, BODY toggle (gold LED; the filter view
  draws the BODY curve gold dashed), on/off slide switch, FREQ, DYN, MOD, RES. Owner: these must be visible with tooltips,
  not hidden in the gear menu.
- Round 13 layout (owner: voice too complex -> simple view + MORE; DRIVE added): rows BLOOM|SMEAR (144), MOTION (148),
  DRIVE (80, copper: TAPE/TUBE chips in the header, DRIVE, TONE, WOW), VOICE (254: window 120 high, SOURCE = MIX,
  CHARACTER, PWM, waves, octave; row TONE, VOWEL, WIDTH (= voiceUnison) and a MORE button). MORE (proc.uiVoiceMore,
  saved) shows the "DETAILS" drawer (a Section without header buttons) over MOTION + DRIVE: FILTER (RES, ENV+DECAY,
  WARMTH, STRING), VOCAL (BY PICK, SCOOP, BREATH, vocal switch), WIDTH (DETUNE, DRIFT, PHASER, CHORUS). FILTER panel:
  BODY is a menu chip (VG-8 curve toggle, IR None / 7 violins / your IR / Load your own IR...) with a small cream
  BODY MIX knob shown while an IR is chosen. uiFold has 5 entries (DRIVE = 4).
- Sections (class Section at x 428, each only as tall as its content, foldable to its header; on/off slide switch in
  the header; fold state in uiFold[4]). Top row: BLOOM (290 wide, sage tint): SUSTAIN; FEEDBACK + RISE, TONE minis + chip
  (icon + overtone, bloom/instant) in one outline; next to it SMEAR (324 wide, red-rust tint): LEVEL, SIZE, DRIVE, GATE
  box (GATE toggle, HOLD, CUT, THRESH), SEND chip (+ PRESS mini only in Pedal mode), DARK toggle. Then MOTION and VOICE, 624 wide. MOTION (amber tint): TREMOLO (+RATE, chip shape/sync/note value), SPREAD
  (chip placement), VIBRATO (+RATE, DELAY, chip voice/guitar/both), DETUNE (+WOBBLE). VOICE (80s blue tint): VOICE
  window, SOURCE box (MIX, CHARACTER (violet macro), STRING mini, PWM RATE mini only for the PWM wave, 8 wave icons,
  OCTAVE), TONE box = the voice's own filter (TONE, RES, WARMTH, ENV over DECAY in one outline),
  VOCAL box (VOWEL + BY PICK, SCOOP, BREATH), WIDTH box (UNISON + DETUNE, DRIFT, PHASER orange; CHORUS I/II/I+II chips in
  its title row, click the lit one to switch off). Rule: a small knob shares an outline/box with the knob it adjusts;
  knobs never hide; choices sit in chips (popup menus) or icon buttons.
- Top bar also has MIDI (LED chip, menu Off / Notes / MPE). SMEAR THRESH has a level ring around the knob (your
  playing level, processor smearKeyDb, on the knob's -80..0 dB scale; lit where it passes the threshold).
- VOICE window: while a TONE-box knob is turned it shows the voice filter curve (solid = TONE/RES, dashed = ENV open,
  DECAY in words), WARMTH = harmonic bars plain vs warm, STRING = wave harmonics (blue) vs the live guitar spectrum
  (amber) by the STRING amount; DRIFT/CHARACTER make the three preview notes wander independently.
- VOICE window (paintVoiceWindow): one line per sounding voice note over the last 3 s, pitch moves shown x9 around the
  note, colour = the control shaping it (gold while SCOOP is still sliding, green VIBRATO, blue note), BREATH haze.
  VOWEL: a scale OO OH AH EH EE (triangle = the knob, dots = each sounding note, moved by BY PICK and the swell) and the
  vowel's formant shape along the bottom (brighter while VOWEL / BY PICK is turned). VOCAL box has an on/off switch.
  Popup menus, value bubbles and tooltips use PedalLook's warm dark style and the Bahnschrift font.
  With no notes, or while a voice/vibrato knob is touched, it draws three preview notes from the current settings.
- Knob shortcuts: right-click menu, Ctrl+click on/off, double-click reset.
- tools/ui_snapshot: 1_default, 2_in_use, 3_rows_open_bloom_folded (vocal knobs set), 4_tuning_motion_off,
  5_pedals_hidden. `ui_snapshot <dir> --fonts` lists the Bahnschrift styles.

## Hard-won lessons (do not regress)
- Quick/short sounds slipping through unprocessed was a real bug. Cause: onset detection required a minimum
  level and a jump over a slow tracker. Fixed by the armed-bin design above; tests 6 cover clicks, 30 ms string
  noise, staccato plucks, 8 rapid plucks, re-pick over a ringing string and non-flutter on steady noise.
- A single time-domain filter on the sum made a new note's sweep filter old notes (91 % change); the spectral
  per-bin filter fixed it (test 7).
- Do not widen the onset smoothing beyond 3 bins; it smears a new note into an adjacent ringing one.
- Closely spaced low notes (< ~3 bins, ~140 Hz apart at 1024 FFT) separate less cleanly. A 512 FFT halves latency
  but worsens this.

## Planned features (owner approved the order)
**Batch 2: done** (TOUCH and FOLLOW, see Architecture 3b). Possible extension: TOUCH also opening the filter further
on harder picks.
**Batch 3: done** (FEEDBACK approximation, LOW LATENCY switch, fine-tune, right-click on/off, neutral/off knob
looks). Open: tune FEEDBACK against a real recording; ideas not done: "auto" harmonic drifting, hold trigger.
**Batch 4: done** (per-note TREMOLO, SPREAD, sub-octave feedback, RES LOW default 0, low latency runs one engine).
Further ideas offered to the owner: DECAY (opposite of sustain), FREEZE, per-note vibrato (hard).
FILTER button: owner decided to keep Off / Low Res / High Res.
**Batch 5: done** (knob states, LEDs, fine-tune markers, section order + icons, matching sync pairs, more tremolo
divisions, spread by pitch fixed + by pick strength, immediate feedback). Owner direction for what comes next:
the tone of mk.gee (Roland VG-8 guitar synth layered with the guitar) and a violin-like singing quality. Advice
given: build a per-note VOICE layer inside this plugin (resynthesis from the tracked partials, like FEEDBACK does;
zero added latency, swelled and filtered with the guitar) rather than audio-to-MIDI; optionally a sidechain input
so an external synth can be swelled/filtered by the guitar's per-note envelopes. Violin: delayed per-note vibrato,
body/bridge resonance EQ, bow noise.
**Later / ideas**: companion octave-generator plugin on the same engine (do NOT put reverb/delay or a full octaver in
this pedal); preset save/load by the user; resizable UI (scale the fixed layout); MIDI/expression-pedal mapping for
ATTACK/FREQ; CLAP/AU/standalone formats (VST3 only for now).

## Tuning against the real pedal (owner has a Pico Swello/POG2 but not at hand)
EHX publishes no attack range, filter slope, resonance values or tracking details, so these are estimates. When
the owner can record: single notes and chords at three ATTACK settings, plus MOD and FREQ sweeps on a sustained
note, dry and through the pedal. Use them to tune `namespace tuning`, the ATTACK range (10 ms..3 s), swell curve
exponent, resonance Q range (0.5..10) and sweep range (6 octaves). Real pedal behaviour documented by EHX:
ATTACK lengthens swell; FILTER button cycles off / low res / high res (hold 2 s to edit the resonances with VOL and
ATTACK); MOD noon = off, clockwise sweeps up to FREQ, counter-clockwise sweeps down from FREQ; mono, digital, buffered bypass.

## Working agreements
- Ask the owner before big design changes; they like a mockup first for UI work.
- Keep it responsive: no feature may add latency to the main path without a switch.
- Keep tests green and add a test for every new DSP behaviour (see tests/engine_test.cpp style).
- Commit messages: end with the Co-Authored-By trailer Claude Code uses by default.

## Owner TODOs (Desktop/bennoVSTs/TODOs.txt, from testing)
- Round 2 (b304ce0 fixed): low-note/chord detection (PitchTracker), vibrato artifacts, MAX BOOST affects feedback,
  window curve matches engine, feedback tone default 1.1 kHz, plate corners, gradient banding.
- Round 2 done (Board v3 built): slide-out pedals (PEDALS tab), function rows with per-knob
  options right under each knob (+ one-line summaries), 3x bigger swell window, DETUNE moved to MOTION, tuning
  ("nerd") menu behind a gear for DYNAMICS range/depth, PACE tightness, strum window, MAX BOOST, filter slope.
- Mac: needs a Mac or a macOS CI runner (GitHub Actions) to build VST3/AU; signing/notarisation for sharing.
- Save your own presets (planned next; the top bar has room for SAVE).
- Done: Ctrl/Shift+click shortcuts, details more accessible (DETAILS panels), SPREAD by pitch fixed (real pitch),
  loud/soft-in-the-middle spread modes, renames (DYNAMICS, PACE, DRY MIX).
- Round 3 done: FEEL knobs bigger, width <= 1080, inline small knobs, several rows open, option icons in rows,
  PACE false re-picks fixed, voice thumps fixed, PICK -> filter, unison/drift switches, user presets + 3 factory
  presets (Warble, Dynamic wah, Glass pad).
- Round 4 done: CPU pass (see CPU above), vibrato targets x3, bigger pedal/FEEL text, OFF look, SYNC grey, MOD idle
  with filter off, PICK -> DYN next to FREQ (3 oct, shown in the window), STAB follows the swell, voice
  confirmation/hold (thumps, gargling), strum default 60 ms, voice row regrouping, header on/off switches, envelope
  view assumes 8 dB/s natural decay (was 15, which exaggerated the sustain lift).
- Round 5 done: filter on/off + one RES (old sessions converted), DYN own range/depth (default 9 dB / 4 oct) in the
  gear panel, ATTACK down to 0 (no swell), trimmed summaries, CHORUS/PHASER row, PWM RATE always visible.
- Round 6 done: Board v4 (sections instead of tall pedals, warm look, DRY on the main pedal, colour legend, VOICE window),
  VOWEL + BY PICK, SCOOP, BREATH, WARMTH.
- Round 7 done: smoother BREATH, gentler eased SCOOP, VG = bowed pad, VOCAL and PHASER on/off, vowel scale + formant
  shape in the VOICE window, popup/tooltip style. Chips show option icons again (tremolo shape, feedback overtone).
- Round 8 done: SMEAR (gated reverb + drive) module next to BLOOM; MOTION/VOICE slightly compacted; DRY no longer
  depends on BLOOM being on.
- Round 9 done: BOWED (VG-8) preset + swell shape, smooth re-pick, EVEN. Round 10: measured BOWED BODY EQ.
- Round 16 done: GLIDE, MONO, BOW wave, HUMAN vibrato (for the violin sound).
- Round 17 done: BOW noise cut ~7x (owner: too noisy; prefers saw for violin, saved his own user preset "VIOLIN");
  "Violin body" back on saw, GLIDE .05. Voice harmonics 24 -> 40 (RES needs harmonics near TONE; RES is dimmed above
  TONE 9 kHz). SMEAR level-matched (comp = 1/sqrt(1 + 1.7 L^2 * 2^(2.83 (size - .5))), L = LEVEL; was +1.6 dB at
  default, +7.4 dB at LEVEL/SIZE 1; `ui_snapshot --levels` checks it stays ~0 dB). Idle dimming: knobs whose master
  is idle (masterOf: tremRate->tremolo, vib*->vibDepth, voice*->voiceMix, voiceDetune->voiceUnison, ...) fade as a
  whole (alpha .55, label .32); buttons fade when their pedal is off or PedalButton::idleMaster is idle (set while
  building via chipMaster). Proposed (mockup shown, awaiting owner): VOICE tabs TONE / VOCAL / WIDTH / PLAY in place of
  the MORE drawer, with a dot on tabs that have something in use.
- Round 13 done: violin body IRs + your own IR (BODY menu, MIX), DRIVE (tape/tube), VOICE simple + MORE drawer,
  "Violin body" preset.
- Round 11 done: STRING, LIFE, voice filter (RES, ENV, DECAY), SMEAR THRESH; VOICE window draws traces as runs
  (cheaper GUI). Whole-plugin CPU bench: `ui_snapshot --bench` (128 @ 44.1 kHz, all features): everything on ~5 %
  avg, p99 ~0.4 ms of 2.9 ms. Owner hears pops; fewer with the UI closed and on Init. Next: MIDI out (offered). Next: presets built around the vocal
  controls and SMEAR; possibly P-BEND.
