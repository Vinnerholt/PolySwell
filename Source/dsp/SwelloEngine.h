#pragma once
// PolySwell DSP core. No JUCE dependency, so it can be unit-tested and
// run offline against pedal recordings.
//
// Signal flow (mono):
//   in -> STFT (N = 1024, or 512 in low-latency mode; 75% overlap, sqrt-Hann)
//          per bin: "armed" while quiet (gain 0). Any sound that appears, however
//                   small or short, starts that bin's swell; loud re-picks over a
//                   ringing note restart it.
//          per bin: attack gain, optional sustain (extra, not on the pedal) and a
//                   2-pole low-pass whose cutoff sweep follows THAT bin's own swell
//                   position -> filter and swell are both per note (polyphonic)
//          per peak: FEEDBACK - once a note has rung for a while, a pitch-shifted copy of its
//                   partials (octave / 12th / unison) blooms in and the note is held up,
//                   like a guitar feeding back in front of the amp
//      -> inverse STFT / overlap-add
//   latency = N samples (the dry path is delayed by the same amount)

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>

namespace swello
{
// ---------------------------------------------------------------------------
// Constants to retune once the real pedal can be measured.
// ---------------------------------------------------------------------------
namespace tuning
{
    // Activity detection (per FFT bin, on the *input* magnitude; 1.0 = full-scale sine)
    constexpr float activateLevel     = 1e-4f;   // bin counts as "sound present" above ~ -80 dBFS
    constexpr float releaseLevel      = 5e-5f;   // ... and as "quiet" below this (hysteresis)
    constexpr float quietSec          = 0.016f;  // quiet this long -> bin re-arms
    constexpr float onsetRatio        = 2.0f;    // re-pick: bin jumps > 6 dB over its tracker
    constexpr float refDecayDbPerSec  = 150.0f;  // smoothed tracker: follows the signal down fast (rapid plucks)
    constexpr float refRawDecayDbPerSec = 15.0f; // raw tracker: slow (new note right next to a ringing one)

    // Attack envelope
    constexpr float attackShapeExp    = 2.0f;    // gain = phase^exp  (1 = linear ramp)

    // Sustain (extra control, not on the pedal): each bin's smoothed level may fall no faster than
    // holdDecay(SUSTAIN) dB/s; the shortfall is made up, so notes ring longer but never get louder than
    // their own peak. Fast level wiggles (beating, vibrato) pass through untouched.
    constexpr float sustainSlowDbPerSec = 0.5f;  // SUSTAIN = 1: decay rate of the held level
    constexpr float sustainFastDbPerSec = 24.0f; // SUSTAIN -> 0: approaches this (= barely any hold)
    constexpr float sustainEnvMs      = 300.0f;  // smoothing of the level that gets held (the decay trend)
    constexpr float sustainFloor      = 5e-4f;   // no boost near the noise floor

    // Note events (whole-note onsets, used by TOUCH, FOLLOW and the UI)
    constexpr float eventMinLevel     = 1e-3f;   // onset energy of a note event: above ~ -60 dBFS
    constexpr float eventMinShare     = 0.15f;   // ... and at least this share of everything sounding (a wobble inside
                                                 //     a ringing note re-triggers a few bins, not a real share)
    constexpr float followMinGapSec   = 0.06f;   // shorter gaps are one gesture, never "fast playing" for PACE
    constexpr int   fastAnalysesAfterPick = 3;
    constexpr float glideMemorySec    = 0.4f;    // GLIDE starts from the previous note if it sounded within this
    constexpr float bendFollowRatio   = 0.21f;   // a held note may move up to ~2.5 semitones (log2) between analyses   // extra pitch analyses on the frames right after a pick
    constexpr float lifeCents         = 9.0f;    // LIFE at full: each note wanders up to +-9 cents
    constexpr float lifeOct           = 0.45f;   // ... its brightness +-0.45 octave
    constexpr float lifeDb            = 1.8f;    // ... its level +-1.8 dB
    constexpr float bowedK            = 3.1f;    // BOWED swell: attack spans 0.5 s / 0.161 s time constants (measured)
    constexpr float bowedExp          = 2.45f;   // BOWED swell exponent (measured S-curve)
    constexpr float bodyMakeupDb      = 3.0f;    // BODY: +3 dB so the 1..5 kHz range stays near 0 dB (the bump goes to +3)
    constexpr float matchMaxBoost     = 8.0f;    // level-matched swell: at most +18 dB of make-up
    constexpr float matchPeakFrom     = 0.03f;   // ... the pick's level is the loudest frame 30..110 ms after the onset
    constexpr float matchPeakSec      = 0.11f;   //     (the first frames hold the pick's click spread over many bins)
    constexpr float repickFloor       = 0.7f;    // smooth re-pick: output kept above 0.7 x the old note (~-3 dB dip)
    constexpr float pickFilterOct     = 4.0f;    // default of Params::pickDepthOct (the UI's fallback)
    constexpr float touchRefCoef      = 0.2f;    // running average of event levels (per event)
    constexpr float followMaxGapSec   = 2.0f;    // longer gaps: no FOLLOW cap (new phrase)
    constexpr float followFracLoose   = 2.0f;    // FOLLOW just on: attack <= 2 x time since last note

    // Feedback
    constexpr float fbPeakMin         = 1e-3f;   // partials above ~ -60 dBFS can feed back
    constexpr float fbMaxSourceHz     = 3000.f;  // partials above this never feed back
    constexpr float fbHoldDbPerSec    = 0.5f;    // a feeding-back note decays at most this fast
    constexpr float fbMaxBoostDb      = 24.0f;
    constexpr float fbTakeover        = 0.65f;   // amp model: at full bloom the note itself drops to 35 %, its harmonic takes over
    constexpr float fbDrift           = 0.06f;   // amp model: DISTANCE wanders by +-this (the player never stands still)
    constexpr float fbWinPower        = 1.5f;    // amp model, one note wins: share = (partial / loudest)^this
    constexpr float fbMuteRatio       = 0.25f;   // amp model: a partial 12 dB under its own smoothed level was muted -> feedback dies
    constexpr float fbLevel           = 0.8f;    // full bloom: overtone at about -2 dB of the note
    constexpr int   fbKernelHalf      = 3;       // bins either side of a resynthesised overtone

    // Voice / vibrato
    constexpr float voicePeakMin      = 3e-4f;   // partials above ~ -70 dBFS take part in pitch detection
    constexpr float voiceMaxSourceHz  = 5000.f;
    constexpr float voiceLevel        = 0.9f;    // voice note level relative to the guitar note's energy
    constexpr float vibFadeSec        = 0.35f;   // vibrato fades in over this after its delay

    // Filter
    constexpr float modDeadZone       = 0.01f;   // around noon -> modulation off
    constexpr float sweepOctaves      = 6.0f;    // sweep range at MOD fully turned
    constexpr float resMinQ           = 0.5f;
    constexpr float resMaxQ           = 10.0f;
}

struct Params
{
    float attackMs   = 300.0f;   // 0 .. 3000 (0 = no swell: notes start at full level)
    int   swellShape = 0;        // 0 classic (phase^2), 1 bowed (VG-8 measured S-curve, see swellGain)
    bool  levelMatch = false;    // the swell ends as loud as the pick was (makes up the string's decay during ATTACK)
    bool  smoothRepick = false;  // a re-pick over a ringing note re-swells from the level it has, not from silence
    float even       = 0.0f;     // 0 .. 1: EVEN, picks levelled towards the average (VG-8 DYNAMICS 100 = 0 here)
    bool  bowedBody  = false;
    float voiceGlide = 0.0f;     // s: GLIDE, a new single note slides from the previous one (0 = off)
    bool  voiceMono  = false;    // MONO: the voice plays only your newest note (lead lines), others fade
    bool  vibHuman   = false;    // HUMAN vibrato: deepens over ~0.9 s, starts a bit slower, wanders slightly in rate
    int   midiOut    = 0;        // MIDI out from the tracked notes: 0 off, 1 notes (channel 1), 2 MPE (per-note bend)    // BODY: the measured VG-8 BOWED tone curve as a fixed EQ on the guitar path
    float sustain    = 0.0f;     // 0 .. 1
    float freqHz     = 2000.0f;  // LPF cutoff / sweep end
    float mod        = 0.0f;     // -1 .. 1  (0 = noon = no modulation)
    int   slope      = 1;        // 0: 6, 1: 12, 2: 24, 3: 48 dB/oct
    int   filterMode = 0;        // 0 off (green), 1 low res (orange), 2 high res (red)
    float resLow     = 0.0f;     // 0 .. 1 (0 = no resonance)
    float resHigh    = 0.65f;    // 0 .. 1
    float touch      = 0.0f;     // -1 .. 1: + = harder picks swell faster, - = harder picks swell slower
    float follow     = 0.0f;     // 0 .. 1: cap each note's attack by the time since the previous note
    float feedback   = 0.0f;     // 0 .. 1: amp-feedback bloom (overtone + sustain) on held notes
    float tremDepth  = 0.0f;     // 0 .. 1: per-note tremolo depth (each note's own LFO starts at its pick)
    float tremRateHz = 5.0f;     // tremolo rate
    int   tremShape  = 0;        // 0 sine, 1 triangle, 2 square (soft edges)
    float spread     = 0.0f;     // 0 .. 1: each note gets its own stereo position
    int   spreadMode = 0;        // 0 alternate, 1 random, 2 by pitch (low left, high right), 3 by pick (soft left,
                                 // hard right), 4 loud in the middle (soft to the sides), 5 soft in the middle
    // VOICE: a synth layer rebuilt from each note's pitch (output on its own channel pair, see processStereo)
    float voiceMix     = 0.0f;   // > 0 switches the voice synthesis on (the actual blend happens in the processor)
    int   voiceWave    = 1;      // 0 supersaw, 1 saw, 2 pwm, 3 square, 4 organ, 5 glass, 6 reed, 7 vg
    int   voiceOct     = 1;      // 0: -1 octave, 1: at pitch, 2: +1 octave
    float voiceToneHz  = 3000.f; // brightness: harmonics above this are rolled off
    float voicePwmRate = 0.8f;   // PWM wave: width modulation rate (per note, from its pick)
    float voiceStab    = 0.0f;   // 0 .. 1: brass "stab", the tone opens wide at each pick and settles
    float voiceWarmth  = 0.0f;   // 0 .. 1: rounder, analog-like harmonics (more 2nd/3rd, softer highs)
    float voiceString  = 0.0f;   // 0 .. 1: STRING, the voice's harmonics follow the guitar note's own (brightens at
                                 //         the pick, darkens as the string decays, like the VG-8)
    float voiceLife    = 0.0f;   // 0 .. 1: LIFE, each note wanders on its own (pitch, brightness, level, PWM phase)
    float voiceRes     = 0.0f;   // 0 .. 1: resonance of the voice filter (at TONE)
    float voiceDecay   = 0.18f;  // s: how fast the ENV (STAB) opening settles after the note is in
    float voiceVowel   = 0.0f;   // 0 = off, else vowel position oo .. oh .. ah .. eh .. ee
    float voiceVowelPick = 0.5f; // -1 .. 1: pick strength moves each note's vowel (hard = more open)
    float voiceScoop   = 0.0f;   // 0 .. 1: each note eases up into pitch (up to 60 cents below), timed by the swell
    float voiceBreath  = 0.0f;   // 0 .. 1: breathy air hugging each note's harmonics
    // VIBRATO (per note, fades in after the delay)
    float vibCents     = 0.0f;   // depth, 0 = off
    float vibRateHz    = 5.5f;
    float vibDelaySec  = 0.4f;
    int   vibTarget    = 0;      // 0: voice only, 1: guitar only, 2: guitar and voice

    // fine-tune (right-click a knob in the UI); defaults are the tuned values
    float touchRangeDb  = 12.0f;  // pick level +- this around the running average = full TOUCH
    float touchDepthOct = 2.0f;   // TOUCH fully on: attack x 2^depth or / 2^depth
    float pickRangeDb   = 9.0f;   // filter DYN: pick level +- this around the running average = full shift
    float pickDepthOct  = 4.0f;   // filter DYN fully on: a hard / soft pick moves the cutoff +- this
    float followTight   = 0.4f;   // FOLLOW full: attack <= this x time since the previous note
    float chordGroupMs  = 60.0f;  // onsets within this of the previous one are one note (strum)
    float pickFilter    = 0.0f;   // -1 .. 1: + = harder picks open the filter further, - = harder picks are darker
    float sustainMaxDb  = 24.0f;  // SUSTAIN = 1: largest make-up gain
    bool  fbAmp         = false;  // amp-in-the-room model: DISTANCE picks the harmonic, it takes over, one note wins
    float fbDistance    = 0.3f;   // 0..1 along the ladder note / octave / 12th / 2 octaves / 2 octaves + third
    bool  fbOneWins     = true;   // the loudest partial takes most of the feedback
    bool  uiTrack       = false;  // keep tracking the played notes for the editor's window (no sound)
    int   fbHarmonic    = 0;      // 0: octave, 1: octave + fifth (12th), 2: unison (the note itself), 3: sub-octave
    float fbRiseSec     = 1.2f;   // how long a held note takes to bloom fully
    float fbToneHz      = 1100.f; // overtones above this are rolled off (purer, more sine-like)
    int   fbMode        = 0;      // 0: bloom on held notes (amp feedback), 1: immediate (harmonizer / octave pedal)
};

// Minimal radix-2 complex FFT (keeps the core free of external dependencies).
template <int Order>
class FftT
{
public:
    using Cpx = std::complex<float>;
    static constexpr int N = 1 << Order;

    FftT()
    {
        for (int i = 0; i < N; ++i)
        {
            int r = 0;
            for (int b = 0; b < Order; ++b)
                if (i & (1 << b)) r |= 1 << (Order - 1 - b);
            rev[(size_t) i] = r;
        }
        for (int k = 0; k < N / 2; ++k)
        {
            const double a = -2.0 * 3.14159265358979323846 * k / N;
            tw[(size_t) k] = Cpx((float) std::cos(a), (float) std::sin(a));
        }
    }

    void forward(Cpx* d) const
    {
        for (int i = 0; i < N; ++i)
            if (i < rev[(size_t) i]) std::swap(d[i], d[rev[(size_t) i]]);

        for (int len = 2; len <= N; len <<= 1)
        {
            const int half = len >> 1, step = N / len;
            for (int i = 0; i < N; i += len)
                for (int j = 0; j < half; ++j)
                {
                    const Cpx t = d[i + j + half] * tw[(size_t) (j * step)];
                    d[i + j + half] = d[i + j] - t;
                    d[i + j]        = d[i + j] + t;
                }
        }
    }

    // Normalised inverse (divides by N).
    void inverse(Cpx* d) const
    {
        for (int i = 0; i < N; ++i) d[i] = std::conj(d[i]);
        forward(d);
        const float s = 1.0f / (float) N;
        for (int i = 0; i < N; ++i) d[i] = std::conj(d[i]) * s;
    }

private:
    std::array<int, N> rev {};
    std::array<Cpx, N / 2> tw {};
};

// Polyphonic pitch detection on a long (4096-point, ~85 ms) window, for the VOICE and SPREAD by pitch only.
// The audio itself still comes from the short-window engine, so this adds no latency to the sound. With
// 11.7 Hz bins it resolves a low E's harmonics. Notes are found one at a time: the candidate (each peak, and
// 1/2 and 1/3 of it) whose harmonic series explains the most energy wins, its partials are taken out, repeat.
// A candidate without its own fundamental only counts if its 2nd, 3rd and 5th harmonics are all there (a real
// low note with a weak fundamental), so a power chord's root + fifth can't fake a note an octave below.
class PitchTracker
{
public:
    static constexpr int N = 4096;
    struct Note { float hz, amp; };   // amp: combined amplitude of the note's partials (sqrt of their energy)

    PitchTracker()
    {
        double sum = 0.0;
        for (int n = 0; n < N; ++n)
        {
            win[(size_t) n] = (float) (0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * n / N));
            sum += win[(size_t) n];
        }
        magNorm = (float) (2.0 / sum);
    }
    void prepare(double sr) { sampleRate = sr; reset(); }
    void reset() { ring.fill(0.f); w = 0; }
    void push(float x) { ring[(size_t) w] = x; w = (w + 1) & (N - 1); }

    int analyse(Note* out, int maxNotes)
    {
        for (int n = 0; n < N; ++n) buf[(size_t) n] = std::complex<float>(ring[(size_t) ((w + n) & (N - 1))] * win[(size_t) n], 0.f);
        fft.forward(buf.data());
        const float binHz = (float) sampleRate / (float) N;
        const int   kMax  = std::min(N / 2 - 2, (int) (5000.f / binHz));
        for (int k = 1; k <= kMax + 1; ++k) lm[(size_t) k] = std::norm(buf[(size_t) k]);   // power (cheap)
        const float minPow = (kMinAmp / magNorm) * (kMinAmp / magNorm);
        auto logMag = [this](int k) { return 0.5f * std::log(lm[(size_t) k] + 1e-20f) + std::log(magNorm); };

        // peaks (parabolic interpolation on the log magnitude), ascending in frequency
        np = 0;
        for (int k = std::max(2, (int) (45.f / binHz)); k <= kMax && np < kMaxPeaks; ++k)
        {
            if (lm[(size_t) k] <= lm[(size_t) k - 1] || lm[(size_t) k] < lm[(size_t) k + 1] || lm[(size_t) k] < minPow) continue;
            const float a = logMag(k - 1), b = logMag(k), c = logMag(k + 1);
            const float d = a - 2.f * b + c, delta = std::abs(d) > 1e-9f ? 0.5f * (a - c) / d : 0.f;
            pf[np] = ((float) k + delta) * binHz;
            pa[np] = std::exp(b - 0.25f * (a - c) * delta);
            used[np] = false;
            ++np;
        }
        if (np == 0) return 0;

        int nn = 0; float firstSal = 0.f;
        while (nn < maxNotes)
        {
            float bestSal = 0.f, bestF = 0.f;
            for (int i = 0; i < np; ++i)
            {
                if (used[i]) continue;
                for (int div = 1; div <= 3; ++div)
                {
                    const float f0 = pf[i] / (float) div;
                    if (f0 < 55.f || f0 > 1200.f) continue;
                    const float sal = salience(f0, false);
                    if (sal > bestSal) { bestSal = sal; bestF = f0; }
                }
            }
            if (bestSal < kMinAmp || (nn > 0 && bestSal < kRelNext * firstSal)) break;
            if (nn == 0) firstSal = bestSal;
            // refine the pitch from the matched partials, take them out, store the note
            float e = 0.f, fsum = 0.f, wsum = 0.f;
            for (int h = 1; h <= kMaxHarm; ++h)
            {
                const int q = nearest(bestF * (float) h);
                if (q < 0) continue;
                used[q] = true; e += pa[q] * pa[q];
                fsum += pa[q] * pf[q] / (float) h; wsum += pa[q];
            }
            if (wsum <= 0.f) break;
            out[nn++] = { fsum / wsum, std::sqrt(e) };
        }
        std::sort(out, out + nn, [](const Note& a, const Note& b) { return a.hz < b.hz; });
        return nn;
    }

private:
    static constexpr int   kMaxPeaks = 96, kMaxHarm = 14;
    static constexpr float kMinAmp = 2e-4f;    // ~ -74 dBFS
    static constexpr float kRelNext = 0.12f;   // further notes must explain at least this much of the first's salience

    // closest unused peak to frequency f within the tolerance (3 %, at least 1.5 bins), or -1
    int nearest(float f) const
    {
        const float tol = std::max(0.03f * f, 1.5f * (float) sampleRate / (float) N);
        int lo = 0, hi = np;
        while (lo < hi) { const int mid = (lo + hi) / 2; if (pf[mid] < f) lo = mid + 1; else hi = mid; }
        int best = -1; float bd = tol;
        for (int q = std::max(0, lo - 2); q < std::min(np, lo + 2); ++q)
            if (! used[q] && std::abs(pf[q] - f) < bd) { bd = std::abs(pf[q] - f); best = q; }
        return best;
    }

    float salience(float f0, bool) const
    {
        float sal = 0.f; int count = 0; bool h1 = false, h2 = false, h3 = false, h5 = false;
        for (int h = 1; h <= kMaxHarm; ++h)
        {
            if (f0 * (float) h > 5000.f) break;
            const int q = nearest(f0 * (float) h);
            if (q < 0) continue;
            sal += pa[q] / std::sqrt((float) h);
            ++count;
            if (h == 1) h1 = true; if (h == 2) h2 = true; if (h == 3) h3 = true; if (h == 5) h5 = true;
        }
        if (! h1)
        {
            // no fundamental: only a real low note's series counts. Its 2nd, 3rd and 5th harmonics must all be there
            // and in tune within 0.5 % (a string's harmonics are; a major triad C-E-G lines up with harmonics 2, 3, 5
            // of a note two octaves down, but equal temperament puts the E ~0.8 % off, so it doesn't pass)
            if (! (h2 && h3 && h5)) return 0.f;
            for (int h : { 2, 3, 5 })
            {
                const int q = nearest(f0 * (float) h);
                if (q < 0 || std::abs(pf[q] - f0 * (float) h) > 0.005f * f0 * (float) h) return 0.f;
            }
        }
        if (count == 1 && f0 < 180.f) return 0.f;     // a lone low partial is noise (string scrape, thump), not a note
        return sal * (1.f + 0.05f * (float) count);
    }

    double sampleRate = 48000.0;
    FftT<12> fft;
    std::array<float, N> ring {}, win {};
    std::array<std::complex<float>, N> buf {};
    std::array<float, N / 2 + 2> lm {};
    float pf[kMaxPeaks] {}, pa[kMaxPeaks] {};
    bool  used[kMaxPeaks] {};
    int   np = 0, w = 0;
    float magNorm = 1.f;
};

template <int Order>
class SwelloEngineT
{
public:
    static constexpr int kFftSize = 1 << Order;
    static constexpr int kHop     = kFftSize / 4;   // 75% overlap
    static constexpr int kBins    = kFftSize / 2 + 1;
    static constexpr int kLatencySamples = kFftSize;

    SwelloEngineT() { buildTables(); prepare(48000.0); }

    void prepare(double sr)
    {
        sampleRate = sr;
        const float dt = (float) kHop / (float) sr;
        refDecay  = std::pow(10.0f, -tuning::refDecayDbPerSec  * dt / 20.0f);
        refRawDecay = std::pow(10.0f, -tuning::refRawDecayDbPerSec * dt / 20.0f);
        envCoef   = 1.0f - std::exp(-dt / (tuning::sustainEnvMs * 0.001f));
        fbHoldDecay = std::pow(10.0f, -tuning::fbHoldDbPerSec * dt / 20.0f);
        frameSec  = dt;
        tracker.prepare(sr);
        trackerEvery = std::max(1, (int) std::lround(0.02 / dt));   // pitch analysis ~every 20 ms
        quietFramesN = std::max(2, (int) std::lround(tuning::quietSec / dt));
        mixCoef   = 1.0f - std::exp(-dt / 0.010f);
        qCoef     = 1.0f - std::exp(-dt / 0.020f);
        ctlCoef   = 1.0f - std::exp(-dt / 0.015f);
        for (int b = 0; b <= kSpecBands; ++b)             // log-spaced band edges for the UI spectrum
        {
            const double f = kSpecLoHz * std::pow((double) kSpecHiHz / kSpecLoHz, (double) b / kSpecBands);
            bandEdge[(size_t) b] = std::clamp((int) std::lround(f * kFftSize / sr), 1, kBins - 1);
        }
        reset();
    }

    void reset()
    {
        inRing.fill(0.f);  outRing.fill(0.f);
        ref.fill(0.f);     refRaw.fill(0.f);  hold.fill(0.f);
        matchPeak.fill(0.f); matchGain.fill(1.f); fbDuck.fill(1.f); fbDriftNow = fbDriftTarget = 0.f; fbDriftCount = 0;
        phase.fill(0.f);   gain.fill(0.f);    env.fill(0.f);   attackMul.fill(1.f); binStrength.fill(0.f); binEven.fill(1.f); magPrev.fill(0.f);
        active.fill(false); quiet.fill(0);
        fbAmt.fill(0.f);   fbHold.fill(0.f);  prevArg.fill(0.f);  subPhPrev.fill(0.f); subPhCur.fill(0.f);
        dRing.fill(0.f);   noteTime.fill(0.f); pan.fill(0.f);     eventPan = 0.f; altSign = 1.f; stereoFrames = 0;
        vRing.fill(0.f);   vdRing.fill(0.f);
        fcBin.fill(0.f);   voiceFrames = 0;
        tracker.reset();   numNotes = 0; numTracks = 0;
        allMidiOff(); trackerDue = false; freshAnalysis = false; fastAnalyses = 0;
        gvL.fill(0.f); gvR.fill(0.f); gvW = 0; sampleCount = 0; evtPending = evtActive = -1; gvAmt = 0.f; gvPhase = 0.f;
        pos = 0; hopCount = 0;
        mix = 0.f; qSm = resToQ(params.resLow);
        freqSm = params.freqHz; modSm = params.mod;
        frameCount = 0; lastQualFrame = 0; eventStartFrame = 0; eventId = 0; haveEvent = false; groupOpen = false;
        refEventDb = -30.f; haveRef = false; groupMul = 1.f; groupPeakE = 0.f; groupLevelDb = -120.f; gapSec = 1e9f;
        binGroup.fill(0);
        for (int b = 0; b < kSpecBands; ++b) { specIn[(size_t) b].store(0.f); specOut[(size_t) b].store(0.f); }
    }

    void setParams(const Params& p) { params = p; }

    // Process one sample. Returns the wet signal (left when SPREAD is on); dryDelayed is the input
    // delayed by kLatencySamples (use it for latency-aligned bypass).
    float process(float in, float& dryDelayed)
    {
        float right = 0.f;
        return processStereo(in, dryDelayed, right);
    }

    // Stereo version: SPREAD places notes left/right. Only the difference R - L is computed (a second
    // inverse FFT, done only while SPREAD is up), so with SPREAD at 0 it costs nothing extra.
    // voiceL / voiceR (optional): the VOICE layer on its own, for the processor's voice effects and MIX.
    float processStereo(float in, float& dryDelayed, float& right, float* voiceL = nullptr, float* voiceR = nullptr)
    {
        constexpr int mask = kFftSize - 1;

        float wet = outRing[(size_t) pos];
        outRing[(size_t) pos] = 0.f;
        right = wet + dRing[(size_t) pos];
        dRing[(size_t) pos] = 0.f;
        tracker.push(in);
        guitarVibrato(wet, right);
        ++sampleCount;
        const float vl = vRing[(size_t) pos];
        if (voiceL) *voiceL = vl;
        if (voiceR) *voiceR = vl + vdRing[(size_t) pos];
        vRing[(size_t) pos] = 0.f; vdRing[(size_t) pos] = 0.f;
        dryDelayed = inRing[(size_t) pos];
        inRing[(size_t) pos] = in;
        pos = (pos + 1) & mask;

        if (++hopCount == kHop) { hopCount = 0; processFrame(); }
        else if (hopCount == kHop / 2 && trackerDue)
        {
            // the pitch analysis runs half a hop after the frame, so its cost and the frame's never land in the same
            // small host buffer (they used to stack up every ~20 ms)
            numNotes = tracker.analyse(notes.data(), kMaxNotes);
            trackerDue = false; freshAnalysis = true;
        }
        return wet;
    }

    // ---- live spectrum for the UI ---------------------------------------------------------------
    // Written once per frame on the audio thread (relaxed atomics, no locks), read by the editor.
    // Per log-spaced band (kSpecLoHz..kSpecHiHz): peak linear amplitude of the input and of the
    // swelled + filtered output. Returns a frame counter so the reader can tell when audio stops.
    static constexpr int   kSpecBands = 64;
    static constexpr float kSpecLoHz = 40.f, kSpecHiHz = 16000.f;
    // Latest note event: counter (changes on every new note/chord), seconds since it started, its
    // effective attack time (after TOUCH/FOLLOW) and its pick strength (-1 soft .. +1 hard).
    struct NoteInfo { uint32_t seq; float ageSec, attackSec, strength, pan, pickStrength; };
    NoteInfo latestNote() const
    {
        const uint32_t frames = frameCountPub.load(std::memory_order_relaxed) - noteFramePub.load(std::memory_order_relaxed);
        return { noteSeqPub.load(std::memory_order_relaxed), (float) frames * frameSecPub.load(std::memory_order_relaxed),
                 noteAttackPub.load(std::memory_order_relaxed), noteStrengthPub.load(std::memory_order_relaxed),
                 notePanPub.load(std::memory_order_relaxed), pickStrengthPub.load(std::memory_order_relaxed) };
    }

    // the notes the voice is playing right now (for the VOICE window): sounding pitch, level, time since the pick
    static constexpr int kVoicePub = 8;
    struct VoiceNote { float hz, level, timeSec, scoop, vowel; };   // scoop 0..1 (still sliding), vowel -1 = off
    int readVoiceNotes(VoiceNote* out) const
    {
        const int n = std::min(voicePubCount.load(std::memory_order_relaxed), kVoicePub);
        for (int i = 0; i < n; ++i)
            out[i] = { voicePubHz[(size_t) i].load(std::memory_order_relaxed), voicePubLevel[(size_t) i].load(std::memory_order_relaxed),
                       voicePubTime[(size_t) i].load(std::memory_order_relaxed), voicePubScoop[(size_t) i].load(std::memory_order_relaxed),
                       voicePubVowel[(size_t) i].load(std::memory_order_relaxed) };
        return n;
    }

    // formant gain of vowel position pos (0 oo .. 0.25 oh .. 0.5 ah .. 0.75 eh .. 1 ee) at f Hz
    // swell gain at phase ph (0..1 over the note's attack). Classic: ph^2. Bowed: measured from the VG-8 BOWED sound
    // (Voyager V-8 renders, spec v2): g(t) = (1 - e^(-t/0.161 s))^2.45 with full level at ~0.5 s, i.e. over the
    // phase k = 0.5 / 0.161 = 3.1, normalised to reach 1 at the end of the attack (S-shaped: slow start).
    static float swellGain(float ph, int shape)
    {
        if (ph >= 1.f) return 1.f;
        if (ph <= 0.f) return 0.f;
        if (shape != 1) return ph * ph;
        const float y = (1.f - std::exp(-tuning::bowedK * ph)) / (1.f - std::exp(-tuning::bowedK));
        return std::pow(y, tuning::bowedExp);
    }
    static float swellPhaseFor(float g, int shape)   // inverse of swellGain
    {
        g = std::clamp(g, 0.f, 1.f);
        if (shape != 1) return std::sqrt(g);
        const float y = std::pow(g, 1.f / tuning::bowedExp);
        return std::clamp(-std::log(1.f - y * (1.f - std::exp(-tuning::bowedK))) / tuning::bowedK, 0.f, 1.f);
    }

    // BODY: the VG-8 BOWED tone, output/input in dB per third-octave (spec v2 section 3.3, smoothed median of 11
    // notes/chords, 0.4..3 s after onset; < 400 Hz and > 6 kHz low confidence) + makeup so the mids sit near 0 dB
    static float bowedBodyDb(float f)
    {
        static constexpr float hz[] = { 63.f, 160.f, 200.f, 250.f, 315.f, 400.f, 500.f, 630.f, 800.f, 1000.f, 1250.f, 1600.f,
                                        2000.f, 2500.f, 3150.f, 4000.f, 5000.f, 6300.f, 8000.f, 10000.f, 12500.f };
        static constexpr float db[] = { -4.f, -4.f, -3.5f, -3.f, -2.f, -0.5f, 0.f, 0.f, -2.f, -3.f, -3.2f, -3.3f,
                                        -3.5f, -3.5f, -4.f, -4.3f, -4.5f, -5.f, -5.5f, -6.f, -6.5f };
        constexpr int n = (int) (sizeof (hz) / sizeof (hz[0]));
        float v;
        if (f <= hz[0]) v = db[0];
        else if (f >= hz[n - 1]) v = db[n - 1];
        else
        {
            int i = 0;
            while (f > hz[i + 1]) ++i;
            const float t = std::log(f / hz[i]) / std::log(hz[i + 1] / hz[i]);
            v = db[i] + (db[i + 1] - db[i]) * t;
        }
        return v + tuning::bodyMakeupDb;
    }

    // VG ("bowed pad"): a bowed-string body, independent of the note: air and wood resonances low, a bridge hill
    // around 2.5-3 kHz, a dip in between, soft highs
    static float bowBody(float f)
    {
        auto pk = [f](float fc, float bw, float a) { const float d = (f - fc) / bw; return a / (1.f + d * d); };
        const float g = 0.45f + pk(285.f, 70.f, 0.7f) + pk(470.f, 120.f, 0.55f) + pk(2800.f, 900.f, 0.8f) - pk(1450.f, 380.f, 0.25f);
        return std::max(0.12f, g) / std::sqrt(1.f + (f / 5500.f) * (f / 5500.f) * (f / 5500.f) * (f / 5500.f));
    }

    static float vowelGain(float pos, float f)
    {
        static constexpr float F[5][3] = { { 300.f, 870.f, 2240.f }, { 480.f, 900.f, 2600.f }, { 730.f, 1090.f, 2440.f },
                                           { 530.f, 1840.f, 2480.f }, { 290.f, 2250.f, 3000.f } };
        static constexpr float B[3] = { 130.f, 170.f, 240.f }, A[3] = { 1.f, 0.65f, 0.3f };
        const float x = std::clamp(pos, 0.f, 1.f) * 4.f;
        const int   i = std::min((int) x, 3);
        const float fr = x - (float) i;
        float g = 0.1f;
        for (int k = 0; k < 3; ++k)
        {
            const float fk = F[i][k] + (F[i + 1][k] - F[i][k]) * fr;
            const float d = (f - fk) / B[k];
            g += A[k] / (1.f + d * d);
        }
        return g;
    }

    void readVoiceSpectrum(float* v) const
    {
        for (int b = 0; b < kSpecBands; ++b) v[b] = specVoice[(size_t) b].load(std::memory_order_relaxed);
    }

    uint32_t readSpectrum(float* in, float* out) const
    {
        for (int b = 0; b < kSpecBands; ++b)
        {
            in[b]  = specIn[(size_t) b].load(std::memory_order_relaxed);
            out[b] = specOut[(size_t) b].load(std::memory_order_relaxed);
        }
        return specSeq.load(std::memory_order_relaxed);
    }

private:
    using Cpx = std::complex<float>;
    static constexpr float kPi = 3.14159265358979323846f;

    // ----------------------------------------------------------------- setup
    void buildTables()
    {
        double sum = 0.0;
        for (int n = 0; n < kFftSize; ++n)
        {
            const double h = 0.5 * (1.0 - std::cos(2.0 * 3.14159265358979323846 * n / kFftSize));
            win[(size_t) n] = (float) std::sqrt(h);
            sum += win[(size_t) n];
        }
        magNorm = (float) (2.0 / sum);                     // sine of amplitude A -> |X|*magNorm = A

        double ola = 0.0;                                  // overlap-add gain of win*win at 75% overlap
        for (int i = 0; i < kFftSize / kHop; ++i)
            ola += (double) win[(size_t) (i * kHop)] * win[(size_t) (i * kHop)];
        olaScale = (float) (1.0 / ola);

        for (int k = 0; k < kBins; ++k)                    // pre-warped bin frequency for the bilinear LPF
            tanBin[(size_t) k] = (float) std::min(std::tan(3.14159265358979323846 * k / kFftSize), 1000.0);
        for (int k = 0; k < kBins; ++k)                    // BODY EQ gain per bin
            bodyGain[(size_t) k] = std::pow(10.f, bowedBodyDb(std::max(20.f, (float) k * (float) sampleRate / (float) kFftSize)) / 20.f);

        // spectrum of the analysis window for a sinusoid offset by d bins (d in -kKernSpan..kKernSpan),
        // used to resynthesise feedback overtones exactly as the STFT would see a real sinusoid
        for (int j = 0; j < kKernSize; ++j)
        {
            const double d = -kKernSpan + (double) j / kKernRes;
            double re = 0.0, im = 0.0;
            for (int n = 0; n < kFftSize; ++n)
            {
                const double a = -2.0 * 3.14159265358979323846 * d * n / kFftSize;
                re += win[(size_t) n] * std::cos(a);
                im += win[(size_t) n] * std::sin(a);
            }
            kern[(size_t) j] = Cpx((float) re, (float) im);
        }
    }

    Cpx kernel(float d) const      // linear interpolation in the window-spectrum table
    {
        const float x = std::clamp((d + (float) kKernSpan) * (float) kKernRes, 0.f, (float) (kKernSize - 1) - 0.001f);
        const int   j = (int) x;
        const float t = x - (float) j;
        return kern[(size_t) j] * (1.f - t) + kern[(size_t) j + 1] * t;
    }

    // tan(pi * x) for x = f / sampleRate, from a table (the filter needs it for every bin and every voice harmonic)
    static float tanPi(float x)
    {
        static const auto table = []
        {
            std::array<float, kTanSize + 2> t {};
            for (int i = 0; i < kTanSize + 2; ++i) t[(size_t) i] = (float) std::tan(3.14159265358979323846 * kTanMax * i / kTanSize);
            return t;
        }();
        const float pos = std::clamp(x, 0.f, kTanMax) * ((float) kTanSize / kTanMax);
        const int   i   = std::min((int) pos, kTanSize - 1);
        const float f   = pos - (float) i;
        return table[(size_t) i] + (table[(size_t) i + 1] - table[(size_t) i]) * f;
    }

    static float resToQ(float r)
    {
        return tuning::resMinQ * std::pow(tuning::resMaxQ / tuning::resMinQ, std::clamp(r, 0.f, 1.f));
    }

    static float wrapPi(float a)
    {
        a = std::fmod(a + kPi, 2.f * kPi);
        if (a < 0.f) a += 2.f * kPi;
        return a - kPi;
    }

    // ----------------------------------------------------------------- STFT
    void processFrame()
    {
        constexpr int mask = kFftSize - 1;

        for (int j = 0; j < kFftSize; ++j)
            spec[(size_t) j] = Cpx(inRing[(size_t) ((pos + j) & mask)] * win[(size_t) j], 0.f);
        fft.forward(spec.data());
        const bool fbOn    = params.feedback > 0.f;
        const bool voiceOn = params.voiceMix > 0.f;
        const bool pitchSpread = params.spread > 0.f && params.spreadMode == 2;
        if (fbOn) std::copy(spec.begin(), spec.begin() + kBins, inSpec.begin());
        if (fbOn || voiceOn) updateInstFreq();                 // true frequency of every bin (phase vocoder)
        // polyphonic pitch (long window) every ~20 ms, and on the next frames after a pick (faster new notes);
        // the analysis itself runs on the half hop (processStereo)
        const bool trackNeeded = voiceOn || pitchSpread || params.midiOut > 0 || params.uiTrack;
        if (fbOn) updateFbModel();
        if (trackNeeded && (frameCount % (uint32_t) trackerEvery == 0u || fastAnalyses > 0))
        {
            trackerDue = true;
            if (fastAnalyses > 0) --fastAnalyses;
        }

        const float dt       = frameSec;
        const float T        = std::max(params.attackMs * 0.001f, 1.0e-4f);
        const float sus      = params.sustain;
        ++frameCount;
        // SUSTAIN: decay rate of the held level (dB/s -> per-frame factor) and the allowed boost
        const float holdDb    = tuning::sustainSlowDbPerSec
                              + (tuning::sustainFastDbPerSec - tuning::sustainSlowDbPerSec) * (1.f - sus) * (1.f - sus);
        const float holdDecay = std::pow(10.0f, -holdDb * dt / 20.0f);
        const float maxBoost  = std::pow(10.0f, params.sustainMaxDb * sus / 20.0f);
        const float fbMaxBoost = std::pow(10.0f, params.sustainMaxDb / 20.0f);   // MAX caps sustain and feedback holds
        const float fbRiseInc  = dt / std::max(params.fbRiseSec, 0.05f);
        const bool  tremOn     = params.tremDepth > 0.f;

        // frame-rate smoothing of the filter controls
        mix   += ((params.filterMode > 0 ? 1.f : 0.f) - mix) * mixCoef;
        qSm   += (resToQ(params.filterMode == 1 ? params.resLow : params.resHigh) - qSm) * qCoef;
        freqSm += (params.freqHz - freqSm) * ctlCoef;
        modSm  += (params.mod    - modSm)  * ctlCoef;

        const bool  filterOn = mix > 0.001f;
        const float invQ     = 1.f / qSm;
        float depthOct = 0.f;
        if (std::abs(modSm) > tuning::modDeadZone)
            depthOct = (std::abs(modSm) - tuning::modDeadZone) / (1.f - tuning::modDeadZone) * tuning::sweepOctaves;
        const float nyq = 0.45f * (float) sampleRate;

        for (int k = 0; k < kBins; ++k)
            mag[(size_t) k] = std::abs(spec[(size_t) k]) * magNorm;
        publishBands(specIn, [this](int k) { return mag[(size_t) k]; });

        // Onsets are judged on a light 3-bin (centre-weighted) power smoothing: random fluctuations of
        // noisy sounds are independent per bin and get averaged down, while a new note next to a
        // ringing one is still seen (wider smoothing smeared them together).
        for (int k = 0; k < kBins; ++k)
        {
            const float a = mag[(size_t) std::max(k - 1, 0)];
            const float c = mag[(size_t) std::min(k + 1, kBins - 1)];
            const float v = mag[(size_t) k];
            magSm[(size_t) k] = std::sqrt(0.25f * a * a + 0.5f * v * v + 0.25f * c * c);
        }

        // Onset pre-pass: which bins start (or re-start) a note this frame, and how much energy that is.
        float onsetEnergy = 0.f;
        frameEnergy = 0.f;
        for (int k = 0; k < kBins; ++k)
        {
            const size_t i = (size_t) k;
            const float m = mag[i];
            frameEnergy += m * m;
            onsetBin[i] = m >= tuning::activateLevel
                       && (! active[i] || magSm[i] > tuning::onsetRatio * ref[i] || m > tuning::onsetRatio * refRaw[i]);
            if (onsetBin[i]) onsetEnergy += m * m;
        }
        noteEvents(onsetEnergy, T);   // groups onsets into note events, sets attackMul for the current one

        for (int k = 0; k < kBins; ++k)
        {
            const float m  = mag[(size_t) k];
            const size_t i = (size_t) k;
            const float phaseInc = dt / (T * attackMul[i]);

            // 1) activity / onset. A quiet bin is "armed" (phase 0, gain 0): whatever
            //    shows up in it, however small or short, has to swell in.
            if (m >= tuning::activateLevel)
            {
                quiet[i] = 0;
                if (onsetBin[i])
                {
                    hold[i] = m; env[i] = m; fbAmt[i] = 0.f; fbHold[i] = m;
                    matchPeak[i] = 0.f; matchGain[i] = 1.f;
                    noteTime[i] = 0.f; pan[i] = eventPan;
                    // small onset (string noise etc.): plain. A bin of the latest note keeps that note's settings even
                    // after its group closed (a pluck keeps flagging onsets for a few frames as it enters the window)
                    if (binGroup[i] != eventId) { attackMul[i] = 1.f; binStrength[i] = 0.f; binEven[i] = 1.f; }
                    // attacks shorter than a frame start part-way in (ATTACK 0 = full level at once, no swell)
                    phase[i] = std::max(0.f, 1.f - T * attackMul[i] / dt);
                    // smooth re-pick: the new note swells in from zero as usual, but the output is not allowed below the
                    // old note's level x repickFloor (~-3 dB) while it does, so it dips a little and re-swells, and the
                    // new pick never jumps out (VG-8 BOWED: 2..4 dB dip, transient hidden)
                    repickFloor[i] = (params.smoothRepick && active[i]) ? gain[i] * magPrev[i] * tuning::repickFloor : 0.f;
                }
                else
                {
                    phase[i] = std::min(1.f, phase[i] + phaseInc); noteTime[i] += dt;
                    if (noteTime[i] >= tuning::matchPeakFrom && noteTime[i] < tuning::matchPeakSec) matchPeak[i] = std::max(matchPeak[i], m);
                }
                active[i] = true;
            }
            else if (m < tuning::releaseLevel)
            {
                if (++quiet[i] >= quietFramesN)
                {
                    active[i] = false; phase[i] = 0.f; hold[i] = 0.f; env[i] = 0.f; ref[i] = 0.f; refRaw[i] = 0.f;
                    binGroup[i] = 0; fbAmt[i] = 0.f; fbHold[i] = 0.f;
                }
                else if (active[i]) phase[i] = std::min(1.f, phase[i] + phaseInc);
            }
            else if (active[i])
                phase[i] = std::min(1.f, phase[i] + phaseInc);   // hysteresis band: keep going

            magPrev[i] = m;
            const float ms = magSm[i];
            ref[i]    = (ms > ref[i])    ? ms : ref[i]    * refDecay;
            refRaw[i] = (m  > refRaw[i]) ? m  : refRaw[i] * refRawDecay;
            env[i] += (m - env[i]) * envCoef;                  // smoothed level: the decay trend, not wiggles
            hold[i] = std::max(env[i], hold[i] * holdDecay);   // may fall no faster than holdDb

            // 2) attack gain
            const float ph = phase[i];
            float g = swellGain(ph, params.swellShape) * binEven[i];
            if (params.bowedBody) g *= bodyGain[i];
            if (repickFloor[i] > 0.f)
            {
                if (ph >= 1.f || g * m >= repickFloor[i]) repickFloor[i] = 0.f;    // the new swell has caught up
                else if (m > 1e-9f) g = std::min(1.f, repickFloor[i] / m);
            }

            // 3) sustain: make up the part of the decay that is faster than the held level allows.
            //    FEEDBACK holds a note too, as its bloom grows (like a note fed by the amp).
            float boost = 1.f;
            if (sus > 0.f && env[i] > 1e-9f)
                boost = std::min(hold[i] / env[i], maxBoost);
            if (fbOn)
            {
                if (active[i] && ph >= 1.f) fbAmt[i] = std::min(1.f, fbAmt[i] + fbRiseInc);   // blooms on held notes
                // amp model: a muted string stops feeding back at once (and starts over if it is let ring again)
                if (params.fbAmp && fbAmt[i] > 0.f && m < tuning::fbMuteRatio * env[i]) { fbAmt[i] = 0.f; fbHold[i] = m; }
                fbHold[i] = std::max(env[i], fbHold[i] * fbHoldDecay);
                const float b = params.fbMode == 1 ? 0.f : fbBloom(i);   // a harmonizer doesn't hold the note
                if (b > 0.f && env[i] > 1e-9f)
                    boost = std::max(boost, std::pow(std::min(fbHold[i] / env[i], fbMaxBoost), b));
            }
            // 3a) level-matched swell: while the note swells in, the string has already decayed, so a long ATTACK ends
            //     quieter than the pick was. Make that up: by the end of the swell the note is as loud as its pick, and
            //     the make-up found there stays (a fixed gain per note). SUSTAIN takes over once it lifts more than
            //     this, so the two never add up and a note is still never louder than its own pick.
            if (params.levelMatch)
            {
                // against the mean of the smoothed and the current level: the smoothed one alone lags a decaying note
                // by ~2 dB (300 ms), the current one alone wobbles with string beating
                if (ph < 1.f && env[i] > 1e-9f) matchGain[i] = std::clamp(matchPeak[i] / (0.5f * (env[i] + m)), 1.f, tuning::matchMaxBoost);
                boost = std::max(boost, 1.f + (matchGain[i] - 1.f) * ph);
            }
            fbDuck[i] = (fbOn && fbTakeover > 0.f) ? 1.f - fbTakeover * fbBloom(i) : 1.f;
            if (boost > 1.f)
            {
                const float gate = std::clamp((env[i] - tuning::sustainFloor) / (3.f * tuning::sustainFloor), 0.f, 1.f);
                g *= 1.f + (boost - 1.f) * gate;
            }
            // 3b) per-note tremolo: this note's own LFO, starting at full level at its pick
            if (tremOn) g *= 1.f - params.tremDepth * tremLfo(noteTime[i]);
            g *= fbDuck[i];      // amp model: the note makes room for its fed-back harmonic
            gain[i] = g;

            // 4) per-note low-pass: cutoff follows THIS bin's own swell position
            fcBin[i] = 0.f;
            if (filterOn && g > 0.f)
            {
                float oct = 0.f;   // (MOD sweep below, PICK offset added before the cutoff)
                if (depthOct > 0.f)
                    oct = (modSm > 0.f) ? -depthOct * (1.f - ph)    // up: starts low, ends at FREQ
                                        : -depthOct * ph;           // down: starts at FREQ, falls
                oct += params.pickFilter * binStrength[i] * params.pickDepthOct;   // filter DYN: per-note brightness
                const float fc = std::clamp(freqSm * std::exp2(oct), 20.f, nyq);
                const float r  = tanBin[i] / tanPi(fc / (float) sampleRate);
                const Cpx H = filterH(r, invQ);
                spec[i] *= Cpx(1.f, 0.f) + (H - Cpx(1.f, 0.f)) * mix;
                fcBin[i] = fc;
            }
        }

        // apply gains (3-tap smoothing across frequency avoids bin-to-bin discontinuities)
        for (int k = 0; k < kBins; ++k)
        {
            const float gm = gain[(size_t) std::max(k - 1, 0)];
            const float gp = gain[(size_t) std::min(k + 1, kBins - 1)];
            spec[(size_t) k] *= 0.25f * gm + 0.5f * gain[(size_t) k] + 0.25f * gp;
        }

        if (fbOn) addFeedback();
        if (pitchSpread) updatePitchPan();
        const bool voiceRun = voiceOn || voiceFrames > 0;   // keep running a few frames to let the rings drain
        if (voiceOn) voiceFrames = kFftSize / kHop; else if (voiceFrames > 0) --voiceFrames;
        if (voiceRun)
        {
            std::fill(vSpec.begin(), vSpec.end(), Cpx(0.f, 0.f));
            if (voiceOn) synthVoice(invQ, true);
            else voicePubCount.store(0, std::memory_order_relaxed);
        }
        if (! voiceOn && (params.midiOut > 0 || params.uiTrack)) synthVoice(invQ, false);   // MIDI only: track the notes, no sound
        else if (! voiceOn && heldN > 0) allMidiOff();

        // SPREAD: per-bin equal-power pan (unity in the centre); the difference R - L goes to its own ring
        const bool spreadOn = params.spread > 0.f;
        if (spreadOn) stereoFrames = kFftSize / kHop;
        if (spreadOn || stereoFrames > 0)
        {
            if (! spreadOn) --stereoFrames;
            for (int k = 0; k < kBins; ++k)
            {
                const float pp = pan[(size_t) k] * params.spread;
                const float th = (pp + 1.f) * 0.25f * kPi;
                panL[(size_t) k] = 1.41421356f * std::cos(th);
                panR[(size_t) k] = 1.41421356f * std::sin(th);
            }
            for (int k = 0; k < kBins; ++k)
            {
                const auto sm = [&](const std::array<float, kBins>& a)
                { return 0.25f * a[(size_t) std::max(k - 1, 0)] + 0.5f * a[(size_t) k] + 0.25f * a[(size_t) std::min(k + 1, kBins - 1)]; };
                const float gl = sm(panL), gr = sm(panR);
                dSpec[(size_t) k] = spec[(size_t) k] * (gr - gl);
                spec[(size_t) k] *= gl;
                if (voiceRun) { vdSpec[(size_t) k] = vSpec[(size_t) k] * (gr - gl); vSpec[(size_t) k] *= gl; }
            }
            if (voiceRun)
            {
                for (int k = 1; k < kFftSize / 2; ++k)
                    vdSpec[(size_t) (kFftSize - k)] = std::conj(vdSpec[(size_t) k]);
                fft.inverse(vdSpec.data());
                for (int j = 0; j < kFftSize; ++j)
                    vdRing[(size_t) ((pos + j) & mask)] += vdSpec[(size_t) j].real() * win[(size_t) j] * olaScale;
            }
            for (int k = 1; k < kFftSize / 2; ++k)
                dSpec[(size_t) (kFftSize - k)] = std::conj(dSpec[(size_t) k]);
            fft.inverse(dSpec.data());
            for (int j = 0; j < kFftSize; ++j)
                dRing[(size_t) ((pos + j) & mask)] += dSpec[(size_t) j].real() * win[(size_t) j] * olaScale;
        }

        publishBands(specOut, [this](int k) { return std::abs(spec[(size_t) k]) * magNorm; });
        if (voiceRun)
        {
            publishBands(specVoice, [this](int k) { return std::abs(vSpec[(size_t) k]) * magNorm; });
            for (int k = 1; k < kFftSize / 2; ++k)
                vSpec[(size_t) (kFftSize - k)] = std::conj(vSpec[(size_t) k]);
            fft.inverse(vSpec.data());
            for (int j = 0; j < kFftSize; ++j)
                vRing[(size_t) ((pos + j) & mask)] += vSpec[(size_t) j].real() * win[(size_t) j] * olaScale;
        }
        else if (specVoice[0].load(std::memory_order_relaxed) != 0.f || specVoice[20].load(std::memory_order_relaxed) != 0.f)
            for (auto& v : specVoice) v.store(0.f, std::memory_order_relaxed);
        specSeq.fetch_add(1, std::memory_order_relaxed);
        frameCountPub.store(frameCount, std::memory_order_relaxed);
        frameSecPub.store(frameSec, std::memory_order_relaxed);
        for (int k = 1; k < kFftSize / 2; ++k)
            spec[(size_t) (kFftSize - k)] = std::conj(spec[(size_t) k]);

        fft.inverse(spec.data());

        for (int j = 0; j < kFftSize; ++j)
            outRing[(size_t) ((pos + j) & mask)] += spec[(size_t) j].real() * win[(size_t) j] * olaScale;
    }

    // a peak's fractional bin: parabolic interpolation on the log magnitude
    float instBinOr(size_t i, float k) const
    {
        const float a = std::log(mag[i - 1] + 1e-12f), b = std::log(mag[i] + 1e-12f), c = std::log(mag[i + 1] + 1e-12f);
        const float d = a - 2.f * b + c;
        return std::abs(d) > 1e-9f ? k + 0.5f * (a - c) / d : k;
    }

    // per-note tremolo LFO: 0 at the pick (full level), 1 at the deepest point
    float tremLfo(float t) const
    {
        float ph = t * params.tremRateHz;
        ph -= std::floor(ph);
        if (params.tremShape == 1) return ph < 0.5f ? 2.f * ph : 2.f - 2.f * ph;                       // triangle
        if (params.tremShape == 2) return std::clamp((ph < 0.5f ? ph : 1.f - ph) * 8.f - 1.5f, 0.f, 1.f); // soft square
        return 0.5f - 0.5f * std::cos(2.f * kPi * ph);                                                   // sine
    }

    // bloom of a bin's feedback, 0..1, scaled by FEEDBACK: nothing for the first quarter of the rise time
    // (feedback needs a moment to build up), then an S-curve to full
    float fbBloom(size_t i) const
    {
        if (params.fbMode == 1) return params.feedback;                 // harmonizer: there from the pick
        const float x = std::clamp((fbAmt[i] - 0.25f) / 0.75f, 0.f, 1.f);
        return params.feedback * x * x * (3.f - 2.f * x);
    }

    // FEEDBACK: every held partial (spectral peak) gets an overtone at ratio x its true frequency
    // (phase-vocoder estimate), resynthesised as a sinusoid with the window's exact spectral shape. Its
    // phase is ratio x the partial's own phase, so it is locked to the note frame after frame (like the
    // harmonic an amp adds). Scaled by the bloom, held up like the note, rolled off above fbToneHz.
    // FEEDBACK, amp model (PolySwell II, fbAmp): where the guitar stands decides which harmonic of a held note the
    // amp picks up. DISTANCE walks a ladder: the note itself, its octave, the 12th, two octaves, two octaves + a third;
    // mostly it sits on one of them and crosses over between two. It also wanders a little by itself, as a player never
    // stands still. Called once per frame.
    void updateFbModel()
    {
        fbRatioA = fbRatioB = 0.f; fbWeightA = 1.f; fbWeightB = 0.f; fbTakeover = 0.f;
        if (! params.fbAmp || params.fbMode == 1) return;
        if (--fbDriftCount <= 0)
        {
            fbRng = fbRng * 1664525u + 1013904223u;
            fbDriftTarget = ((float) (fbRng >> 8) * (1.f / 16777216.f) - 0.5f) * 2.f * tuning::fbDrift;
            fbDriftCount = (int) ((0.8f + 1.2f * (float) ((fbRng >> 4) & 255u) / 255.f) / std::max(frameSec, 1e-4f));
        }
        fbDriftNow += (fbDriftTarget - fbDriftNow) * std::min(1.f, frameSec / 0.6f);
        static const float ladder[5] = { 1.f, 2.f, 3.f, 4.f, 5.f };
        const float pos = std::clamp(params.fbDistance + fbDriftNow, 0.f, 1.f) * 4.f;
        const int ia = std::min(3, (int) pos);
        const float x = std::clamp((pos - (float) ia - 0.3f) / 0.4f, 0.f, 1.f), w = x * x * (3.f - 2.f * x);
        fbRatioA = ladder[ia]; fbRatioB = ladder[ia + 1]; fbWeightA = 1.f - w; fbWeightB = w;
        // the fed-back harmonic takes over from the note (not when the "harmonic" is the note itself)
        fbTakeover = tuning::fbTakeover * ((ia == 0 ? 0.f : fbWeightA) + fbWeightB);
    }

    // FEEDBACK: every held partial (spectral peak) gets an overtone at ratio x its true frequency
    // (phase-vocoder estimate), resynthesised as a sinusoid with the window's exact spectral shape. Its
    // phase is ratio x the partial's own phase, so it is locked to the note frame after frame (like the
    // harmonic an amp adds). Scaled by the bloom, held up like the note, rolled off above fbToneHz.
    void addFeedback()
    {
        const bool  amp      = params.fbAmp && params.fbMode != 1;
        const float ratio    = params.fbHarmonic == 1 ? 3.f : params.fbHarmonic == 2 ? 1.f
                             : params.fbHarmonic == 3 ? 0.5f : 2.f;
        const bool  sub      = ! amp && ratio < 1.f;   // a phase can't be halved: the sub-octave keeps its own running phase
        if (sub) subPhCur.fill(0.f);
        const float binHz    = (float) sampleRate / (float) kFftSize;
        const float expected = 2.f * kPi * (float) kHop / (float) kFftSize;   // phase advance per bin index
        const int   maxK     = std::min(kBins - 2, (int) (tuning::fbMaxSourceHz / binHz));
        const float fbMaxBoost = std::pow(10.0f, params.sustainMaxDb / 20.0f);

        // amp model, "one note wins": the loudest blooming partial takes most of the feedback, as in a real amp
        float strongest = 0.f;
        if (amp && params.fbOneWins)
            for (int k = 2; k <= maxK; ++k)
            {
                const size_t i = (size_t) k;
                if (mag[i] < tuning::fbPeakMin || mag[i] <= mag[i - 1] || mag[i] < mag[i + 1]) continue;
                strongest = std::max(strongest, mag[i] * fbBloom(i));
            }

        for (int k = 2; k <= maxK; ++k)
        {
            const size_t i = (size_t) k;
            const float m = mag[i];
            if (m < tuning::fbPeakMin || m <= mag[i - 1] || m < mag[i + 1]) continue;   // spectral peak only
            const float b = fbBloom(i);
            if (b <= 0.001f) continue;
            const float win = strongest > 0.f ? std::pow(std::min(1.f, m * b / strongest), tuning::fbWinPower) : 1.f;

            const float kTrue = std::clamp(instBin[i], (float) k - 1.f, (float) k + 1.f);
            // the partial as a sinusoid: amplitude and phase at the frame start
            const Cpx   kIn  = kernel((float) k - kTrue);
            const float kInM = std::max(std::abs(kIn), 1e-6f);
            const float A    = 2.f * std::abs(inSpec[i]) / kInM;
            const float th   = std::arg(inSpec[i]) - std::arg(kIn);
            const float held = (params.fbMode == 1 || env[i] <= 1e-9f) ? 1.f : std::min(fbHold[i] / env[i], fbMaxBoost);

            for (int pass = 0; pass < (amp ? 2 : 1); ++pass)
            {
                const float r     = amp ? (pass == 0 ? fbRatioA : fbRatioB) : ratio;
                const float share = amp ? (pass == 0 ? fbWeightA : fbWeightB) * win : 1.f;
                if (share < 0.01f) continue;
                if (amp && r == 1.f) continue;      // the note itself: its hold boost is the feedback, nothing to add
                const float kOut  = r * kTrue;
                if (kOut + (float) tuning::fbKernelHalf >= (float) (kBins - 1) || kOut < 1.5f) continue;
                const float tone = 1.f / std::sqrt(1.f + std::pow(kOut * binHz / std::max(params.fbToneHz, 100.f), 4.f));
                // harmonizer: the copy follows the note's own swell/tremolo; bloom: the swell is long done, and
                // the note's gain already carries the feedback hold (using it here would apply the hold twice)
                const float Aout = tuning::fbLevel * b * tone * held * A * share * (params.fbMode == 1 ? gain[i] : 1.f);
                float outPhase = wrapPi(r * th);
                if (sub)
                {
                    // continue this partial's sub-octave phase from the last frame (look one bin either side)
                    float prev = subPhPrev[i];
                    if (prev == 0.f) prev = subPhPrev[i - 1] != 0.f ? subPhPrev[i - 1] : subPhPrev[i + 1];
                    outPhase = wrapPi(prev + expected * kOut);
                    for (int d = -1; d <= 1; ++d) subPhCur[(size_t) (k + d)] = outPhase == 0.f ? 1e-6f : outPhase;
                }
                const Cpx   ph   = std::polar(0.5f * Aout, outPhase);

                const int j0 = (int) std::floor(kOut) - tuning::fbKernelHalf + 1;
                for (int j = j0; j < j0 + 2 * tuning::fbKernelHalf; ++j)
                    if (j >= 1) spec[(size_t) j] += ph * kernel((float) j - kOut);
            }
        }
        if (sub) std::swap(subPhPrev, subPhCur);
    }

    // Phase vocoder: each bin's true frequency (in bins) from its phase advance since the last frame.
    void updateInstFreq()
    {
        const float expected = 2.f * kPi * (float) kHop / (float) kFftSize;
        for (int k = 0; k < kBins; ++k)
        {
            const float a = std::arg(spec[(size_t) k]);
            instBin[(size_t) k] = (float) k + wrapPi(a - prevArg[(size_t) k] - expected * (float) k) / expected;
            prevArg[(size_t) k] = a;
        }
    }

    // per-note vibrato factor (frequency ratio) for a bin, fading in after the delay
    float vibFactor(size_t i) const
    {
        if (params.vibCents <= 0.f || params.vibTarget == 1) return 1.f;   // target "guitar": the voice stays straight
        const float t   = noteTime[i];
        if (params.vibHuman)
        {
            // a violinist: the vibrato deepens over ~0.9 s, starts ~15 % slower and wanders a little in rate (each note
            // its own way: the wander's phase comes from the note's bin)
            const float x = t - params.vibDelaySec;
            if (x <= 0.f) return 1.f;
            const float a = std::min(1.f, x / 0.9f), amt = a * a * (3.f - 2.f * a);
            const float ramp = x < 1.2f ? 0.85f * x + 0.0625f * x * x : 1.11f + (x - 1.2f);   // integral of 0.85 .. 1.0 rate
            const float wob  = 1.f + 0.05f * std::sin(2.f * kPi * 0.37f * x + 0.7f * (float) i);
            return std::exp2(params.vibCents * amt * std::sin(2.f * kPi * params.vibRateHz * ramp * wob) / 1200.f);
        }
        const float amt = std::clamp((t - params.vibDelaySec) / tuning::vibFadeSec, 0.f, 1.f);
        if (amt <= 0.f) return 1.f;
        return std::exp2(params.vibCents * amt * std::sin(2.f * kPi * params.vibRateHz * (t - params.vibDelaySec)) / 1200.f);
    }

    // continue a running phase stored per bin (look one bin either side), advanced by kOut bins of frequency
    static float runPhase(const std::array<float, kBins>& prev, std::array<float, kBins>& cur, int k, float kOut)
    {
        constexpr float expected = 2.f * 3.14159265358979323846f * (float) kHop / (float) kFftSize;
        float p0 = prev[(size_t) k];
        if (p0 == 0.f) p0 = prev[(size_t) std::max(k - 1, 0)] != 0.f ? prev[(size_t) std::max(k - 1, 0)] : prev[(size_t) std::min(k + 1, kBins - 1)];
        const float ph = wrapPi(p0 + expected * kOut);
        for (int d = -1; d <= 1; ++d) cur[(size_t) std::clamp(k + d, 0, kBins - 1)] = ph == 0.f ? 1e-6f : ph;
        return ph;
    }

    // add a sinusoid of amplitude A at kOut bins with phase ph into a spectrum, shaped like the STFT sees it
    void addSine(std::array<Cpx, kFftSize>& dst, float kOut, float A, float ph) const
    {
        const Cpx c = std::polar(0.5f * A, ph);
        const int j0 = (int) std::floor(kOut) - tuning::fbKernelHalf + 1;
        for (int j = j0; j < j0 + 2 * tuning::fbKernelHalf; ++j)
            if (j >= 1 && j < kBins) dst[(size_t) j] += c * kernel((float) j - kOut);
    }

    // the same, without complex maths (magnitude only), with tan(pi fc / sr) passed in once per note
    float filterMagFast(float kOut, float tanFc, float invQ) const
    {
        if (tanFc <= 0.f) return 1.f;
        const float r = tanPi(kOut / (float) kFftSize) / tanFc, r2 = r * r;
        float m;
        if (params.slope <= 0) m = 1.f / std::sqrt(1.f + r2);
        else
        {
            const float res = 1.f / std::sqrt((1.f - r2) * (1.f - r2) + r2 * invQ * invQ);
            const float flat = params.slope == 1 ? 1.f : 1.f / std::sqrt(1.f + r2 * r2);
            m = params.slope == 1 ? res : params.slope == 2 ? res * flat : res * flat * flat * flat;
        }
        return 1.f + (m - 1.f) * mix;
    }

    // magnitude of the note's own low-pass at output frequency kOut (bins), using its bin's current cutoff
    float filterMagAt(size_t src, float kOut, float invQ) const
    {
        if (fcBin[src] <= 0.f || mix <= 0.001f) return 1.f;
        const float r = std::tan(kPi * std::min(kOut / (float) kFftSize, 0.49f)) / std::tan(kPi * fcBin[src] / (float) sampleRate);
        return 1.f + (std::abs(filterH(r, invQ)) - 1.f) * mix;
    }

    // VIBRATO on the guitar: a clean pitch bend of the whole guitar signal (short modulated delay) that restarts
    // with every new note and fades in after the delay. Rebuilding single partials per note (the first version)
    // made artifacts on low notes, whose partials are too close together to separate.
    void guitarVibrato(float& l, float& r)
    {
        const bool on = params.vibCents > 0.f && params.vibTarget >= 1;   // "guitar" or "guitar + voice"
        if (! on && gvAmt < 1e-5f) return;
        if (evtPending >= 0 && (int64_t) sampleCount >= evtPending && evtPending > evtActive) evtActive = evtPending;
        const float sr  = (float) sampleRate;
        const float age = evtActive >= 0 ? (float) ((int64_t) sampleCount - evtActive) / sr : -1.f;
        float target = (on && age >= 0.f) ? std::clamp((age - params.vibDelaySec) / tuning::vibFadeSec, 0.f, 1.f) : 0.f;
        if (params.vibHuman && on && age >= 0.f)
        {
            const float a = std::clamp((age - params.vibDelaySec) / 0.9f, 0.f, 1.f);
            target = a * a * (3.f - 2.f * a);
        }
        gvAmt += (target - gvAmt) * (1.f - std::exp(-1.f / (0.02f * sr)));
        const float w = 2.f * kPi * std::max(params.vibRateHz, 0.5f);
        if (params.vibHuman && age >= params.vibDelaySec && target > 0.f)
        {
            const float x = age - params.vibDelaySec;
            if (x < 1.f / sr) gvPhase = 0.f;
            gvPhase += w / sr * (0.85f + 0.15f * std::min(1.f, x / 1.2f)) * (1.f + 0.05f * std::sin(2.f * kPi * 0.37f * x));
        }
        else if (age >= params.vibDelaySec && target > 0.f) gvPhase = w * (age - params.vibDelaySec);
        else if (gvAmt > 1e-4f)                        gvPhase += w / sr;      // let the old vibrato fade out smoothly
        else                                           gvPhase = 0.f;
        const float A = (std::exp2(std::max(params.vibCents, 0.f) / 1200.f) - 1.f) / w * gvAmt;   // seconds
        const float d = A * (1.f - std::cos(gvPhase)) * sr;                                       // samples, >= 0
        gvL[(size_t) gvW] = l; gvR[(size_t) gvW] = r;
        const float rp = (float) gvW - d, fl = std::floor(rp), fr = rp - fl;
        const int i0 = (int) fl & (kGvSize - 1), i1 = (i0 + 1) & (kGvSize - 1);
        l = gvL[(size_t) i0] + (gvL[(size_t) i1] - gvL[(size_t) i0]) * fr;
        r = gvR[(size_t) i0] + (gvR[(size_t) i1] - gvR[(size_t) i0]) * fr;
        gvW = (gvW + 1) & (kGvSize - 1);
    }

    // the 1024-point bin that carries a note right now: the most swelled of its first three harmonics' bins
    int noteBin(float hz) const
    {
        const float binHz = (float) sampleRate / (float) kFftSize;
        int best = std::clamp((int) std::lround(hz / binHz), 1, kBins - 2); float bg = -1.f;
        for (int h = 1; h <= 3; ++h)
        {
            const int k = (int) std::lround(hz * (float) h / binHz);
            if (k < 1 || k >= kBins - 1) break;
            const float g = active[(size_t) k] ? gain[(size_t) k] : 0.f;
            if (g > bg) { bg = g; best = k; }
        }
        return best;
    }

    // SPREAD by pitch: place the latest note by its real pitch (from the long-window tracker), refined for the
    // first 200 ms of the note because the long window needs a moment to see a new note
    void updatePitchPan()
    {
        const float age = (float) (frameCount - eventStartFrame) * frameSec;
        if (! haveEvent || age > 0.2f || numNotes == 0) return;
        const float binHz = (float) sampleRate / (float) kFftSize;
        float hz = 0.f;
        for (int n = 0; n < numNotes && hz == 0.f; ++n)          // lowest note that belongs to the latest event
            for (int h = 1; h <= 3; ++h)
            {
                const int k = (int) std::lround(notes[(size_t) n].hz * (float) h / binHz);
                if (k > 0 && k < kBins && binGroup[(size_t) k] == eventId) { hz = notes[(size_t) n].hz; break; }
            }
        if (hz == 0.f) return;
        eventPan = std::clamp(std::log2(hz / 82.4f) / 2.f * 2.f - 1.f, -1.f, 1.f);   // low E = left, E two octaves up = right
        notePanPub.store(eventPan * params.spread, std::memory_order_relaxed);
        for (int k = 0; k < kBins; ++k)
            if (binGroup[(size_t) k] == eventId) pan[(size_t) k] = eventPan;
    }

    // VOICE: every note from the pitch tracker is rebuilt as a synth tone: harmonics weighted by the wave, the
    // note's own swell / sustain / tremolo gain and filter (from the bin that carries it), TONE and STAB, octave
    // and vibrato. Notes are matched to last frame's by pitch, so each keeps a continuous phase.
    void synthVoice(float invQ, bool audio)
    {
        const float binHz = (float) sampleRate / (float) kFftSize;
        const float octR  = params.voiceOct == 0 ? 0.5f : params.voiceOct == 2 ? 2.f : 1.f;
        constexpr float expected = 2.f * kPi * (float) kHop / (float) kFftSize;
        // a note must be seen by two pitch analyses in a row before it sounds (one-off phantoms between ringing
        // notes made the voice "gargle"), low notes by three (string thumps)
        const int confirmFrames    = std::max((int) std::lround(0.015f / frameSec), trackerEvery + 1);
        const int confirmFramesLow = std::max((int) std::lround(0.030f / frameSec), 2 * trackerEvery + 1);
        const int holdFrames       = 3 * trackerEvery;   // a note missing from one or two analyses is held, not dropped
        // confirmed = seen by 2 analyses (low notes 3) and at least 15 / 30 ms old
        auto confirmCheck = [&](Track& tr)
        {
            const bool low = tr.hz < 150.f;
            if (tr.confirmedAt < 0 && tr.seen >= (low ? 3 : 2) && tr.age >= (low ? confirmFramesLow / 2 : confirmFrames / 2))
                tr.confirmedAt = tr.age;
        };
        const bool pwm = params.voiceWave == 2;
        if (! pwm)
        {
            float norm = 0.f;
            for (int h = 1; h <= kMaxHarm; ++h) { waveW[(size_t) h] = waveWeight(h, 0.5f); norm += waveW[(size_t) h] * waveW[(size_t) h]; }
            norm = 1.f / std::sqrt(std::max(norm, 1e-9f));
            for (int h = 1; h <= kMaxHarm; ++h) waveW[(size_t) h] *= norm;
        }
        const float T = std::max(params.attackMs * 0.001f, 1.0e-4f);
        // WARMTH: a little more 2nd and 3rd harmonic, softer highs (per note, so chords stay clean)
        std::array<float, kMaxHarm + 1> warmW {};
        for (int h = 1; h <= kMaxHarm; ++h)
        {
            const float w = params.voiceWarmth, x = (float) (h - 1) / 5.f;
            warmW[(size_t) h] = (1.f + w * (h == 2 ? 0.7f : h == 3 ? 0.4f : 0.f)) / std::sqrt(1.f + w * x * x);
        }
        const bool  vowelOn = params.voiceVowel > 0.001f;
        const float vowelAmt = std::clamp(params.voiceVowel * 20.f, 0.f, 1.f);
        const float vowelPos = std::clamp((params.voiceVowel - 0.05f) / 0.95f, 0.f, 1.f);
        const float scoopCents = 60.f * params.voiceScoop;
        const bool  bowed = params.voiceWave == 7;
        int pubN = 0;
        Track next[kMaxTracks]; int nn = 0;
        int newIdx[8] {}; int newCount = 0;
        bool anyBreath = false;

        // match this frame's notes to the running tracks (by pitch)
        bool used[kMaxTracks] {};
        for (int n = 0; n < numNotes && nn < kMaxTracks; ++n)
        {
            const float hz = notes[(size_t) n].hz;
            int m = -1;
            // the same note (within 3 %); or, when no pick just happened, the nearest held note within ~2.5 semitones:
            // a bend or slide keeps its track (and its MIDI note, bent) instead of starting a new note
            for (int t = 0; t < numTracks; ++t)
                if (! used[t] && std::abs(tracks[(size_t) t].hz / hz - 1.f) < 0.03f) { m = t; break; }
            if (m < 0 && frameCount - eventStartFrame > (uint32_t) (2 * trackerEvery))
            {
                float best = tuning::bendFollowRatio;
                for (int t = 0; t < numTracks; ++t)
                {
                    const float d = std::abs(std::log2(tracks[(size_t) t].hz / hz));
                    if (! used[t] && tracks[(size_t) t].confirmedAt >= 0 && d < best) { best = d; m = t; }
                }
            }
            Track tr = m >= 0 ? tracks[(size_t) m] : Track {};
            if (m >= 0) used[m] = true;
            // smoothed level: notes sharing partials (root, fifth, ninth) trade them between analyses
            tr.amp = (m >= 0) ? tr.amp + (notes[(size_t) n].amp - tr.amp) * 0.3f : notes[(size_t) n].amp;
            tr.hz = hz; tr.age = (m >= 0 ? tr.age : 0) + 1; tr.lost = 0;
            if (m < 0) { tr.seen = 0; tr.confirmedAt = -1; tr.midiNote = -1; tr.id = ++trackIdCounter; }
            if (freshAnalysis || m < 0) ++tr.seen;
            confirmCheck(tr);
            if (m < 0)   // a new note: its own LIFE (each note different)
            {
                tr.lp = tr.lpT = rand11(); tr.lt = tr.ltT = rand11(); tr.la = tr.laT = rand11();
                tr.pwOff = 3.14159265f * (rand11() + 1.f); tr.lifeCount = 0;
            }
            if (m < 0) newIdx[newCount++ < 8 ? newCount - 1 : 7] = nn;
            next[nn++] = tr;
        }
        // GLIDE: a single new note (not a chord, not a re-pick of the same note) slides from the previous lead note
        if (params.voiceGlide > 0.001f && newCount == 1 && lastLeadHz > 0.f
            && (float) (frameCount - lastLeadFrame) * frameSec < tuning::glideMemorySec)
        {
            Track& g = next[newIdx[0]];
            const float d = std::log2(lastLeadHz / g.hz);
            if (std::abs(d) > 0.04f && std::abs(d) < 1.05f) { g.glideOct = d; g.glideLeft = 1.f; }
        }
        // tracks that vanished fade out over a few frames instead of stopping dead
        for (int t = 0; t < numTracks && nn < kMaxTracks; ++t)
            if (! used[t] && tracks[(size_t) t].lost < holdFrames && tracks[(size_t) t].confirmedAt >= 0)
            {
                Track tr = tracks[(size_t) t];
                ++tr.lost;
                tr.amp *= tr.lost <= 2 * trackerEvery ? 0.97f : 0.6f;   // held through a missed analysis, then fades
                ++tr.age;
                next[nn++] = tr;
            }

        int lead = -1;
        for (int n = 0; n < nn; ++n)
            if (next[n].confirmedAt >= 0 && next[n].lost == 0 && (lead < 0 || next[n].age - next[n].confirmedAt < next[lead].age - next[lead].confirmedAt))
                lead = n;
        if (lead >= 0) { lastLeadHz = next[lead].hz; lastLeadFrame = frameCount; }

        for (int n = 0; n < nn; ++n)
        {
            Track& tr = next[n];
            // MONO: only the newest note sounds; the others fade out quickly
            if (params.voiceMono && lead >= 0) tr.monoGain = n == lead ? std::min(1.f, tr.monoGain + 0.34f) : tr.monoGain * 0.55f;
            else tr.monoGain = 1.f;
            const int    sb = noteBin(tr.hz);
            const size_t s  = (size_t) sb;
            const float  g  = active[s] ? gain[s] : (tr.lost > 0 ? gain[s] : 0.f);
            // SCOOP: from the moment this voice note is heard, ease up into pitch (smoothstep) over ~1/3 of its attack
            float scoopLeft = 0.f;
            if (scoopCents > 0.f)
            {
                const float heard = tr.confirmedAt >= 0 ? (float) (tr.age - tr.confirmedAt) * frameSec : 0.f;
                const float dur   = std::clamp(0.35f * T * attackMul[s], 0.07f, 0.3f);
                const float x     = std::min(1.f, heard / dur);
                scoopLeft = 1.f - x * x * (3.f - 2.f * x);
            }
            const float  scoopF = scoopLeft > 0.f ? std::exp2(-scoopCents * scoopLeft / 1200.f) : 1.f;
            const float life = params.voiceLife;
            if (life > 0.f)
            {
                if (--tr.lifeCount <= 0)   // a new target every ~0.3 .. 0.9 s
                {
                    tr.lpT = rand11(); tr.ltT = rand11(); tr.laT = rand11();
                    tr.lifeCount = (int) ((0.6f + 0.3f * rand11()) / frameSec);
                }
                const float k = std::min(1.f, frameSec / 0.35f);
                tr.lp += (tr.lpT - tr.lp) * k; tr.lt += (tr.ltT - tr.lt) * k; tr.la += (tr.laT - tr.la) * k;
            }
            const float  lifeF = life > 0.f ? std::exp2(tuning::lifeCents * life * tr.lp / 1200.f) : 1.f;
            float glideF = 1.f;
            if (tr.glideLeft > 0.f)
            {
                if (tr.confirmedAt >= 0) tr.glideLeft = std::max(0.f, tr.glideLeft - frameSec / std::max(params.voiceGlide, 0.01f));
                const float x = tr.glideLeft;
                glideF = std::exp2(tr.glideOct * x * x * (3.f - 2.f * x));   // eases from the old pitch into the new
            }
            const float  kBase = tr.hz / binHz * octR * vibFactor(s) * scoopF * lifeF * glideF;
            tr.ph = wrapPi(tr.ph + expected * kBase);
            const float ramp = tr.confirmedAt < 0 ? 0.f : std::clamp((float) (tr.age - tr.confirmedAt + 1) / 3.f, 0.f, 1.f);
            if (! audio || ramp <= 0.f || g <= 0.0005f) continue;   // (MIDI only: the tracks are all we need)

            const float t    = noteTime[s];
            const float A    = tr.amp * g * ramp * tr.monoGain * tuning::voiceLevel * (life > 0.f ? std::pow(10.f, tuning::lifeDb * life * tr.la / 20.f) : 1.f);
            // BOW wave: bow pressure from your picking (harder = brighter, grittier)
            const bool  bow = params.voiceWave == 8;
            const float pressure = bow ? std::clamp(0.5f + 0.5f * binStrength[s], 0.f, 1.f) : 0.f;
            // STAB: the tone opens with the note's own swell and settles 0.18 s after it is fully in, so it is heard
            // with long attacks too (it used to fade within 0.12 s of the pick, while the note was still silent)
            const float ph    = phase[s];
            const float stabE = ph < 1.f ? ph * ph : std::exp(-std::max(0.f, t - T * attackMul[s]) / std::max(params.voiceDecay, 0.02f));
            const float tone  = std::max(params.voiceToneHz, 100.f) * (1.f + params.voiceStab * 8.f * stabE)
                              * (life > 0.f ? std::exp2(tuning::lifeOct * life * tr.lt) : 1.f)
                              * (bow ? std::exp2(0.7f * (pressure - 0.5f)) : 1.f);
            const float invTone = binHz / tone;
            const float invQ2   = 1.f / ((0.7071f + 7.f * params.voiceRes * params.voiceRes) * (0.7071f + 7.f * params.voiceRes * params.voiceRes));
            std::array<float, kMaxHarm + 1> wl {};
            const float* wv = waveW.data();
            if (pwm)
            {
                const float pw = std::clamp(0.5f + 0.42f * std::sin(2.f * kPi * params.voicePwmRate * t + (life > 0.f ? tr.pwOff * life : 0.f)), 0.08f, 0.92f);
                float norm = 0.f;
                for (int h = 1; h <= kMaxHarm; ++h) { wl[(size_t) h] = waveWeight(h, pw); norm += wl[(size_t) h] * wl[(size_t) h]; }
                norm = 1.f / std::sqrt(std::max(norm, 1e-9f));
                for (int h = 1; h <= kMaxHarm; ++h) wl[(size_t) h] *= norm;
                wv = wl.data();
            }
            std::array<float, kMaxHarm + 1> ws {};
            if (params.voiceString > 0.001f)
            {
                // the guitar's own harmonics of this note right now (the louder of the two nearest bins), normalised
                float gn = 0.f;
                std::array<float, kMaxHarm + 1> gh {};
                for (int h = 1; h <= kMaxHarm; ++h)
                {
                    const float kb = tr.hz / binHz * (float) h;
                    if (kb >= (float) (kBins - 2)) break;
                    const int k0 = (int) kb;
                    gh[(size_t) h] = std::max(mag[(size_t) k0], mag[(size_t) k0 + 1]);
                    gn += gh[(size_t) h] * gh[(size_t) h];
                }
                gn = gn > 1e-18f ? 1.f / std::sqrt(gn) : 0.f;
                const float f = params.voiceString;
                for (int h = 1; h <= kMaxHarm; ++h) ws[(size_t) h] = (1.f - f) * wv[h] + f * gh[(size_t) h] * gn;
                wv = ws.data();
            }
            const float tanFc = (fcBin[s] > 0.f && mix > 0.001f) ? tanPi(fcBin[s] / (float) sampleRate) : 0.f;
            // VOWEL: this note's vowel, a little more closed while it swells in, opened by a hard pick (BY PICK)
            const float vpos = std::clamp(vowelPos + params.voiceVowelPick * binStrength[s] * 0.35f - 0.25f * (1.f - std::min(ph, 1.f)), 0.f, 1.f);
            std::array<float, kMaxHarm + 1> hw {};
            float e0 = 0.f, e1 = 0.f;
            for (int h = 1; h <= kMaxHarm; ++h)
            {
                const float kOut = kBase * (float) h;
                if (wv[h] <= 0.f || kOut + (float) tuning::fbKernelHalf >= (float) (kBins - 1)) continue;
                // voice filter: 2-pole low-pass at TONE, resonance RES (RES 0 = the old Butterworth roll-off)
                const float x2   = (kOut * invTone) * (kOut * invTone);
                const float base = wv[h] * warmW[(size_t) h] / std::sqrt((1.f - x2) * (1.f - x2) + x2 * invQ2);
                float v = vowelOn ? base * (1.f + vowelAmt * (vowelGain(vpos, kOut * binHz) - 1.f)) : base;
                if (bowed) v *= bowBody(kOut * binHz);
                hw[(size_t) h] = v; e0 += base * base; e1 += v * v;
            }
            const float norm = ((vowelOn || bowed) && e1 > 1e-12f) ? std::sqrt(e0 / e1) : 1.f;   // shapes, doesn't change the level
            for (int h = 1; h <= kMaxHarm; ++h)
            {
                if (hw[(size_t) h] <= 0.f) continue;
                const float kOut = kBase * (float) h;
                float amp  = A * hw[(size_t) h] * norm * filterMagFast(kOut, tanFc, invQ);
                if (amp < 1e-7f) continue;
                float jit = 0.f;
                if (bow)   // slip-stick: each harmonic's phase wanders a little (more on the upper ones, more with pressure)
                {
                    float& j = tr.bowJit[(size_t) h];
                    j = 0.9f * j + rand11() * (0.02f + 0.08f * pressure) * std::min(1.f, (float) h / 8.f);
                    jit = j;
                    amp *= 1.f + 0.05f * (0.5f + pressure) * rand11() * std::min(1.f, (float) h / 10.f);
                }
                addSine(vSpec, kOut, amp, wrapPi(tr.ph * (float) h + jit));
            }
            // BREATH: breathy air hugging this note's harmonics (more on the upper ones), plus a soft air band;
            // collected per bin here and given a slowly wandering phase after the loop, so it is smooth, not grainy
            const float breathAmt = std::min(1.f, params.voiceBreath + (bow ? 0.04f + 0.08f * pressure : 0.f));   // BOW: a little rosin noise (owner: was too noisy)
            if (breathAmt > 0.001f)
            {
                anyBreath = true;
                const float b = breathAmt * std::sqrt(breathAmt);   // gentle at low settings
                const float onset = 1.f + 1.2f * (1.f - std::min(ph, 1.f));
                const float scale = 0.5f * std::abs(kernel(0.f));
                for (int h = 1; h <= 16; ++h)
                {
                    if (hw[(size_t) h] <= 0.f) continue;
                    const float kOut = kBase * (float) h;
                    if (kOut + 3.f >= (float) (kBins - 1)) break;
                    const float air = std::min(1.f, kOut * binHz / 1500.f);
                    const float a = A * hw[(size_t) h] * norm * b * 0.5f * air * onset * filterMagFast(kOut, tanFc, invQ) * scale;
                    const int k0 = (int) std::floor(kOut);
                    for (int k = k0 - 1; k <= k0 + 2; ++k)
                        if (k >= 1 && k < kBins) breathAcc[(size_t) k] += a * std::max(0.f, 1.f - std::abs((float) k - kOut) / 2.2f);
                }
                const int k0 = std::max(2, (int) (2000.f / binHz)), k1 = std::min(kBins - 2, (int) (7000.f / binHz));
                for (int k = k0; k <= k1; ++k)
                {
                    const float f = (float) k * binHz;
                    const float shape = (vowelOn ? 0.4f + 0.6f * std::min(1.f, vowelGain(vpos, f)) : 0.7f) * filterMagFast((float) k, tanFc, invQ);
                    breathAcc[(size_t) k] += A * b * 0.01f * onset * shape * scale;
                }
            }
            if (pubN < kVoicePub)
            {
                voicePubHz[(size_t) pubN].store(kBase * binHz, std::memory_order_relaxed);
                voicePubLevel[(size_t) pubN].store(A / tuning::voiceLevel, std::memory_order_relaxed);
                voicePubTime[(size_t) pubN].store(t, std::memory_order_relaxed);
                voicePubScoop[(size_t) pubN].store(scoopLeft, std::memory_order_relaxed);
                voicePubVowel[(size_t) pubN].store(vowelOn ? vpos : -1.f, std::memory_order_relaxed);
                ++pubN;
            }
        }
        if (anyBreath)
            for (int k = 1; k < kBins; ++k)
            {
                if (breathAcc[(size_t) k] <= 0.f) continue;
                rng = rng * 1664525u + 1013904223u;
                const float jitter = ((float) (rng >> 8) / 16777216.f - 0.5f) * 2.6f;   // +-75 degrees a frame
                breathPh[(size_t) k] = wrapPi(breathPh[(size_t) k] + expected * (float) k + jitter);
                vSpec[(size_t) k] += std::polar(breathAcc[(size_t) k], breathPh[(size_t) k]);
                breathAcc[(size_t) k] = 0.f;
            }
        if (params.midiOut > 0) updateMidi(next, nn);   // (before the tracks are stored: it marks their MIDI notes)
        else if (heldN > 0) allMidiOff();
        numTracks = nn;
        for (int n = 0; n < nn; ++n) tracks[(size_t) n] = next[n];
        voicePubCount.store(pubN, std::memory_order_relaxed);
        freshAnalysis = false;
    }

    // harmonic h's weight for the chosen wave (pw = current pulse width for PWM)
    float waveWeight(int h, float pw) const
    {
        const float n = (float) h;
        switch (params.voiceWave)
        {
            case 0: case 1: return 1.f / n;                                                   // saw (supersaw adds unison later)
            case 2: return std::abs(std::sin(kPi * n * pw)) / n;                              // pulse, width moving
            case 3: return (h % 2 == 1) ? 1.f / n : 0.f;                                      // square
            case 4: { static const float o[] = { 0, 1.f, .8f, .6f, .5f, 0, .3f, 0, .25f }; return h <= 8 ? o[h] : 0.f; } // organ drawbars
            case 5: { static const float gl[] = { 0, 1.f, .22f, 0, .12f, 0, 0, 0, .05f }; return h <= 8 ? gl[h] : 0.f; } // glass
            case 6: return (h % 2 == 1) ? (h == 1 ? .6f : 1.6f / n) : .15f / n;                // reed: strong odd partials
            case 7: return h <= 20 ? 1.f / std::pow(n, 0.92f) : 0.f;                        // VG bowed: saw-like, shaped by bowBody()
            case 8: return std::abs(std::sin(kPi * n / 7.3f)) / n + 0.02f / n;                 // BOW: a bowed string (bow position 1/7.3)
            default: return 1.f / n;
        }
    }

    // Low-pass response at normalised frequency r = f/fc. Resonance (invQ) lives in the first
    // 2-pole stage only; extra stages are flat Butterworth-style, so 24/48 dB/oct stay controlled.
    Cpx filterH(float r, float invQ) const
    {
        const Cpx one(1.f, 0.f);
        if (params.slope <= 0) return one / Cpx(1.f, r);
        const Cpx res  = one / Cpx(1.f - r * r, r * invQ);
        if (params.slope == 1) return res;
        const Cpx flat = one / Cpx(1.f - r * r, r * 1.41421356f);
        if (params.slope == 2) return res * flat;
        return res * flat * flat * flat;
    }

    // Group onsets into note events. An event starts with an onset frame carrying enough energy; onsets
    // that follow within chordGroupMs of the previous one belong to it (a strum). While the event is
    // open its level is the loudest frame of its own bins, so the pick strength (TOUCH) settles over the
    // first ~30 ms and the attack of all its bins is updated as it does.
    void noteEvents(float onsetEnergy, float T)
    {
        const bool  qualifies = onsetEnergy >= tuning::eventMinLevel * tuning::eventMinLevel
                             && onsetEnergy >= tuning::eventMinShare * frameEnergy;
        const float sinceQual = (float) (frameCount - lastQualFrame) * frameSec;
        const bool  continues = groupOpen && sinceQual < params.chordGroupMs * 0.001f;

        if (groupOpen && ! continues) closeGroup();
        if (qualifies && ! continues)
        {
            ++eventId;
            fastAnalyses = tuning::fastAnalysesAfterPick;   // see the new note quickly (voice + MIDI)
            gapSec = haveEvent ? (float) (frameCount - eventStartFrame) * frameSec : 1e9f;
            haveEvent = true; groupOpen = true; groupPeakE = 0.f; eventStartFrame = frameCount;
            if (params.spreadMode == 1) { rng = rng * 1664525u + 1013904223u; eventPan = (float) (rng >> 8) / 8388608.f - 1.f; }
            else                        { altSign = -altSign; eventPan = altSign; }
            if (params.spreadMode == 2) eventPan = 0.f;
            notePanPub.store(eventPan * params.spread, std::memory_order_relaxed);
            noteFramePub.store(frameCount, std::memory_order_relaxed);
            evtPending = (int64_t) sampleCount + kLatencySamples - kHop;   // when this note reaches the output
            noteSeqPub.fetch_add(1, std::memory_order_relaxed);
        }
        if (qualifies) lastQualFrame = frameCount;
        if (! groupOpen) return;

        float e = 0.f;
        for (int k = 0; k < kBins; ++k)
        {
            const size_t i = (size_t) k;
            if (onsetBin[i]) binGroup[i] = eventId;
            if (binGroup[i] == eventId) e += mag[i] * mag[i];
        }
        groupPeakE = std::max(groupPeakE, e);
        groupLevelDb = 10.f * std::log10(std::max(groupPeakE, 1e-12f));
        const float range    = std::max(params.touchRangeDb, 1.f);
        const float strength = haveRef ? std::clamp((groupLevelDb - refEventDb) / range, -1.f, 1.f) : 0.f;
        // filter DYN has its own range (independent of the FEEL DYNAMICS settings)
        const float pickStrength = haveRef ? std::clamp((groupLevelDb - refEventDb) / std::max(params.pickRangeDb, 1.f), -1.f, 1.f) : 0.f;

        float Tn = T * std::exp2(-params.touch * strength * params.touchDepthOct);
        if (params.follow > 0.f && gapSec < tuning::followMaxGapSec && gapSec >= tuning::followMinGapSec)
        {
            const float frac = tuning::followFracLoose + (params.followTight - tuning::followFracLoose) * params.follow;
            Tn = std::min(Tn, frac * gapSec);
        }
        Tn = std::clamp(Tn, 0.010f, 6.f);
        groupMul = Tn / T;

        // SPREAD by pitch (the note's real pitch, low E = left .. high E two octaves up = right), by pick
        // strength (soft = left, hard = right), or loud / soft notes in the middle (the others alternate sides)
        if (params.spreadMode >= 2)
        {
            float p = strength;
            const float s01 = 0.5f * (strength + 1.f);
            if (params.spreadMode == 2) p = eventPan;                     // set by updatePitchPan
            else if (params.spreadMode == 4) p = (1.f - s01) * altSign;   // loud = middle, soft = sides
            else if (params.spreadMode == 5) p = s01 * altSign;           // soft = middle, loud = sides
            eventPan = p;
            notePanPub.store(eventPan * params.spread, std::memory_order_relaxed);
        }
        for (int k = 0; k < kBins; ++k)
            if (binGroup[(size_t) k] == eventId)
            {
                attackMul[(size_t) k] = groupMul;
                binStrength[(size_t) k] = pickStrength;
                binEven[(size_t) k] = std::exp2(-params.even * strength * std::max(params.touchRangeDb, 1.f) / 6.0206f);   // EVEN 1 = picks levelled to the average
                if (params.spreadMode >= 2) pan[(size_t) k] = eventPan;
            }

        noteAttackPub.store(Tn, std::memory_order_relaxed);
        noteStrengthPub.store(strength, std::memory_order_relaxed);
        pickStrengthPub.store(pickStrength, std::memory_order_relaxed);
    }

    void closeGroup()
    {
        groupOpen = false;
        if (! haveRef) { refEventDb = groupLevelDb; haveRef = true; }
        else           refEventDb += (groupLevelDb - refEventDb) * tuning::touchRefCoef;
    }

    template <typename MagFn>
    void publishBands(std::array<std::atomic<float>, kSpecBands>& dst, MagFn magAt)
    {
        for (int b = 0; b < kSpecBands; ++b)
        {
            const int lo = bandEdge[(size_t) b], hi = std::max(bandEdge[(size_t) b + 1], lo + 1);
            float pk = 0.f;
            for (int k = lo; k < hi && k < kBins; ++k) pk = std::max(pk, magAt(k));
            dst[(size_t) b].store(pk, std::memory_order_relaxed);
        }
    }

    // ---------------------------------------------------------------- state
    FftT<Order> fft;
    Params params;
    double sampleRate = 48000.0;

    std::array<float, kFftSize> win {}, inRing {}, outRing {};
    std::array<Cpx, kFftSize> spec {};
    std::array<Cpx, kBins> inSpec {};
    std::array<float, kBins> ref {}, refRaw {}, hold {}, phase {}, gain {}, tanBin {}, mag {}, magSm {};
    std::array<bool, kBins> active {};
    std::array<int, kBins> quiet {};
    float magNorm = 1.f, olaScale = 1.f;
    float refDecay = 1.f, refRawDecay = 1.f, envCoef = 1.f, frameSec = 0.f, fbHoldDecay = 1.f;
    int   quietFramesN = 3;
    std::array<float, kBins> env {}, attackMul {}, binStrength {};
    float frameEnergy = 0.f;
    std::array<bool, kBins> onsetBin {};

    // feedback
    std::array<float, kBins> fbAmt {}, fbHold {}, prevArg {}, instBin {}, subPhPrev {}, subPhCur {};

    // voice / vibrato
    static constexpr int kMaxHarm = 40, kMaxNotes = 8, kGvSize = 1024, kTanSize = 4096;
    static constexpr float kTanMax = 0.46f;
    std::array<float, kMaxHarm + 1> waveW {};   // this frame's wave weights (fixed waves), normalised
    PitchTracker tracker;
    int trackerEvery = 4;
    std::array<PitchTracker::Note, kMaxNotes> notes {};
    struct Track
    {
        float hz = 0.f, ph = 0.f, amp = 0.f; int age = 0, lost = 0;
        int seen = 0, confirmedAt = -1, midiNote = -1, midiCh = 0, lastBend = 8192; uint32_t id = 0;
        float glideOct = 0.f, glideLeft = 0.f, monoGain = 1.f;      // GLIDE: start offset (octaves) and what is left; MONO fade
        std::array<float, kMaxHarm + 1> bowJit {};                            // BOW: each harmonic's wandering phase (slip-stick)
        // LIFE: this note's own slow wander (current value and target): pitch in cents, tone in octaves, level in dB
        float lp = 0.f, lpT = 0.f, lt = 0.f, ltT = 0.f, la = 0.f, laT = 0.f, pwOff = 0.f; int lifeCount = 0;
    };
    // ---- MIDI out: note on when a tracked note is confirmed, off when it is gone; MPE: one channel per note
    //      (2..16) with its own pitch bend (+-48 semitones, the MPE default) following bends and vibrato
public:
    struct MidiEv { uint8_t type, ch, note, vel; int bend; uint64_t sample; };   // type 0 on, 1 off, 2 bend
    int popMidi(MidiEv* out, int maxN)
    {
        const int n = std::min(midiN, maxN);
        for (int i = 0; i < n; ++i) out[i] = midiQ[(size_t) i];
        midiN = 0;
        return n;
    }
    uint64_t samplesProcessed() const { return sampleCount; }
private:
    void pushMidi(uint8_t type, uint8_t ch, uint8_t note, uint8_t vel, int bend)
    {
        if (midiN < (int) midiQ.size()) midiQ[(size_t) midiN++] = { type, ch, note, vel, bend, sampleCount };
    }
    void allMidiOff()
    {
        for (int i = 0; i < heldN; ++i) pushMidi(1, (uint8_t) midiHeld[(size_t) i].ch, (uint8_t) midiHeld[(size_t) i].note, 0, 0);
        heldN = 0;
        for (auto& t : tracks) t.midiNote = -1;
    }
    void updateMidi(Track* nx, int nn)
    {
        const bool mpe = params.midiOut == 2;
        // notes whose track is gone (or no longer sounding): off
        for (int h = 0; h < heldN;)
        {
            const Held hd = midiHeld[(size_t) h];
            bool alive = false;
            for (int n = 0; n < nn; ++n) if (nx[n].id == hd.id && nx[n].lost <= trackerEvery) alive = true;
            if (! alive)
            {
                pushMidi(1, (uint8_t) hd.ch, (uint8_t) hd.note, 0, 0);
                for (int n = 0; n < nn; ++n) if (nx[n].id == hd.id) nx[n].midiNote = -2;   // done
                midiHeld[(size_t) h] = midiHeld[(size_t) --heldN];
            }
            else ++h;
        }
        for (int n = 0; n < nn; ++n)
        {
            Track& tr = nx[n];
            const float midi = 69.f + 12.f * std::log2(std::max(tr.hz, 20.f) / 440.f);
            if (tr.midiNote == -1 && tr.confirmedAt >= 0 && tr.lost == 0 && heldN < 15)
            {
                tr.midiNote = std::clamp((int) std::lround(midi), 0, 127);
                const size_t sb = (size_t) noteBin(tr.hz);
                const int vel = std::clamp((int) std::lround(80.f + 42.f * binStrength[sb]), 1, 127);
                tr.midiCh = mpe ? 2 + (int) (nextMpeCh++ % 15u) : 1;
                if (mpe) { tr.lastBend = bendFor(midi - (float) tr.midiNote); pushMidi(2, (uint8_t) tr.midiCh, 0, 0, tr.lastBend); }
                pushMidi(0, (uint8_t) tr.midiCh, (uint8_t) tr.midiNote, (uint8_t) vel, 0);
                midiHeld[(size_t) heldN++] = { tr.id, tr.midiNote, tr.midiCh };
            }
            else if (mpe && tr.midiNote >= 0)
            {
                const int b = bendFor(midi - (float) tr.midiNote);
                if (std::abs(b - tr.lastBend) >= 6) { tr.lastBend = b; pushMidi(2, (uint8_t) tr.midiCh, 0, 0, b); }
            }
        }
    }
    static int bendFor(float semis) { return std::clamp((int) std::lround(8192.f + semis / 48.f * 8192.f), 0, 16383); }
    struct Held { uint32_t id; int note, ch; };
    std::array<Held, 16> midiHeld {};
    int heldN = 0;
    std::array<MidiEv, 128> midiQ {};
    int midiN = 0;
    uint32_t nextMpeCh = 0, trackIdCounter = 0;
    bool trackerDue = false, freshAnalysis = false;
    float lastLeadHz = 0.f; uint32_t lastLeadFrame = 0;
    int fastAnalyses = 0;

    float rand11() { rng = rng * 1664525u + 1013904223u; return (float) (rng >> 8) / 8388608.f - 1.f; }
    static constexpr int kMaxTracks = 12;
    std::array<Track, kMaxTracks> tracks {};
    int numNotes = 0, numTracks = 0;
    std::array<float, kGvSize> gvL {}, gvR {};
    int gvW = 0;
    uint64_t sampleCount = 0;
    int64_t evtPending = -1, evtActive = -1;
    float gvAmt = 0.f, gvPhase = 0.f;
    std::array<float, kFftSize> vRing {}, vdRing {};
    std::array<Cpx, kFftSize> vSpec {}, vdSpec {};
    std::array<float, kBins> fcBin {};
    std::array<std::atomic<float>, kSpecBands> specVoice {};
    int voiceFrames = 0;

    // tremolo / spread
    std::array<float, kBins> noteTime {}, pan {}, panL {}, panR {};
    std::array<float, kFftSize> dRing {};
    std::array<Cpx, kFftSize> dSpec {};
    float eventPan = 0.f, altSign = 1.f;
    uint32_t rng = 22222u;
    int stereoFrames = 0;
    static constexpr int kKernSpan = 4, kKernRes = 64, kKernSize = 2 * kKernSpan * kKernRes + 1;
    std::array<Cpx, kKernSize> kern {};

    // note events
    std::array<uint32_t, kBins> binGroup {};
    uint32_t frameCount = 0, lastQualFrame = 0, eventStartFrame = 0, eventId = 0;
    bool  haveEvent = false, haveRef = false, groupOpen = false;
    float refEventDb = -30.f, groupMul = 1.f, groupPeakE = 0.f, groupLevelDb = -120.f, gapSec = 1e9f;
    std::atomic<uint32_t> noteSeqPub { 0 }, noteFramePub { 0 }, frameCountPub { 0 };
    std::array<std::atomic<float>, kVoicePub> voicePubHz {}, voicePubLevel {}, voicePubTime {}, voicePubScoop {}, voicePubVowel {};
    std::array<float, kBins> breathAcc {}, breathPh {};
    std::array<float, kBins> fbDuck {};
    float fbRatioA = 0.f, fbRatioB = 0.f, fbWeightA = 1.f, fbWeightB = 0.f, fbTakeover = 0.f, fbDriftNow = 0.f, fbDriftTarget = 0.f;
    int fbDriftCount = 0; uint32_t fbRng = 0x2468aceu;
    std::array<float, kBins> matchPeak {}, matchGain {};   // level-matched swell: the pick's level, the make-up found
    std::array<float, kBins> magPrev {}, binEven {}, repickFloor {}, bodyGain {};   // last frame's magnitude (smooth re-pick), EVEN gain per note   // BREATH: amplitude collected per bin, its slowly wandering phase
    std::atomic<int> voicePubCount { 0 };
    std::atomic<float> noteAttackPub { 0.3f }, noteStrengthPub { 0.f }, frameSecPub { 0.f }, notePanPub { 0.f }, pickStrengthPub { 0.f };
    int pos = 0, hopCount = 0;

    float mix = 0.f, mixCoef = 0.f, qCoef = 0.f, ctlCoef = 0.f;
    float qSm = 1.f, freqSm = 2000.f, modSm = 0.f;

    std::array<int, kSpecBands + 1> bandEdge {};
    std::array<std::atomic<float>, kSpecBands> specIn {}, specOut {};
    std::atomic<uint32_t> specSeq { 0 };
};

using SwelloEngine   = SwelloEngineT<10>;   // standard: 1024-point FFT, ~21 ms at 48 kHz
using SwelloEngineLL = SwelloEngineT<9>;    // low latency: 512-point FFT, ~10.7 ms

// the standard engine's sizes, for code that only uses the default
constexpr int kFftSize        = SwelloEngine::kFftSize;
constexpr int kHop            = SwelloEngine::kHop;
constexpr int kBins           = SwelloEngine::kBins;
constexpr int kLatencySamples = SwelloEngine::kLatencySamples;

} // namespace swello
