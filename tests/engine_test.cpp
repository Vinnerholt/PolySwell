// Build: g++ -O2 -std=c++17 -I Source tests/engine_test.cpp -o engine_test && ./engine_test
#include "dsp/SwelloEngine.h"
#include "dsp/Detuner.h"
#include "dsp/Limiter.h"
#include "dsp/VoiceFx.h"
#include "dsp/Smear.h"
#include "dsp/Drive.h"
#include "dsp/Hold.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace swello;
static constexpr double kSr = 48000.0;
static constexpr double kPi = 3.14159265358979323846;
static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

struct Out { std::vector<float> wet, dry; };

static Out run(const std::vector<float>& in, const Params& p)
{
    SwelloEngine e; e.prepare(kSr); e.setParams(p);
    Out o; o.wet.resize(in.size()); o.dry.resize(in.size());
    for (size_t i = 0; i < in.size(); ++i) o.wet[i] = e.process(in[i], o.dry[i]);
    return o;
}

// amplitude of a sinusoid at f inside [start, start+len) (Hann-windowed correlation)
static double amp(const std::vector<float>& x, int start, int len, double f)
{
    double re = 0, im = 0, ws = 0;
    for (int n = 0; n < len; ++n)
    {
        const double w = 0.5 * (1 - std::cos(2 * kPi * n / len));
        const double ph = 2 * kPi * f * (start + n) / kSr;
        re += w * x[(size_t) (start + n)] * std::cos(ph);
        im -= w * x[(size_t) (start + n)] * std::sin(ph);
        ws += w;
    }
    return 2.0 * std::sqrt(re * re + im * im) / ws;
}

static int at(double sec) { return (int) (sec * kSr); }


// deterministic noise in [-1, 1]
static float noise()
{
    static uint32_t st = 12345u;
    st = st * 1664525u + 1013904223u;
    return (float) ((st >> 8) & 0xFFFF) / 32768.0f - 1.0f;
}

static double energy(const std::vector<float>& x, int start, int len)
{
    double e = 0;
    for (int i = 0; i < len; ++i) e += (double) x[(size_t) (start + i)] * x[(size_t) (start + i)];
    return e;
}

// energy ratio wet/dry over an event window (dry = latency-aligned input)
static double eventRatio(const Out& o, int start, int len)
{
    return energy(o.wet, start, len) / std::max(energy(o.dry, start, len), 1e-20);
}

static void addPluck(std::vector<float>& x, int t0, double amp, double tau, double f0 = 220.0)
{
    for (int i = t0; i < (int) x.size() && i < t0 + at(8 * tau); ++i)
    {
        const double t = (i - t0) / kSr;
        double s = 0;
        for (int h = 1; h <= 6; ++h) s += std::sin(2 * kPi * f0 * h * t) / h;
        x[(size_t) i] += (float) (amp * s * std::exp(-t / tau));
    }
}

int main()
{
    // 1) steady sine passes at unity once the first swell has finished
    std::printf("Test 1: reconstruction / unity gain\n");
    {
        std::vector<float> x((size_t) at(2.0));
        for (size_t i = 0; i < x.size(); ++i) x[i] = 0.3f * (float) std::sin(2 * kPi * 220.0 * i / kSr);
        Params p; p.attackMs = 10;
        auto o = run(x, p);
        const double r = amp(o.wet, at(1.5), 4800, 220.0) / amp(o.dry, at(1.5), 4800, 220.0);
        std::printf("  steady-state wet/dry = %.4f\n", r);
        check(std::abs(r - 1.0) < 0.01, "within 1% of unity");
    }

    // 2) single plucked note: swell from silence to full level over ~ATTACK
    std::printf("Test 2: plucked chord swells (attack = 500 ms)\n");
    {
        std::vector<float> x((size_t) at(3.0), 0.f);
        const int t0 = at(1.0);
        for (int i = t0; i < (int) x.size(); ++i)
        {
            const double t = (i - t0) / kSr;
            double s = 0;
            for (int h = 1; h <= 6; ++h) s += std::sin(2 * kPi * 220.0 * h * t) / h;
            x[(size_t) i] = (float) (0.25 * s * std::exp(-t / 2.0));
        }
        Params p; p.attackMs = 500;
        auto o = run(x, p);
        const double offs[] = { 0.03, 0.12, 0.25, 0.40, 0.60, 0.90 };
        double ratio[6];
        for (int k = 0; k < 6; ++k)
        {
            const int s = t0 + at(offs[k]);
            ratio[k] = amp(o.wet, s, 1200, 220.0) / amp(o.dry, s, 1200, 220.0);
            std::printf("  t0+%3.0f ms : wet/dry = %.3f\n", offs[k] * 1000, ratio[k]);
        }
        check(ratio[0] < 0.15, "starts near silent");
        check(ratio[2] > ratio[1] && ratio[3] > ratio[2], "rises monotonically");
        check(ratio[5] > 0.9, "fully open after the attack time");
    }

    // 3) polyphony: sustained note keeps its level while a new note swells
    std::printf("Test 3: polyphony (note A sustained, note B enters, attack = 800 ms)\n");
    {
        const double fa = 196.0, fb = 330.0;
        std::vector<float> x((size_t) at(4.0));
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = i / kSr;
            x[i] = (float) (0.2 * std::sin(2 * kPi * fa * t) + (t >= 2.0 ? 0.2 * std::sin(2 * kPi * fb * t) : 0.0));
        }
        Params p; p.attackMs = 800;
        auto o = run(x, p);
        const int sB = at(2.0);
        double aMin = 9, b1, b2;
        for (double off : { 0.05, 0.2, 0.4, 0.6, 0.8, 1.2 })
        {
            const int s = sB + at(off);
            aMin = std::min(aMin, amp(o.wet, s, 1200, fa) / amp(o.dry, s, 1200, fa));
        }
        b1 = amp(o.wet, sB + at(0.05), 1200, fb) / amp(o.dry, sB + at(0.05), 1200, fb);
        b2 = amp(o.wet, sB + at(1.5),  1200, fb) / amp(o.dry, sB + at(1.5),  1200, fb);
        std::printf("  note A min wet/dry during B's swell = %.3f\n", aMin);
        std::printf("  note B wet/dry: start %.3f, end %.3f\n", b1, b2);
        check(aMin > 0.85, "note A is not swelled (polyphonic)");
        check(b1 < 0.2 && b2 > 0.9, "note B swells from 0 to full");
    }

    // 4) sustain knob slows the decay of a plucked note
    std::printf("Test 4: sustain\n");
    {
        std::vector<float> x((size_t) at(4.0), 0.f);
        const int t0 = at(0.5);
        for (int i = t0; i < (int) x.size(); ++i)
        {
            const double t = (i - t0) / kSr;
            x[(size_t) i] = (float) (0.3 * std::sin(2 * kPi * 220.0 * t) * std::pow(10.0, -20.0 * t / 20.0)); // -20 dB/s
        }
        Params p; p.attackMs = 10;
        auto off = run(x, p);
        p.sustain = 1.0f;
        auto on = run(x, p);
        const int s = t0 + at(1.5);
        const double dOff = amp(off.wet, s, 2400, 220.0), dOn = amp(on.wet, s, 2400, 220.0);
        std::printf("  level at +1.5 s: sustain 0 = %.4f, sustain 1 = %.4f (x%.1f)\n", dOff, dOn, dOn / dOff);
        check(dOn > 2.0 * dOff, "sustain raises the tail by > 6 dB");
    }

    // 5) static low-pass
    std::printf("Test 5: static low-pass (1 kHz, low resonance)\n");
    {
        std::vector<float> x((size_t) at(2.0));
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = 0.2f * (float) (std::sin(2 * kPi * 200.0 * i / kSr) + std::sin(2 * kPi * 8000.0 * i / kSr));
        Params p; p.attackMs = 10; p.filterMode = 1; p.freqHz = 1000; p.resLow = 0.0f;
        auto o = run(x, p);
        const double lo = amp(o.wet, at(1.5), 4800, 200.0), hi = amp(o.wet, at(1.5), 4800, 8000.0);
        std::printf("  200 Hz = %.3f, 8 kHz = %.5f (%.1f dB)\n", lo, hi, 20 * std::log10(hi / lo));
        check(hi / lo < 0.03, "8 kHz attenuated by > 30 dB");
    }


    // 6) SHORT SOUNDS must be processed too (attack = 300 ms)
    std::printf("Test 6: short sounds are swallowed by the swell\n");
    {
        Params p; p.attackMs = 300;
        auto mk = [] { return std::vector<float>((size_t) at(3.0), 0.f); };

        { auto x = mk(); for (int i = 0; i < 48; ++i) x[(size_t) (at(1.0) + i)] = 0.3f * noise();
          auto o = run(x, p); const double r = eventRatio(o, at(1.0), at(0.15));
          std::printf("  1 ms click                 : wet/dry energy = %.5f\n", r);
          check(r < 0.02, "click is attenuated by > 17 dB"); }

        { auto x = mk(); for (int i = 0; i < at(0.03); ++i) x[(size_t) (at(1.0) + i)] = 0.005f * noise();
          auto o = run(x, p); const double r = eventRatio(o, at(1.0), at(0.2));
          std::printf("  30 ms string noise (-50 dB): wet/dry energy = %.5f\n", r);
          check(r < 0.05, "quiet string noise is attenuated"); }

        { auto x = mk(); addPluck(x, at(1.0), 0.25, 0.010);
          auto o = run(x, p); const double r = eventRatio(o, at(1.0), at(0.2));
          std::printf("  staccato pluck (10 ms tau) : wet/dry energy = %.5f\n", r);
          check(r < 0.05, "quick pluck is attenuated"); }

        { auto x = mk(); for (int n = 0; n < 8; ++n) addPluck(x, at(0.5 + 0.1 * n), 0.25, 0.025);
          auto o = run(x, p);
          double worst = 0;
          for (int n = 0; n < 8; ++n) worst = std::max(worst, eventRatio(o, at(0.5 + 0.1 * n), at(0.1)));
          std::printf("  8 rapid plucks (100 ms)    : worst wet/dry energy = %.5f\n", worst);
          check(worst < 0.05, "every pluck of a rapid sequence is attenuated"); }

        { std::vector<float> x((size_t) at(4.0));
          for (size_t i = 0; i < x.size(); ++i)
              x[i] = (float) ((i < (size_t) at(2.0) ? 0.05 : 0.25) * std::sin(2 * kPi * 220.0 * i / kSr));
          auto o = run(x, p);
          const double a = amp(o.wet, at(2.0) + at(0.02), 1200, 220.0) / amp(o.dry, at(2.0) + at(0.02), 1200, 220.0);
          const double b = amp(o.wet, at(2.0) + at(1.0),  1200, 220.0) / amp(o.dry, at(2.0) + at(1.0),  1200, 220.0);
          std::printf("  re-pick of ringing string  : wet/dry start %.3f, end %.3f\n", a, b);
          check(a < 0.3 && b > 0.9, "re-pick over a ringing note swells again"); }

        { std::vector<float> x((size_t) at(3.0));
          for (auto& v : x) v = 0.05f * noise();
          auto o = run(x, p);
          const double r = std::sqrt(energy(o.wet, at(1.5), at(1.0)) / energy(o.dry, at(1.5), at(1.0)));
          std::printf("  steady noise (no flutter)  : wet/dry rms = %.3f\n", r);
          check(r > 0.85, "sustained noisy sound is not chopped up"); }
    }

    // 7) POLYPHONIC FILTER: a new note's sweep must not filter notes that are already ringing
    std::printf("Test 7: filter is per note (attack 600 ms, MOD fully up, FREQ 3 kHz)\n");
    {
        std::vector<float> x((size_t) at(4.0));
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = i / kSr;
            double s = 0.1 * std::sin(2 * kPi * 220.0 * t) + 0.1 * std::sin(2 * kPi * 1500.0 * t);
            if (t >= 2.0) s += 0.1 * std::sin(2 * kPi * 330.0 * t) + 0.1 * std::sin(2 * kPi * 2200.0 * t);
            x[i] = (float) s;
        }
        Params p; p.attackMs = 600; p.filterMode = 1; p.freqHz = 3000; p.mod = 1.0f; p.resLow = 0.0f;
        auto o = run(x, p);
        auto ratio = [&] (double sec, double f) { const int s = at(sec); return amp(o.wet, s, 1200, f) / amp(o.dry, s, 1200, f); };
        const double aBefore = ratio(1.9, 1500.0);
        double aWorst = 0;
        for (double off : { 0.05, 0.2, 0.4, 0.7, 1.0 }) aWorst = std::max(aWorst, std::abs(ratio(2.0 + off, 1500.0) / aBefore - 1.0));
        const double bEarly = ratio(2.03, 2200.0), bLate = ratio(3.7, 2200.0);
        std::printf("  old note, 1.5 kHz partial: max change while a new note swells = %.1f %%\n", 100 * aWorst);
        std::printf("  new note, 2.2 kHz partial: early %.4f, late %.3f\n", bEarly, bLate);
        check(aWorst < 0.10, "ringing note is not touched by the new note's filter sweep");
        check(bEarly < 0.2 * bLate && bLate > 0.4, "new note's filter sweeps open over the attack time");
    }

    // 8) filter slope switch: 6 / 12 / 24 / 48 dB per octave
    std::printf("Test 8: filter slope (fc 1 kHz, probes at 200 Hz and 2 kHz)\n");
    {
        std::vector<float> x((size_t) at(2.0));
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = 0.2f * (float) (std::sin(2 * kPi * 200.0 * i / kSr) + std::sin(2 * kPi * 2000.0 * i / kSr));
        double att[4];
        for (int sl = 0; sl < 4; ++sl)
        {
            Params p; p.attackMs = 10; p.filterMode = 1; p.freqHz = 1000; p.resLow = 0.0f; p.slope = sl;
            auto o = run(x, p);
            att[sl] = 20 * std::log10(amp(o.wet, at(1.5), 4800, 2000.0) / amp(o.wet, at(1.5), 4800, 200.0));
            std::printf("  slope %d: 2 kHz relative to 200 Hz = %.1f dB\n", sl, att[sl]);
        }
        check(att[0] < -4 && att[1] < att[0] - 3 && att[2] < att[1] - 8 && att[3] < att[2] - 15,
              "each slope setting attenuates clearly more than the previous");
    }

    // 9) detune: shifted copy at the right pitch, clean when ratio = 1
    std::printf("Test 9: detune\n");
    {
        const double f0 = 440.0, cents = 20.0, f1 = f0 * std::pow(2.0, cents / 1200.0);
        std::vector<float> x((size_t) at(3.0));
        for (size_t i = 0; i < x.size(); ++i) x[i] = 0.3f * (float) std::sin(2 * kPi * f0 * i / kSr);
        Detuner d; d.prepare(kSr);
        std::vector<float> up(x.size());
        for (size_t i = 0; i < x.size(); ++i) up[i] = d.process(x[i], (float) std::pow(2.0, cents / 1200.0));
        const double aSrc = amp(up, at(2.0), 24000, f0), aShift = amp(up, at(2.0), 24000, f1);
        std::printf("  shifted copy: at %.1f Hz = %.3f, at %.1f Hz = %.3f\n", f0, aSrc, f1, aShift);
        check(aShift > 0.25 && aSrc < 0.08, "copy is at the shifted pitch with near-unity level");
        Detuner z; z.prepare(kSr);
        std::vector<float> same(x.size());
        for (size_t i = 0; i < x.size(); ++i) same[i] = z.process(x[i], 1.0f);
        check(std::abs(amp(same, at(2.0), 24000, f0) - 0.3) < 0.01, "ratio 1 is a clean delay");
    }

    // 10) limiter
    std::printf("Test 10: limiter\n");
    {
        Limiter lim; lim.prepare(kSr);
        float pk = 0; double quiet = 0;
        for (int i = 0; i < at(1.0); ++i)
        {
            float v = 4.0f * (float) std::sin(2 * kPi * 220.0 * i / kSr), w = v;
            lim.process(v, w); pk = std::max(pk, std::abs(v));
        }
        for (int i = 0; i < at(1.0); ++i)
        {
            float v = 0.3f * (float) std::sin(2 * kPi * 220.0 * i / kSr), w = v;
            lim.process(v, w);
            if (i > at(0.5)) quiet = std::max(quiet, (double) std::abs(v));
        }
        std::printf("  peak with 4.0 input = %.3f, level for 0.3 input afterwards = %.3f\n", pk, quiet);
        check(pk <= Limiter::ceiling + 1e-4f, "never exceeds the ceiling");
        check(quiet > 0.29, "recovers to unity gain for quiet material");
    }

    // 11) live spectrum for the UI
    std::printf("Test 11: live spectrum bands (1 kHz note, attack 500 ms)\n");
    {
        Params p; p.attackMs = 500.f;
        SwelloEngine e; e.prepare(kSr); e.setParams(p);
        float in[SwelloEngine::kSpecBands], out[SwelloEngine::kSpecBands], dry = 0.f;
        const double f0 = 1000.0;
        const int band = (int) std::floor(std::log(f0 / SwelloEngine::kSpecLoHz) / std::log((double) SwelloEngine::kSpecHiHz / SwelloEngine::kSpecLoHz) * SwelloEngine::kSpecBands);
        float earlyIn = 0, earlyOut = 0, lateIn = 0, lateOut = 0, farOut = 0;
        const uint32_t seq0 = e.readSpectrum(in, out);
        uint32_t seq1 = seq0;
        for (int i = 0; i < at(1.0); ++i)
        {
            e.process(0.3f * (float) std::sin(2 * kPi * f0 * i / kSr), dry);
            if (i == at(0.08)) { e.readSpectrum(in, out); earlyIn = in[band]; earlyOut = out[band]; }
            if (i == at(0.9))  { seq1 = e.readSpectrum(in, out); lateIn = in[band]; lateOut = out[band]; farOut = out[SwelloEngine::kSpecBands - 4]; }
        }
        std::printf("  band %d: early in %.3f out %.3f, late in %.3f out %.3f, 12 kHz band out %.5f, frames %u\n",
                    band, earlyIn, earlyOut, lateIn, lateOut, farOut, (unsigned) (seq1 - seq0));
        check(earlyIn > 0.2f && earlyOut < 0.1f * earlyIn, "output band starts held back while the input is present");
        check(lateOut > 0.8f * lateIn, "output band has swelled to the input level after the attack");
        check(farOut < 0.01f * lateOut, "bands away from the note stay empty");
        check(seq1 - seq0 > 150, "frame counter advances once per hop");
    }

    // 12) sustain = longer notes, not louder notes
    std::printf("Test 12: sustain lengthens decaying notes without making anything louder\n");
    {
        // (a) steady note with a 6 dB, 5 Hz tremolo: level must not rise
        std::vector<float> x((size_t) at(3.0));
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = (double) i / kSr, trem = 0.75 + 0.25 * std::sin(2 * kPi * 5.0 * t);
            x[i] = (float) (0.2 * trem * std::sin(2 * kPi * 330.0 * t));
        }
        Params p; p.attackMs = 10.f;
        const Out o0 = run(x, p);
        p.sustain = 1.f;
        const Out o1 = run(x, p);
        const double e0 = energy(o0.wet, at(1.0), at(1.5)), e1 = energy(o1.wet, at(1.0), at(1.5));
        std::printf("  steady tremolo note: level change with sustain 1 = %+.2f dB\n", 10.0 * std::log10(e1 / e0));
        check(e1 < e0 * 1.26, "steady note is not made louder (< +1 dB)");

        // (b) a note decaying at 8 dB/s: peak unchanged, tail held up
        std::vector<float> d((size_t) at(4.0));
        for (size_t i = 0; i < d.size(); ++i)
        {
            const double t = (double) i / kSr;
            d[i] = (float) (0.3 * std::pow(10.0, -8.0 * t / 20.0) * std::sin(2 * kPi * 220.0 * t));
        }
        Params q; q.attackMs = 10.f;
        const Out n0 = run(d, q);
        q.sustain = 1.f;
        const Out n1 = run(d, q);
        auto peakOf = [](const std::vector<float>& v) { float m = 0; for (float s : v) m = std::max(m, std::abs(s)); return m; };
        const int lat = kLatencySamples;
        const double a0 = amp(n0.wet, at(0.3) + lat, 4800, 220.0), a1 = amp(n1.wet, at(0.3) + lat, 4800, 220.0);
        const double t0 = amp(n0.wet, at(3.0) + lat, 4800, 220.0), t1 = amp(n1.wet, at(3.0) + lat, 4800, 220.0);
        std::printf("  decaying note: peak %.3f -> %.3f, level at 3 s %.4f -> %.4f (%.1f dB below its own 0.3 s level)\n",
                    peakOf(n0.wet), peakOf(n1.wet), t0, t1, 20.0 * std::log10(a1 / t1));
        check(peakOf(n1.wet) < peakOf(n0.wet) * 1.12f, "peak is not raised (< +1 dB)");
        check(t1 > 3.16 * t0, "tail at 3 s is > 10 dB higher than without sustain");
        check(20.0 * std::log10(a1 / t1) < 8.0, "with sustain the note falls < 8 dB over 2.7 s (natural: 22 dB)");
    }

    // 13) TOUCH: pick strength sets each note's attack
    std::printf("Test 13: touch (attack 400 ms, plucks every 0.6 s: 4 medium, hard, medium, soft)\n");
    {
        const double lv[] = { 0.08, 0.08, 0.08, 0.08, 0.32, 0.08, 0.02 };
        const double fr[] = { 196.0, 247.0, 294.0, 196.0, 247.0, 294.0, 196.0 };
        std::vector<float> x((size_t) at(4.6));
        for (int n = 0; n < 7; ++n) addPluck(x, at(0.1 + 0.6 * n), lv[n], 0.25, fr[n]);
        auto attacks = [&](float touch, float* out)
        {
            Params p; p.attackMs = 400.f; p.touch = touch;
            SwelloEngine e; e.prepare(kSr); e.setParams(p);
            float dry = 0.f;
            for (size_t i = 0; i < x.size(); ++i)
            {
                e.process(x[i], dry);
                for (int n = 0; n < 7; ++n)
                    if ((int) i == at(0.1 + 0.6 * n) + kFftSize + 2400) out[n] = e.latestNote().attackSec;
            }
        };
        float off[7], pos[7], neg[7];
        attacks(0.f, off); attacks(1.f, pos); attacks(-1.f, neg);
        std::printf("  attack s  touch 0: hard %.3f soft %.3f | touch +1: medium %.3f hard %.3f soft %.3f | touch -1: hard %.3f soft %.3f\n",
                    off[4], off[6], pos[3], pos[4], pos[6], neg[4], neg[6]);
        check(std::abs(off[4] - 0.4f) < 0.01f && std::abs(off[6] - 0.4f) < 0.01f, "touch 0: every note uses ATTACK");
        check(std::abs(pos[3] - 0.4f) < 0.06f, "touch +1: an average pick keeps about ATTACK");
        check(pos[4] < 0.2f && pos[6] > 0.8f, "touch +1: hard pick swells faster, soft pick slower");
        check(neg[4] > 0.8f && neg[6] < 0.2f, "touch -1: the other way round");
    }

    // 14) FOLLOW: fast playing still swells; a strum is one event
    std::printf("Test 14: follow (attack 1 s, a note every 150 ms)\n");
    {
        const double fr[] = { 196.0, 247.0, 294.0, 370.0, 440.0, 330.0, 262.0, 220.0 };
        std::vector<float> x((size_t) at(2.0));
        for (int n = 0; n < 8; ++n) addPluck(x, at(0.1 + 0.15 * n), 0.15, 0.15, fr[n]);
        Params p; p.attackMs = 1000.f;
        const Out o0 = run(x, p);
        p.follow = 1.f;
        const Out o1 = run(x, p);
        double r0 = 0, r1 = 0;
        for (int n = 3; n < 8; ++n)
        {
            r0 += eventRatio(o0, at(0.1 + 0.15 * n) + kLatencySamples, at(0.15)) / 5.0;
            r1 += eventRatio(o1, at(0.1 + 0.15 * n) + kLatencySamples, at(0.15)) / 5.0;
        }
        std::printf("  notes 4-8, wet/dry energy inside each note: follow 0 = %.3f, follow 1 = %.3f\n", r0, r1);
        check(r0 < 0.1, "without follow, 1 s swells barely open on 150 ms notes");
        check(r1 > 0.4, "with follow, each fast note swells open within its own length");

        // strum: 5 strings 8 ms apart = one event, so the attack is not cut to a few ms
        std::vector<float> s((size_t) at(1.5));
        for (int n = 0; n < 5; ++n) addPluck(s, at(0.1 + 0.008 * n), 0.1, 0.6, fr[n]);
        Params q; q.attackMs = 600.f; q.follow = 1.f;
        SwelloEngine e; e.prepare(kSr); e.setParams(q);
        float dry = 0.f; const uint32_t before = e.latestNote().seq;
        for (float v : s) e.process(v, dry);
        const auto ni = e.latestNote();
        std::printf("  strum of 5 strings: %u event(s), attack %.3f s\n", (unsigned) (ni.seq - before), ni.attackSec);
        check(ni.seq - before == 1 && std::abs(ni.attackSec - 0.6f) < 0.01f, "a strum counts as one note and keeps ATTACK");
    }

    // 15) FEEDBACK: a held note blooms an overtone and is held up
    std::printf("Test 15: feedback (220 Hz note decaying 6 dB/s, attack 100 ms, rise 1.2 s)\n");
    {
        std::vector<float> x((size_t) at(4.0));
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = (double) i / kSr;
            x[i] = (float) (0.25 * std::pow(10.0, -6.0 * t / 20.0) * std::sin(2 * kPi * 220.0 * t));
        }
        const int lat = kLatencySamples, w = 4800;
        auto probe = [&](float fb, int harmonic, double t, double f)
        {
            Params p; p.attackMs = 100.f; p.feedback = fb; p.fbHarmonic = harmonic;
            const Out o = run(x, p);
            return amp(o.wet, at(t) + lat, w, f);
        };
        const double offOct   = probe(0.f, 0, 3.0, 440.0),  offFund = probe(0.f, 0, 3.0, 220.0);
        const double earlyOct = probe(1.f, 0, 0.3, 440.0);
        const double lateOct  = probe(1.f, 0, 3.0, 440.0),  lateFund = probe(1.f, 0, 3.0, 220.0);
        const double late12th = probe(1.f, 1, 3.0, 660.0);
        std::printf("  at 3 s: feedback 0 -> 220 Hz %.4f, 440 Hz %.4f | feedback 1 -> 220 Hz %.4f, 440 Hz %.4f (at 0.3 s: %.4f), 12th mode 660 Hz %.4f\n",
                    offFund, offOct, lateFund, lateOct, earlyOct, late12th);
        check(offOct < 0.002, "feedback off: no overtone");
        check(earlyOct < 0.1 * lateOct, "the overtone is not there right after the pick");
        check(lateOct > 0.4 * lateFund, "after the rise time the octave has bloomed in");
        check(lateFund > 2.0 * offFund, "the fed-back note is held up (> 6 dB over its natural decay)");
        check(late12th > 0.4 * lateFund, "12th mode blooms at 3x the frequency");

        Params p; p.attackMs = 100.f; p.feedback = 1.f; p.sustain = 1.f;
        const Out o = run(x, p);
        float pk = 0, inPk = 0;
        for (size_t i = 0; i < x.size(); ++i) { pk = std::max(pk, std::abs(o.wet[i])); inPk = std::max(inPk, std::abs(x[i])); }
        std::printf("  feedback + sustain fully on: output peak %.3f (input peak %.3f)\n", pk, inPk);
        check(pk < 2.0f * inPk, "no runaway: output stays below 2x the input peak");
    }

    // 16) low-latency engine (512-point FFT) keeps the core behaviour
    std::printf("Test 16: low-latency engine (512-point FFT, latency %d samples)\n", SwelloEngineLL::kLatencySamples);
    {
        auto runLL = [](const std::vector<float>& in, const Params& p)
        {
            SwelloEngineLL e; e.prepare(kSr); e.setParams(p);
            Out o; o.wet.resize(in.size()); o.dry.resize(in.size());
            for (size_t i = 0; i < in.size(); ++i) o.wet[i] = e.process(in[i], o.dry[i]);
            return o;
        };
        std::vector<float> x((size_t) at(2.0));
        for (size_t i = 0; i < x.size(); ++i) x[i] = 0.2f * (float) std::sin(2 * kPi * 330.0 * i / kSr);
        Params p; p.attackMs = 500.f;
        const Out o = runLL(x, p);
        const int lat = SwelloEngineLL::kLatencySamples;
        const double early = amp(o.wet, at(0.05) + lat, 2400, 330.0), late = amp(o.wet, at(1.5) + lat, 4800, 330.0);
        const double dryLate = amp(o.dry, at(1.5) + lat, 4800, 330.0);
        std::printf("  330 Hz: wet at 50 ms %.4f, at 1.5 s %.4f (dry %.4f)\n", early, late, dryLate);
        check(SwelloEngineLL::kLatencySamples == 512, "latency is 512 samples (10.7 ms at 48 kHz)");
        check(early < 0.1 * late, "starts near silent");
        check(std::abs(late / dryLate - 1.0) < 0.02, "reconstructs to unity after the attack");

        std::vector<float> c((size_t) at(1.5));
        addPluck(c, at(1.0), 0.3, 0.004, 3000.0);
        const Out oc = runLL(c, Params{});
        const double r = eventRatio(oc, at(1.0) + lat, at(0.15));
        std::printf("  click: wet/dry energy %.5f\n", r);
        check(r < 0.02, "a click is still swallowed by the swell");
    }

    // 17) per-note tremolo
    std::printf("Test 17: per-note tremolo (depth 1, 4 Hz; 220 Hz at 0 s, 550 Hz at 0.375 s)\n");
    {
        const double fa = 220.0, fb = 550.0, tB = 0.375;   // > ~140 Hz apart: separate bins (see CLAUDE.md)
        std::vector<float> x((size_t) at(3.0));
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = (double) i / kSr;
            x[i] = (float) (0.15 * std::sin(2 * kPi * fa * t) + (t >= tB ? 0.15 * std::sin(2 * kPi * fb * t) : 0.0));
        }
        Params p; p.attackMs = 10.f; p.tremDepth = 1.f; p.tremRateHz = 4.f;
        const Out o = run(x, p);
        const int lat = kLatencySamples, w = 1200;   // 25 ms probes
        // B's own LFO starts at its pick: peaks at tB + n/4, troughs at tB + 0.125 + n/4.
        // A started 0.375 s earlier = 1.5 cycles, so A is in its trough when B peaks.
        double bPk = 0, bTr = 0, aAtBPk = 0, aAtBTr = 0;
        for (int n = 2; n < 8; ++n)
        {
            const double tp = tB + n * 0.25, tt = tp + 0.125;
            bPk += amp(o.wet, at(tp) + lat - w / 2, w, fb);   bTr += amp(o.wet, at(tt) + lat - w / 2, w, fb);
            aAtBPk += amp(o.wet, at(tp) + lat - w / 2, w, fa); aAtBTr += amp(o.wet, at(tt) + lat - w / 2, w, fa);
        }
        std::printf("  note B at its own peaks %.3f / troughs %.3f;  note A at those times %.3f / %.3f\n",
                    bPk / 6, bTr / 6, aAtBPk / 6, aAtBTr / 6);
        check(bPk > 3.0 * bTr, "each note pulses (deep tremolo)");
        check(aAtBTr > 3.0 * aAtBPk, "notes pulse on their own phase, started by their own pick");
        Params q; q.attackMs = 10.f;
        const Out o0 = run(x, q);
        check(std::abs(amp(o0.wet, at(2.0) + lat, 4800, fa) / 0.15 - 1.0) < 0.03, "depth 0: no modulation");
    }

    // 18) SPREAD: notes placed left / right
    std::printf("Test 18: spread (alternate mode, full; note A at 0.1 s, note B at 0.6 s)\n");
    {
        std::vector<float> x((size_t) at(1.6));
        addPluck(x, at(0.1), 0.2, 1.0, 220.0);
        addPluck(x, at(0.6), 0.2, 1.0, 330.0);
        auto runSt = [&](float spread, std::vector<float>& L, std::vector<float>& R)
        {
            Params p; p.attackMs = 20.f; p.spread = spread;
            SwelloEngine e; e.prepare(kSr); e.setParams(p);
            L.resize(x.size()); R.resize(x.size());
            float dry = 0.f;
            for (size_t i = 0; i < x.size(); ++i) L[i] = e.processStereo(x[i], dry, R[i]);
        };
        std::vector<float> L, R, L0, R0;
        runSt(1.f, L, R); runSt(0.f, L0, R0);
        const int lat = kLatencySamples, w = 4800;
        const double aL = amp(L, at(1.2) + lat, w, 220.0), aR = amp(R, at(1.2) + lat, w, 220.0);
        const double bL = amp(L, at(1.2) + lat, w, 330.0), bR = amp(R, at(1.2) + lat, w, 330.0);
        double diff0 = 0; for (size_t i = 0; i < L0.size(); ++i) diff0 = std::max(diff0, (double) std::abs(L0[i] - R0[i]));
        std::printf("  note A: L %.3f R %.3f | note B: L %.3f R %.3f | spread 0: max |L-R| %.6f\n", aL, aR, bL, bR, diff0);
        check((aL > 4.0 * aR && bR > 4.0 * bL) || (aR > 4.0 * aL && bL > 4.0 * bR), "consecutive notes land on opposite sides");
        check(diff0 < 1e-6, "spread 0: identical left and right");
    }

    // 19) sub-octave feedback
    std::printf("Test 19: sub-octave feedback (440 Hz held note)\n");
    {
        std::vector<float> x((size_t) at(4.0));
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = (double) i / kSr;
            x[i] = (float) (0.25 * std::pow(10.0, -6.0 * t / 20.0) * std::sin(2 * kPi * 440.0 * t));
        }
        Params p; p.attackMs = 100.f; p.feedback = 1.f; p.fbHarmonic = 3;
        const Out o = run(x, p);
        const int lat = kLatencySamples;
        const double sub = amp(o.wet, at(3.0) + lat, 9600, 220.0), fund = amp(o.wet, at(3.0) + lat, 9600, 440.0);
        const double early = amp(o.wet, at(0.3) + lat, 4800, 220.0);
        std::printf("  at 3 s: 220 Hz %.4f, 440 Hz %.4f (220 Hz at 0.3 s: %.4f)\n", sub, fund, early);
        check(sub > 0.3 * fund, "the octave below blooms in");
        check(early < 0.1 * sub, "not there right after the pick");
    }

    // 20) SPREAD by pitch and by pick strength, immediate (harmonizer) feedback
    std::printf("Test 20: spread by pitch / by pick, immediate feedback\n");
    {
        auto runSt = [](const std::vector<float>& x, const Params& p, std::vector<float>& L, std::vector<float>& R)
        {
            SwelloEngine e; e.prepare(kSr); e.setParams(p);
            L.resize(x.size()); R.resize(x.size());
            float dry = 0.f;
            for (size_t i = 0; i < x.size(); ++i) L[i] = e.processStereo(x[i], dry, R[i]);
        };
        const int lat = kLatencySamples, w = 4800;

        // by pitch: a low E (82 Hz) then a high E (659 Hz), each with harmonics
        std::vector<float> x((size_t) at(2.2));
        addPluck(x, at(0.1), 0.2, 1.0, 82.4);
        addPluck(x, at(1.1), 0.2, 1.0, 659.3);
        Params p; p.attackMs = 20.f; p.spread = 1.f; p.spreadMode = 2;
        std::vector<float> L, R;
        runSt(x, p, L, R);
        const double lowL = amp(L, at(0.6) + lat, w, 164.8), lowR = amp(R, at(0.6) + lat, w, 164.8);    // 2nd harmonic of the low E
        const double hiL  = amp(L, at(1.6) + lat, w, 659.3), hiR  = amp(R, at(1.6) + lat, w, 659.3);
        std::printf("  by pitch: low E  L %.4f R %.4f | high E  L %.4f R %.4f\n", lowL, lowR, hiL, hiR);
        check(lowL > 4.0 * lowR, "by pitch: a low note (all its harmonics) goes left");
        check(hiR > 4.0 * hiL, "by pitch: a high note goes right");

        // by pick: medium plucks to learn the average, then a hard and a soft one
        const double lv[] = { 0.08, 0.08, 0.08, 0.08, 0.32, 0.08, 0.02 };
        const double fr[] = { 196.0, 247.0, 294.0, 196.0, 247.0, 294.0, 196.0 };
        std::vector<float> y((size_t) at(4.6));
        for (int n = 0; n < 7; ++n) addPluck(y, at(0.1 + 0.6 * n), lv[n], 0.25, fr[n]);
        Params q; q.attackMs = 20.f; q.spread = 1.f; q.spreadMode = 3;
        runSt(y, q, L, R);
        const double hL = amp(L, at(2.6) + lat, 2400, 247.0), hR = amp(R, at(2.6) + lat, 2400, 247.0);
        const double sL = amp(L, at(3.8) + lat, 2400, 196.0), sR = amp(R, at(3.8) + lat, 2400, 196.0);
        std::printf("  by pick: hard  L %.4f R %.4f | soft  L %.4f R %.4f\n", hL, hR, sL, sR);
        check(hR > 3.0 * hL, "by pick: a hard pick goes right");
        check(sL > 2.0 * sR, "by pick: a soft pick goes clearly left (quiet notes share some bins with ringing ones)");

        // immediate feedback = harmonizer: the octave is there right after the swell, no waiting
        std::vector<float> z((size_t) at(1.0));
        for (size_t i = 0; i < z.size(); ++i) z[i] = 0.2f * (float) std::sin(2 * kPi * 220.0 * i / kSr);
        Params f; f.attackMs = 50.f; f.feedback = 1.f; f.fbMode = 1;
        const Out o = run(z, f);
        const double oct = amp(o.wet, at(0.25) + lat, 4800, 440.0), fund = amp(o.wet, at(0.25) + lat, 4800, 220.0);
        std::printf("  immediate: at 0.25 s  220 Hz %.4f, 440 Hz %.4f\n", fund, oct);
        check(oct > 0.5 * fund, "immediate mode: the octave sounds from the start (harmonizer)");
        check(fund < 0.22, "immediate mode: the note itself is not held up or boosted");
    }

    // 21) VOICE: a synth tone rebuilt from each note
    std::printf("Test 21: voice layer (saw wave)\n");
    {
        auto runVoice = [](const std::vector<float>& x, const Params& p, std::vector<float>& V, std::vector<float>* W = nullptr)
        {
            SwelloEngine e; e.prepare(kSr); e.setParams(p);
            V.resize(x.size());
            if (W) W->resize(x.size());
            float dry = 0.f, r = 0.f, vr = 0.f;
            for (size_t i = 0; i < x.size(); ++i)
            {
                const float w = e.processStereo(x[i], dry, r, &V[i], &vr);
                if (W) (*W)[i] = w;
            }
        };
        const int lat = kLatencySamples, w = 9600;
        std::vector<float> x((size_t) at(2.0));
        for (size_t i = 0; i < x.size(); ++i) x[i] = 0.2f * (float) std::sin(2 * kPi * 220.0 * i / kSr);
        Params p; p.attackMs = 10.f; p.voiceMix = 1.f; p.voiceWave = 1; p.voiceToneHz = 12000.f;
        std::vector<float> V;
        runVoice(x, p, V);
        const double h1 = amp(V, at(1.0) + lat, w, 220.0), h2 = amp(V, at(1.0) + lat, w, 440.0), h3 = amp(V, at(1.0) + lat, w, 660.0);
        std::printf("  220 Hz sine -> saw voice: h1 %.4f, h2 %.4f (%.2f), h3 %.4f (%.2f)\n", h1, h2, h2 / h1, h3, h3 / h1);
        check(h1 > 0.05, "the voice sounds at the note's pitch");
        check(std::abs(h2 / h1 - 0.5) < 0.12 && std::abs(h3 / h1 - 0.333) < 0.1, "saw wave: harmonics at 1/2, 1/3 ...");

        std::vector<float> y((size_t) at(2.0));
        for (size_t i = 0; i < y.size(); ++i)
            y[i] = 0.15f * (float) (std::sin(2 * kPi * 220.0 * i / kSr) + std::sin(2 * kPi * 330.0 * i / kSr));
        runVoice(y, p, V);
        const double a = amp(V, at(1.0) + lat, w, 220.0), b = amp(V, at(1.0) + lat, w, 330.0), sub = amp(V, at(1.0) + lat, w, 110.0);
        std::printf("  220 + 330 Hz (a fifth): voice 220 %.4f, 330 %.4f, 110 (must be empty) %.4f\n", a, b, sub);
        check(a > 0.04 && b > 0.04, "two notes give two voices (polyphonic)");
        check(sub < 0.1 * a, "a fifth is not mistaken for a missing low fundamental");

        Params q = p; q.voiceOct = 2;
        runVoice(x, q, V);
        const double up = amp(V, at(1.0) + lat, w, 440.0), at220 = amp(V, at(1.0) + lat, w, 220.0);
        std::printf("  octave +1: 440 Hz %.4f, 220 Hz %.4f\n", up, at220);
        check(up > 0.05 && at220 < 0.1 * up, "octave +1 moves the voice up an octave");

        // low notes and chords (long-window pitch tracker)
        auto harmonicNote = [](std::vector<float>& v, double f0, double a, int hFrom, int hTo)
        {
            for (size_t i = 0; i < v.size(); ++i)
            {
                double s = 0;
                for (int h = hFrom; h <= hTo; ++h) s += a / h * std::sin(2 * kPi * f0 * h * i / kSr);
                v[i] += (float) s;
            }
        };
        std::vector<float> z((size_t) at(2.0), 0.f);
        harmonicNote(z, 82.4, 0.08, 2, 6);                                   // low E, fundamental missing
        runVoice(z, p, V);
        const double lowE = amp(V, at(1.0) + lat, 19200, 82.4), lowE2 = amp(V, at(1.0) + lat, 19200, 164.8);
        std::printf("  low E without its fundamental: voice at 82.4 Hz %.4f (164.8 Hz %.4f)\n", lowE, lowE2);
        check(lowE > 0.01 && lowE > 1.5 * lowE2, "a low E with a missing fundamental plays at 82 Hz");

        std::vector<float> c((size_t) at(2.0), 0.f);                         // C major: C3, E3, G3 with harmonics
        harmonicNote(c, 130.8, 0.06, 1, 8); harmonicNote(c, 164.8, 0.06, 1, 8); harmonicNote(c, 196.0, 0.06, 1, 8);
        runVoice(c, p, V);
        const double vc = amp(V, at(1.0) + lat, 19200, 130.8), ve = amp(V, at(1.0) + lat, 19200, 164.8), vg = amp(V, at(1.0) + lat, 19200, 196.0);
        const double vLow = amp(V, at(1.0) + lat, 19200, 65.4);
        std::printf("  C major chord: voice C3 %.4f, E3 %.4f, G3 %.4f, C2 (must be empty) %.4f\n", vc, ve, vg, vLow);
        check(vc > 0.01 && ve > 0.01 && vg > 0.01, "a three-note chord gives three voices");
        check(vLow < 0.25 * std::min(vc, std::min(ve, vg)), "no phantom note below the chord");

        std::vector<float> pc((size_t) at(2.0), 0.f);                        // power chord E3 + B3
        harmonicNote(pc, 164.8, 0.07, 1, 6); harmonicNote(pc, 247.0, 0.07, 1, 6);
        runVoice(pc, p, V);
        const double pE = amp(V, at(1.0) + lat, 19200, 164.8), pB = amp(V, at(1.0) + lat, 19200, 247.0), pLow = amp(V, at(1.0) + lat, 19200, 82.4);
        std::printf("  power chord E3 + B3: voice E3 %.4f, B3 %.4f, E2 (must be empty) %.4f\n", pE, pB, pLow);
        check(pE > 0.01 && pB > 0.01 && pLow < 0.25 * std::min(pE, pB), "a power chord is two notes, not a lower phantom");
    }

    // 22) VIBRATO: per note, after the delay
    std::printf("Test 22: vibrato (40 cents, 5 Hz, delay 0.5 s)\n");
    {
        std::vector<float> x((size_t) at(2.5));
        for (size_t i = 0; i < x.size(); ++i) x[i] = 0.2f * (float) std::sin(2 * kPi * 330.0 * i / kSr);
        // instantaneous frequency from zero crossings in 30 ms windows: its spread over a time span
        auto freqSpread = [](const std::vector<float>& v, double t0, double t1)
        {
            double lo = 1e9, hi = 0;
            for (double t = t0; t < t1; t += 0.01)
            {
                const int a = at(t), b = at(t + 0.03);
                int zc = 0; double first = -1, last = -1;
                for (int i = a + 1; i < b; ++i)
                    if (v[(size_t) i - 1] < 0 && v[(size_t) i] >= 0)
                    {
                        const double frac = (i - 1) + v[(size_t) i - 1] / (v[(size_t) i - 1] - v[(size_t) i]);
                        if (first < 0) first = frac; last = frac; ++zc;
                    }
                if (zc < 3) continue;
                const double f = (zc - 1) * kSr / (last - first);
                lo = std::min(lo, f); hi = std::max(hi, f);
            }
            return hi - lo;
        };
        Params p; p.attackMs = 10.f; p.voiceMix = 1.f; p.voiceWave = 5; p.voiceToneHz = 400.f;
        p.vibCents = 40.f; p.vibRateHz = 5.f; p.vibDelaySec = 0.5f;
        SwelloEngine e; e.prepare(kSr); e.setParams(p);
        std::vector<float> V(x.size()), G(x.size());
        float dry = 0.f, r = 0.f, vr = 0.f;
        for (size_t i = 0; i < x.size(); ++i) G[i] = e.processStereo(x[i], dry, r, &V[i], &vr);
        const double lat = (double) kLatencySamples / kSr;
        const double before = freqSpread(V, 0.15 + lat, 0.45 + lat), after = freqSpread(V, 1.2 + lat, 2.2 + lat);
        const double guitar = freqSpread(G, 1.2 + lat, 2.2 + lat);
        std::printf("  voice pitch wobble: before the delay %.1f Hz, after %.1f Hz (40 cents at 330 Hz = +-7.7 Hz); guitar %.1f Hz\n",
                    before, after, guitar);
        check(before < 2.0, "no vibrato before the delay");
        check(after > 9.0, "vibrato after the delay");
        check(guitar < 2.0, "target 'voice': the guitar stays straight");

        Params q = p; q.vibTarget = 1; q.voiceMix = 0.f;
        SwelloEngine e2; e2.prepare(kSr); e2.setParams(q);
        for (size_t i = 0; i < x.size(); ++i) G[i] = e2.process(x[i], dry);
        const double gAfter = freqSpread(G, 1.2 + lat, 2.2 + lat);
        std::printf("  target 'guitar' (engine 1): guitar wobble %.1f Hz\n", gAfter);
        check(gAfter > 9.0, "target 'guitar': the guitar bends");

        // the three targets (engine: 0 voice, 1 guitar only, 2 both): who bends?
        for (int tg = 1; tg <= 2; ++tg)
        {
            Params t = p; t.vibTarget = tg;
            SwelloEngine et; et.prepare(kSr); et.setParams(t);
            std::vector<float> V2(x.size()), G2(x.size());
            for (size_t i = 0; i < x.size(); ++i) G2[i] = et.processStereo(x[i], dry, r, &V2[i], &vr);
            const double v = freqSpread(V2, 1.2 + lat, 2.2 + lat), gg = freqSpread(G2, 1.2 + lat, 2.2 + lat);
            std::printf("  target %s: voice wobble %.1f Hz, guitar %.1f Hz\n", tg == 1 ? "'guitar'" : "'both'", v, gg);
            check(gg > 9.0, "the guitar bends");
            check(tg == 1 ? v < 2.0 : v > 9.0, tg == 1 ? "target 'guitar': the voice stays straight" : "target 'both': the voice bends too");
        }

        // low D (73.4 Hz, 8 harmonics) with guitar vibrato: the level must stay steady (no artifacts / dropouts)
        std::vector<float> d((size_t) at(2.5));
        for (size_t i = 0; i < d.size(); ++i)
        {
            double v = 0;
            for (int h = 1; h <= 8; ++h) v += 0.06 / h * std::sin(2 * kPi * 73.4 * h * i / kSr);
            d[i] = (float) v;
        }
        Params lv; lv.attackMs = 10.f; lv.vibCents = 40.f; lv.vibRateHz = 5.f; lv.vibDelaySec = 0.5f; lv.vibTarget = 1;
        SwelloEngine e3; e3.prepare(kSr); e3.setParams(lv);
        std::vector<float> D(d.size());
        for (size_t i = 0; i < d.size(); ++i) D[i] = e3.process(d[i], dry);
        double lo = 1e9, hi = 0;
        for (double t = 1.2; t < 2.3; t += 0.02)
        {
            const double r = std::sqrt(energy(D, at(t) + kLatencySamples, at(0.04)) / at(0.04));
            lo = std::min(lo, r); hi = std::max(hi, r);
        }
        std::printf("  low D with guitar vibrato: level in 40 ms windows %.4f .. %.4f\n", lo, hi);
        check(hi < 1.25 * lo, "guitar vibrato on a low note keeps a steady level (no artifacts)");
    }

    // 23) voice effects: unison, chorus, phaser
    std::printf("Test 23: voice effects (unison, chorus, phaser)\n");
    {
        std::vector<float> x((size_t) at(2.0));
        for (size_t i = 0; i < x.size(); ++i) x[i] = 0.3f * (float) std::sin(2 * kPi * 440.0 * i / kSr);
        auto runFx = [&](const VoiceFx::Settings& s, std::vector<float>& L, std::vector<float>& R)
        {
            VoiceFx fx; fx.prepare(kSr);
            L.resize(x.size()); R.resize(x.size());
            for (size_t i = 0; i < x.size(); ++i) { float l = x[i], r = x[i]; fx.process(l, r, s); L[i] = l; R[i] = r; }
        };
        std::vector<float> L, R;
        VoiceFx::Settings u; u.unison = 1.f; u.detune = 20.f;
        runFx(u, L, R);
        double diff = 0, pk = 0;
        for (size_t i = (size_t) at(0.5); i < x.size(); ++i) { diff += std::abs(L[i] - R[i]); pk = std::max(pk, (double) std::max(std::abs(L[i]), std::abs(R[i]))); }
        diff /= (double) (x.size() - (size_t) at(0.5));
        const double up = amp(L, at(1.0), 48000, 440.0 * std::pow(2.0, 20.0 / 1200.0)) + amp(R, at(1.0), 48000, 440.0 * std::pow(2.0, 20.0 / 1200.0));
        std::printf("  unison: mean |L-R| %.4f, energy at +20 cents %.4f, peak %.3f\n", diff, up, pk);
        check(diff > 0.02, "unison spreads the voice across the stereo field");
        check(up > 0.02, "unison adds detuned copies");
        check(pk < 0.9, "unison does not blow up the level");

        VoiceFx::Settings c; c.chorus = 1;
        runFx(c, L, R);
        double cdiff = 0; for (size_t i = (size_t) at(0.5); i < x.size(); ++i) cdiff += std::abs(L[i] - R[i]);
        cdiff /= (double) (x.size() - (size_t) at(0.5));
        std::printf("  chorus I: mean |L-R| %.4f\n", cdiff);
        check(cdiff > 0.02, "chorus makes a moving stereo image");

        VoiceFx::Settings ph; ph.phaser = 1.f;
        runFx(ph, L, R);
        double lo = 1e9, hi = 0;
        for (double t = 0.2; t < 1.9; t += 0.05) { const double a = amp(L, at(t), 2400, 440.0); lo = std::min(lo, a); hi = std::max(hi, a); }
        std::printf("  phaser: level at 440 Hz moves between %.3f and %.3f\n", lo, hi);
        check(hi > 1.5 * lo, "phaser sweeps notches through the sound");
    }

    // 24) PACE: a ringing note's wobbles are not new notes; PICK -> filter; no voice thump from noise
    std::printf("Test 24: no false notes, PICK filter, no voice from noise\n");
    {
        // one plucked note with beating partials (220 / 221.5 Hz and 440 / 443 Hz) ringing for 2 s
        std::vector<float> x((size_t) at(2.2));
        for (size_t i = (size_t) at(0.1); i < x.size(); ++i)
        {
            const double t = (double) i / kSr - 0.1, env = std::exp(-t / 0.9);
            x[i] = (float) (env * (0.12 * std::sin(2 * kPi * 220.0 * t) + 0.12 * std::sin(2 * kPi * 221.5 * t)
                                 + 0.06 * std::sin(2 * kPi * 440.0 * t) + 0.06 * std::sin(2 * kPi * 443.0 * t)));
        }
        Params p; p.attackMs = 600.f; p.follow = 1.f;
        SwelloEngine e; e.prepare(kSr); e.setParams(p);
        float dry = 0.f; const uint32_t s0 = e.latestNote().seq;
        float minAttack = 10.f;
        for (size_t i = 0; i < x.size(); ++i)
        {
            e.process(x[i], dry);
            if (e.latestNote().seq != s0) minAttack = std::min(minAttack, e.latestNote().attackSec);
        }
        const uint32_t events = e.latestNote().seq - s0;
        std::printf("  beating note: %u note event(s), shortest attack given %.3f s (ATTACK 0.6 s, PACE full)\n", (unsigned) events, minAttack);
        check(events == 1, "a beating, ringing note is one note (no false re-picks)");
        check(minAttack > 0.55f, "PACE leaves a single note's attack alone");

        // PICK -> filter: after medium picks, a hard pick is brighter than a soft one (filter low res, 600 Hz)
        const double lv[] = { 0.08, 0.08, 0.08, 0.08, 0.32, 0.08, 0.02 };
        std::vector<float> y((size_t) at(4.6));
        // different pitches per note (a quieter re-pick of the SAME note doesn't stand out against its louder tail)
        const double pf[] = { 196.0, 247.0, 294.0, 196.0, 247.0, 294.0, 196.0 };
        for (int n = 0; n < 7; ++n) addPluck(y, at(0.1 + 0.6 * n), lv[n], 0.25, pf[n]);
        auto h5 = [&](float pick, double t, double f0)   // level of the 5th harmonic of the note at time t
        {
            Params q; q.attackMs = 20.f; q.filterMode = 1; q.freqHz = 600.f; q.pickFilter = pick;
            const Out o = run(y, q);
            return amp(o.wet, at(t) + kLatencySamples, 2400, f0 * 5);
        };
        const double hardOn = h5(1.f, 2.6, 247.0), hardOff = h5(0.f, 2.6, 247.0), softOn = h5(1.f, 3.8, 196.0), softOff = h5(0.f, 3.8, 196.0);
        std::printf("  5th partial: hard pick PICK+1 %.5f vs off %.5f | soft pick PICK+1 %.5f vs off %.5f\n", hardOn, hardOff, softOn, softOff);
        check(hardOn > 2.0 * hardOff, "PICK +1: a hard pick opens the filter (brighter)");
        check(softOn < 0.5 * softOff, "PICK +1: a soft pick closes it (darker)");

        // 30 ms of string-scrape noise: the voice must stay (almost) silent
        std::vector<float> z((size_t) at(1.0), 0.f);
        for (int i = at(0.3); i < at(0.33); ++i) z[(size_t) i] = 0.05f * noise();
        Params v; v.attackMs = 10.f; v.voiceMix = 1.f;
        SwelloEngine ev; ev.prepare(kSr); ev.setParams(v);
        double vE = 0, inE = 0; float r = 0.f, vl = 0.f, vr = 0.f;
        for (size_t i = 0; i < z.size(); ++i) { ev.processStereo(z[i], dry, r, &vl, &vr); vE += (double) vl * vl; inE += (double) z[i] * z[i]; }
        std::printf("  noise burst: voice energy / input energy %.4f\n", vE / inE);
        check(vE < 0.05 * inE, "a noise burst does not make the voice thump");
    }

    // 25) ATTACK 0: no swell, a note comes out at full level straight away
    std::printf("Test 25: ATTACK 0 = no swell\n");
    {
        std::vector<float> x((size_t) at(0.6));
        for (size_t i = at(0.1); i < x.size(); ++i) x[i] = 0.2f * (float) std::sin(2 * kPi * 330.0 * i / kSr);
        const int t0 = at(0.1) + kLatencySamples;
        auto lvl = [&](const Out& o, double t, double len) { return std::sqrt(energy(o.wet, t0 + at(t), at(len)) / at(len)); };
        Params p0; p0.attackMs = 0.f;  const auto o0 = run(x, p0);
        Params p1; p1.attackMs = 30.f; const auto o1 = run(x, p1);
        const double full = lvl(o0, 0.3, 0.05);
        std::printf("  level 0..10 / 10..20 ms after the note: ATTACK 0: %.2f / %.2f, ATTACK 30 ms: %.2f / %.2f (of %.2f)\n",
                    lvl(o0, 0.0, 0.01), lvl(o0, 0.01, 0.01), lvl(o1, 0.0, 0.01), lvl(o1, 0.01, 0.01), full);
        check(lvl(o0, 0.01, 0.01) > 0.9 * full, "ATTACK 0: full level after 10 ms (only the STFT frame ramps it)");
        check(lvl(o0, 0.0, 0.01) > 1.5 * lvl(o1, 0.0, 0.01), "ATTACK 0 starts clearly faster than 30 ms");
    }

    // 26) vocal voice controls: VOWEL, SCOOP, BREATH, WARMTH
    std::printf("Test 26: VOWEL, SCOOP, BREATH, WARMTH\n");
    {
        std::vector<float> x((size_t) at(1.2));
        for (size_t i = at(0.1); i < x.size(); ++i)
        {
            double v = 0;
            for (int h = 1; h <= 6; ++h) v += 0.08 / h * std::sin(2 * kPi * 220.0 * h * i / kSr);
            x[i] = (float) v;
        }
        auto voice = [&](const Params& p)
        {
            auto e = std::make_unique<SwelloEngine>(); e->prepare(kSr); e->setParams(p);
            std::vector<float> V(x.size()); float dry = 0.f, r = 0.f, vr = 0.f;
            for (size_t i = 0; i < x.size(); ++i) e->processStereo(x[i], dry, r, &V[i], &vr);
            return V;
        };
        const int a0 = at(0.7) + kLatencySamples, n = at(0.3);
        Params base; base.attackMs = 10.f; base.voiceMix = 1.f; base.voiceWave = 1;
        Params ah = base; ah.voiceVowel = 0.525f;   // "ah"
        Params ee = base; ee.voiceVowel = 1.0f;     // "ee"
        const auto Va = voice(ah), Ve = voice(ee), V0 = voice(base);
        const double ra = amp(Va, a0, n, 2200.0) / amp(Va, a0, n, 880.0), re = amp(Ve, a0, n, 2200.0) / amp(Ve, a0, n, 880.0);
        const double la = std::sqrt(energy(Va, a0, n)), l0 = std::sqrt(energy(V0, a0, n));
        std::printf("  2.2 kHz / 880 Hz: 'ah' %.2f, 'ee' %.2f; level with vowel / without %.2f\n", ra, re, la / l0);
        check(re > 2.0 * ra, "VOWEL 'ee' has a much higher 2nd formant than 'ah'");
        check(la / l0 > 0.7 && la / l0 < 1.4, "VOWEL shapes the tone without changing the level much");

        // SCOOP: the voice starts below pitch and slides up
        Params sc = base; sc.voiceWave = 5; sc.voiceToneHz = 400.f; sc.voiceScoop = 1.f; sc.attackMs = 600.f;
        Params st = sc; st.voiceScoop = 0.f;
        auto zcFreq = [](const std::vector<float>& v, int a, int b)
        {
            int zc = 0; double first = -1, last = -1;
            for (int i = a + 1; i < b; ++i)
                if (v[(size_t) i - 1] < 0 && v[(size_t) i] >= 0)
                { const double fr = (i - 1) + v[(size_t) i - 1] / (v[(size_t) i - 1] - v[(size_t) i]); if (first < 0) first = fr; last = fr; ++zc; }
            return zc > 2 ? (zc - 1) * kSr / (last - first) : 0.0;
        };
        const auto Vs = voice(sc), Vt = voice(st);
        int on = 0;   // where the voice comes in
        for (int i = at(0.1); i < (int) Vs.size() - at(0.01); i += at(0.005))
            if (std::sqrt(energy(Vs, i, at(0.01)) / at(0.01)) > 0.002) { on = i; break; }
        const double early = zcFreq(Vs, on, on + at(0.04)), late = zcFreq(Vs, a0, a0 + at(0.2)), plain = zcFreq(Vt, on, on + at(0.04));
        std::printf("  SCOOP: first 40 ms %.1f Hz, later %.1f Hz (no scoop: %.1f Hz)\n", early, late, plain);
        check(early > 0 && early < 0.993 * late, "SCOOP: a note starts below pitch (eases up, up to 60 cents)");
        check(std::abs(late - 220.0) < 2.0, "SCOOP: then settles on pitch");

        // BREATH: noise between the harmonics
        Params br = base; br.voiceBreath = 1.f;
        const auto Vb = voice(br);
        const double nb = amp(Vb, a0, n, 3410.0), n0 = amp(V0, a0, n, 3410.0);
        const double lb = std::sqrt(energy(Vb, a0, n)), lv0 = std::sqrt(energy(V0, a0, n));
        std::printf("  BREATH: level between harmonics (3.4 kHz) %.2e vs %.2e without; total level x%.2f\n", nb, n0, lb / lv0);
        check(lb / lv0 < 1.5, "BREATH stays a layer of air, not a wall of noise");
        check(nb > 3.0 * n0, "BREATH adds air between the harmonics");

        // WARMTH: more 2nd harmonic, less top
        Params wm = base; wm.voiceWarmth = 1.f;
        const auto Vw = voice(wm);
        const double r2w = amp(Vw, a0, n, 440.0) / amp(Vw, a0, n, 220.0), r20 = amp(V0, a0, n, 440.0) / amp(V0, a0, n, 220.0);
        const double hw = amp(Vw, a0, n, 2640.0) / amp(Vw, a0, n, 220.0), h0 = amp(V0, a0, n, 2640.0) / amp(V0, a0, n, 220.0);
        std::printf("  WARMTH: 2nd/1st %.2f vs %.2f, 12th/1st %.3f vs %.3f\n", r2w, r20, hw, h0);
        check(r2w > 1.2 * r20 && hw < 0.8 * h0, "WARMTH: rounder (more 2nd harmonic, softer highs)");

        // VG (bowed): a fixed body - the bridge hill (~2.8 kHz) stands out over the dip (~1.5 kHz), level kept
        Params vg = base; vg.voiceWave = 7;
        const auto Vg = voice(vg);
        const double bv = amp(Vg, a0, n, 2860.0) / amp(Vg, a0, n, 1540.0), bs = amp(V0, a0, n, 2860.0) / amp(V0, a0, n, 1540.0);
        const double lg = std::sqrt(energy(Vg, a0, n)) / std::sqrt(energy(V0, a0, n));
        std::printf("  VG bowed: 2.86/1.54 kHz %.2f (saw %.2f), level vs saw %.2f\n", bv, bs, lg);
        check(bv > 1.5 * bs && lg > 0.7 && lg < 1.4, "VG: bowed body resonance, same level");
    }

    // 27) SMEAR: reverb tail, gate HOLD and CUT, stable at full size
    std::printf("Test 27: SMEAR (gated reverb)\n");
    {
        // 0.5 s of a 330 Hz note, then silence; returns the reverb's RMS in windows after the note stops
        auto tail = [&](Smear::Settings st, double fromSec, double lenSec)
        {
            Smear sm; sm.prepare(kSr);
            const int total = at(3.5), stop = at(0.5);
            std::vector<float> o((size_t) total);
            for (int i = 0; i < total; ++i)
            {
                const float x = i < stop ? 0.3f * (float) std::sin(2 * kPi * 330.0 * i / kSr) : 0.f;
                float l, r; sm.process(x, x, x, st, l, r);
                o[(size_t) i] = 0.5f * (l + r);
            }
            return std::sqrt(energy(o, stop + at(fromSec), at(lenSec)) / at(lenSec));
        };
        Smear::Settings open; open.size = 0.5f;
        Smear::Settings gated = open; gated.gate = true; gated.holdMs = 100.f; gated.cutMs = 10.f;
        Smear::Settings soft = gated; soft.cutMs = 400.f;
        const double t0 = tail(open, 0.3, 0.1), tg = tail(gated, 0.3, 0.1), ts = tail(soft, 0.2, 0.1), tsOpen = tail(open, 0.2, 0.1);
        const double during = tail(open, -0.2, 0.1), held = tail(gated, 0.0, 0.08);
        std::printf("  tail 0.3 s after the note: open %.4f, gated (hold 100 ms, cut 10 ms) %.6f\n", t0, tg);
        std::printf("  gate open during HOLD: %.4f (open %.4f); soft cut (400 ms) at 0.2 s: %.4f of %.4f\n", held, tail(open, 0.0, 0.08), ts, tsOpen);
        check(during > 0.01 && t0 > 0.005, "the reverb builds and rings on after the note");
        check(tg < 0.02 * t0, "GATE: the tail is cut once HOLD and CUT have passed");
        check(held > 0.8 * tail(open, 0.0, 0.08), "GATE: stays open for HOLD after the playing stops");
        check(ts > 0.4 * tsOpen && ts < 0.95 * tsOpen, "CUT: a long cut fades the tail instead of chopping it");

        Smear::Settings big; big.size = 1.f; big.drive = 1.f;
        const double late = tail(big, 2.9, 0.1), early = tail(big, 0.0, 0.1);
        std::printf("  full size + drive: %.4f right after, %.4f 2.9 s later\n", early, late);
        check(std::isfinite(late) && late < early && late > 0.0, "full SIZE with DRIVE stays stable and keeps ringing");
    }

    // 28) BOWED (VG-8 spec v2): S-curve swell timing, smooth re-pick, EVEN
    std::printf("Test 28: bowed swell shape, smooth re-pick, EVEN\n");
    {
        // (a) the measured curve: 10 % ~60 ms, 50 % ~240 ms, 90 % ~420 ms of a 0.5 s attack
        std::vector<float> x((size_t) at(1.2));
        for (size_t i = at(0.1); i < x.size(); ++i) x[i] = 0.2f * (float) std::sin(2 * kPi * 330.0 * i / kSr);
        Params bw; bw.attackMs = 500.f; bw.swellShape = 1;
        const auto o = run(x, bw);
        const int t0 = at(0.1) + kLatencySamples;
        auto lv = [&](const std::vector<float>& v, double t) { return std::sqrt(energy(v, t0 + at(t - 0.01), at(0.02)) / at(0.02)); };
        const double full = lv(o.wet, 0.9);
        const double g60 = lv(o.wet, 0.06) / full, g240 = lv(o.wet, 0.24) / full, g420 = lv(o.wet, 0.42) / full;
        std::printf("  bowed swell: %.2f at 60 ms, %.2f at 240 ms, %.2f at 420 ms (VG-8: 0.10 / 0.50 / 0.90)\n", g60, g240, g420);
        check(g60 < 0.2 && g240 > 0.35 && g240 < 0.65 && g420 > 0.78, "BOWED swell follows the measured S-curve");

        // (b) re-pick the same note louder over its own tail: classic drops to silence, smooth never jumps out
        std::vector<float> y((size_t) at(1.6));
        for (size_t i = at(0.1); i < y.size(); ++i)
        {
            const double t = (double) i / kSr;
            const double env = t < 0.7 ? 0.1 * std::exp(-(t - 0.1) / 0.8) : 0.2 * std::exp(-(t - 0.7) / 0.8);
            y[i] = (float) (env * std::sin(2 * kPi * 220.0 * t));
        }
        auto repick = [&](bool smooth, double& before, double& dip, double& peak)
        {
            Params p; p.attackMs = 500.f; p.smoothRepick = smooth;
            const auto r = run(y, p);
            const int t1 = at(0.7) + kLatencySamples;
            auto l = [&](double t) { return std::sqrt(energy(r.wet, t1 + at(t), at(0.02)) / at(0.02)); };
            before = l(-0.05); dip = 1e9; peak = 0;
            for (double t = 0.0; t < 0.12; t += 0.01) { dip = std::min(dip, l(t)); peak = std::max(peak, l(t)); }
        };
        double b0, d0, p0, b1, d1, p1;
        repick(false, b0, d0, p0); repick(true, b1, d1, p1);
        std::printf("  re-pick: classic %.4f -> min %.4f; smooth %.4f -> min %.4f, max %.4f in the first 120 ms\n", b0, d0, b1, d1, p1);
        check(d0 < 0.3 * b0, "classic re-pick restarts the swell from (near) silence");
        check(d1 > 0.45 * b1 && p1 < 1.5 * b1, "smooth re-pick: a small dip, the new pick never jumps out");

        // (c) EVEN: a loud and a soft pick end up much closer in level
        std::vector<float> z((size_t) at(4.0));
        const double lvls[] = { 0.08, 0.08, 0.08, 0.08, 0.02, 0.3 };   // soft, then loud
        const double fs[] = { 196.0, 247.0, 294.0, 196.0, 247.0, 294.0 };
        for (int k = 0; k < 6; ++k) addPluck(z, at(0.1 + 0.6 * k), lvls[k], 0.6, fs[k]);
        auto levels = [&](float even)
        {
            Params p; p.attackMs = 100.f; p.even = even;
            const auto r = run(z, p);
            // each note at its own pitch (the loud note's tail would swamp a broadband measure of the soft one)
            auto l = [&](int k) { return amp(r.wet, at(0.1 + 0.6 * k + 0.2) + kLatencySamples, at(0.15), fs[k]); };
            return 20.0 * std::log10(l(5) / l(4));
        };
        const double dRaw = levels(0.f), dEven = levels(1.f);
        std::printf("  loud vs soft pick: %.1f dB apart (EVEN 0), %.1f dB (EVEN 1)\n", dRaw, dEven);
        check(dEven < 0.6 * dRaw, "EVEN brings loud and soft picks closer together");

        // (d) BODY: the measured VG-8 BOWED tone (500 Hz bump, ~-4.3 dB at 4 kHz, -4 dB lows)
        std::vector<float> w((size_t) at(1.2));
        for (size_t i = at(0.1); i < w.size(); ++i)
            w[i] = (float) (0.05 * (std::sin(2 * kPi * 100.0 * i / kSr) + std::sin(2 * kPi * 500.0 * i / kSr) + std::sin(2 * kPi * 4000.0 * i / kSr)));
        auto tone = [&](bool body, double f)
        {
            Params p; p.attackMs = 10.f; p.bowedBody = body;
            const auto r = run(w, p);
            return 20.0 * std::log10(amp(r.wet, at(0.7) + kLatencySamples, at(0.3), f) / amp(r.wet, at(0.7) + kLatencySamples, at(0.3), 500.0));
        };
        const double hi0 = tone(false, 4000.0), hi1 = tone(true, 4000.0), lo0 = tone(false, 100.0), lo1 = tone(true, 100.0);
        std::printf("  BODY: 4 kHz vs 500 Hz %.1f -> %.1f dB, 100 Hz vs 500 Hz %.1f -> %.1f dB (measured -4.3 / -4)\n", hi0, hi1, lo0, lo1);
        check(std::abs((hi1 - hi0) + 4.3) < 1.0 && std::abs((lo1 - lo0) + 4.0) < 1.0, "BODY applies the measured VG-8 BOWED tone curve");
    }

    // 29) living voice: STRING, LIFE, voice RES; SMEAR THRESH
    std::printf("Test 29: STRING, LIFE, voice resonance, SMEAR THRESH\n");
    {
        // a note whose upper harmonics die away faster than its fundamental (like a real string)
        std::vector<float> x((size_t) at(2.5));
        for (size_t i = at(0.1); i < x.size(); ++i)
        {
            const double t = (double) i / kSr - 0.1;
            double v = 0;
            for (int h = 1; h <= 8; ++h) v += 0.1 / h * std::exp(-t * (0.3 + 1.6 * (h - 1))) * std::sin(2 * kPi * 220.0 * h * t);
            x[i] = (float) v;
        }
        auto voice = [&](const Params& p)
        {
            auto e = std::make_unique<SwelloEngine>(); e->prepare(kSr); e->setParams(p);
            std::vector<float> V(x.size()); float dry = 0.f, r = 0.f, vr = 0.f;
            for (size_t i = 0; i < x.size(); ++i) e->processStereo(x[i], dry, r, &V[i], &vr);
            return V;
        };
        Params base; base.attackMs = 10.f; base.voiceMix = 1.f; base.voiceWave = 1;
        auto bright = [&](const std::vector<float>& V, double t)   // 4th harmonic vs fundamental, dB
        { const int a = at(t) + kLatencySamples; return 20.0 * std::log10(amp(V, a, at(0.15), 880.0) / amp(V, a, at(0.15), 220.0)); };
        Params st = base; st.voiceString = 1.f;
        const auto V0 = voice(base), Vs = voice(st);
        const double d0 = bright(V0, 0.3) - bright(V0, 1.6), ds = bright(Vs, 0.3) - bright(Vs, 1.6);
        std::printf("  STRING: the voice's 4th harmonic falls %.1f dB from 0.3 s to 1.6 s (fixed wave: %.1f dB)\n", ds, d0);
        check(ds > d0 + 6.0, "STRING: the voice darkens as the string decays, like the guitar note");

        // LIFE: a held note wanders a little in pitch
        auto zcSpread = [&](const std::vector<float>& V)
        {
            double lo = 1e9, hi = 0;
            for (double t = 0.4; t < 2.2; t += 0.05)
            {
                const int a = at(t) + kLatencySamples, b = a + at(0.05);
                int zc = 0; double first = -1, last = -1;
                for (int i = a + 1; i < b; ++i)
                    if (V[(size_t) i - 1] < 0 && V[(size_t) i] >= 0)
                    { const double fr = (i - 1) + V[(size_t) i - 1] / (V[(size_t) i - 1] - V[(size_t) i]); if (first < 0) first = fr; last = fr; ++zc; }
                if (zc < 3) continue;
                const double f = (zc - 1) * kSr / (last - first);
                lo = std::min(lo, f); hi = std::max(hi, f);
            }
            return 1200.0 * std::log2(hi / lo);
        };
        Params lf = base; lf.voiceWave = 5; lf.voiceToneHz = 400.f; lf.voiceLife = 1.f;
        Params lf0 = lf; lf0.voiceLife = 0.f;
        const double cl = zcSpread(voice(lf)), c0 = zcSpread(voice(lf0));
        std::printf("  LIFE: pitch wanders over %.1f cents (off: %.1f)\n", cl, c0);
        check(cl > c0 + 3.0 && cl < 30.0, "LIFE: each note wanders a little on its own");

        // RES: a peak at TONE
        Params rs = base; rs.voiceToneHz = 880.f; rs.voiceRes = 1.f;
        Params rs0 = rs; rs0.voiceRes = 0.f;
        const auto Vr = voice(rs), Vr0 = voice(rs0);
        const int a0 = at(0.4) + kLatencySamples;
        const double pk = amp(Vr, a0, at(0.2), 880.0) / amp(Vr, a0, at(0.2), 220.0), pk0 = amp(Vr0, a0, at(0.2), 880.0) / amp(Vr0, a0, at(0.2), 220.0);
        std::printf("  RES: harmonic at TONE vs fundamental x%.1f with RES (x%.2f without)\n", pk, pk0);
        check(pk > 3.0 * pk0, "voice RES: a resonant peak at TONE");

        // SMEAR THRESH: soft playing stays out of the reverb, loud playing goes in
        auto rev = [&](float level, float thr)
        {
            Smear sm; sm.prepare(kSr); Smear::Settings s; s.threshDb = thr;
            double e = 0;
            for (int i = 0; i < at(1.0); ++i)
            {
                const float v = level * (float) std::sin(2 * kPi * 330.0 * i / kSr);
                float l, r; sm.process(v, v, v, s, l, r);
                if (i > at(0.5)) e += l * l;
            }
            return std::sqrt(e / at(0.5)) / level;
        };
        const double soft = rev(0.01f, -20.f), loud = rev(0.3f, -20.f), softOpen = rev(0.01f, -80.f);
        std::printf("  THRESH -20 dB: soft note reverb %.4f (no thresh %.4f), loud note %.4f (per unit input)\n", soft, softOpen, loud);
        check(soft < 0.05 * softOpen && loud > 0.5 * softOpen, "THRESH: only playing above the threshold feeds the reverb");
    }

    // 30) MIDI out: notes from the tracked pitches, timing after the pick, offs, MPE bends
    std::printf("Test 30: MIDI out\n");
    {
        std::vector<float> x((size_t) at(2.4));
        addPluck(x, at(0.1), 0.15, 0.5, 220.0);    // A3 = 57
        addPluck(x, at(1.2), 0.15, 0.5, 329.63);   // E4 = 64
        auto runMidi = [&](int mode, std::vector<SwelloEngine::MidiEv>& evs)
        {
            auto e = std::make_unique<SwelloEngine>(); e->prepare(kSr);
            Params p; p.attackMs = 300.f; p.midiOut = mode; e->setParams(p);
            float d, r;
            SwelloEngine::MidiEv buf[128];
            for (size_t i = 0; i < x.size(); ++i)
            {
                e->processStereo(x[i], d, r);
                const int n = e->popMidi(buf, 128);
                for (int k = 0; k < n; ++k) evs.push_back(buf[k]);
            }
        };
        std::vector<SwelloEngine::MidiEv> ev;
        runMidi(1, ev);
        double on1 = -1, on2 = -1, off1 = -1; int n1 = -1, n2 = -1, ons = 0;
        for (auto& m : ev)
        {
            if (m.type == 0) { ++ons; if (on1 < 0) { on1 = (double) m.sample / kSr; n1 = m.note; } else if (on2 < 0) { on2 = (double) m.sample / kSr; n2 = m.note; } }
            if (m.type == 1 && m.note == n1 && off1 < 0) off1 = (double) m.sample / kSr;
        }
        std::printf("  notes: %d at %.0f ms after its pick, %d at %.0f ms after its pick; %d note-ons in all; first off at %.2f s\n",
                    n1, (on1 - 0.1) * 1000.0, n2, (on2 - 1.2) * 1000.0, ons, off1);
        check(n1 == 57 && n2 == 64, "MIDI: the played notes come out (A3 = 57, E4 = 64)");
        check(on1 - 0.1 < 0.07 && on2 - 1.2 < 0.07, "MIDI: a note-on within 70 ms of the pick");
        check(ons == 2 && off1 > 0.1, "MIDI: no false notes, and the first note is switched off");

        std::vector<SwelloEngine::MidiEv> mpe;
        runMidi(2, mpe);
        int bends = 0, chans = 0; for (auto& m : mpe) { if (m.type == 2) ++bends; if (m.type == 0 && m.ch >= 2) ++chans; }
        std::printf("  MPE: %d note-ons on their own channels, %d bend messages\n", chans, bends);
        check(chans == 2 && bends >= 2, "MPE: each note on its own channel with its own bend");

        // a 2-semitone string bend (220 -> 247 Hz over 0.3 s): one note, bent - not a string of new notes
        std::vector<float> y((size_t) at(2.0));
        double ph = 0.0;
        for (size_t i = at(0.1); i < y.size(); ++i)
        {
            const double t = (double) i / kSr;
            const double f = t < 0.7 ? 220.0 : t < 1.0 ? 220.0 * std::pow(2.0, 2.0 / 12.0 * (t - 0.7) / 0.3) : 220.0 * std::pow(2.0, 2.0 / 12.0);
            ph += 2 * kPi * f / kSr;
            double v = 0; for (int h = 1; h <= 5; ++h) v += 0.1 / h * std::sin(h * ph);
            y[i] = (float) (v * std::exp(-(t - 0.1) / 2.0));
        }
        auto e = std::make_unique<SwelloEngine>(); e->prepare(kSr);
        Params p; p.attackMs = 300.f; p.midiOut = 2; e->setParams(p);
        float d, rr; SwelloEngine::MidiEv buf[128];
        int bendOns = 0, maxBend = 0;
        for (size_t i = 0; i < y.size(); ++i)
        {
            e->processStereo(y[i], d, rr);
            const int nE = e->popMidi(buf, 128);
            for (int k = 0; k < nE; ++k) { if (buf[k].type == 0) ++bendOns; if (buf[k].type == 2) maxBend = std::max(maxBend, buf[k].bend); }
        }
        const double semis = (maxBend - 8192) / 8192.0 * 48.0;
        std::printf("  bend: %d note-on(s), the bend reaches %+.2f semitones\n", bendOns, semis);
        check(bendOns == 1 && semis > 1.7 && semis < 2.3, "MPE: a string bend is one note with its bend (follows ~2 semitones)");
    }

    // 31) DRIVE: tape (odd, smooth) and tube (adds the 2nd harmonic), level kept, stable when pushed
    std::printf("Test 31: DRIVE (tape / tube)\n");
    {
        auto drive = [&](int type, float amt, float level, std::vector<float>& out)
        {
            Drive dv; dv.prepare(kSr); Drive::Settings s; s.type = type; s.drive = amt; s.tone = 1.f; s.wow = 0.f;
            out.assign((size_t) at(1.0), 0.f);
            for (size_t i = 0; i < out.size(); ++i)
            {
                float l = level * (float) std::sin(2 * kPi * 220.0 * i / kSr), r = l;
                dv.process(l, r, s); out[i] = l;
            }
        };
        std::vector<float> t, u, big;
        drive(0, 0.6f, 0.3f, t); drive(1, 0.6f, 0.3f, u); drive(0, 1.f, 1.0f, big);
        const int a0 = at(0.5), n = at(0.4);
        const double inRms = 0.3 / std::sqrt(2.0), tRms = std::sqrt(energy(t, a0, n) / n), uRms = std::sqrt(energy(u, a0, n) / n);
        const double t2 = amp(t, a0, n, 440.0) / amp(t, a0, n, 220.0), t3 = amp(t, a0, n, 660.0) / amp(t, a0, n, 220.0);
        const double u2 = amp(u, a0, n, 440.0) / amp(u, a0, n, 220.0);
        bool finite = true; for (float v : big) finite = finite && std::isfinite(v) && std::abs(v) < 4.f;
        std::printf("  tape: level x%.2f, 2nd %.3f, 3rd %.3f; tube: level x%.2f, 2nd %.3f\n", tRms / inRms, t2, t3, uRms / inRms, u2);
        check(tRms / inRms > 0.5 && tRms / inRms < 1.6 && uRms / inRms > 0.5 && uRms / inRms < 1.6, "DRIVE keeps the level roughly steady");
        check(t3 > 0.01 && t2 < 0.3 * t3, "TAPE: smooth, mostly odd harmonics");
        check(u2 > 0.02, "TUBE: adds the warm 2nd harmonic");
        check(finite, "DRIVE stays stable when pushed hard");
    }

    // 32) violin tools: GLIDE, MONO, BOW wave, human vibrato
    std::printf("Test 32: GLIDE, MONO, BOW wave, human vibrato\n");
    {
        auto zcF = [](const std::vector<float>& v, int a, int b)
        {
            int zc = 0; double first = -1, last = -1;
            for (int i = a + 1; i < b; ++i)
                if (v[(size_t) i - 1] < 0 && v[(size_t) i] >= 0)
                { const double fr = (i - 1) + v[(size_t) i - 1] / (v[(size_t) i - 1] - v[(size_t) i]); if (first < 0) first = fr; last = fr; ++zc; }
            return zc > 2 ? (zc - 1) * kSr / (last - first) : 0.0;
        };
        auto voice = [&](const std::vector<float>& x, const Params& p)
        {
            auto e = std::make_unique<SwelloEngine>(); e->prepare(kSr); e->setParams(p);
            std::vector<float> V(x.size()); float d = 0.f, r = 0.f, vr = 0.f;
            for (size_t i = 0; i < x.size(); ++i) e->processStereo(x[i], d, r, &V[i], &vr);
            return V;
        };
        // two notes, the second (C#4-ish 277 Hz) while the first (220 Hz) still rings
        std::vector<float> x((size_t) at(2.0));
        addPluck(x, at(0.1), 0.15, 1.5, 220.0);
        addPluck(x, at(0.8), 0.15, 1.5, 277.18);
        Params base; base.attackMs = 10.f; base.voiceMix = 1.f; base.voiceWave = 5; base.voiceToneHz = 400.f; base.voiceMono = true;
        Params gl = base; gl.voiceGlide = 0.25f;
        const auto Vg = voice(x, gl), V0 = voice(x, base);
        // where the second note's voice first appears: frequency in its first 30 ms and later
        auto firstAt = [&](const std::vector<float>& V)
        {
            for (int i = at(0.8) + kLatencySamples; i < (int) V.size() - at(0.03); i += at(0.005))
            { const double f = zcF(V, i, i + at(0.03)); if (f > 250.0) return i; }
            return at(0.8) + kLatencySamples;
        };
        const int ig = firstAt(Vg);
        const double gEarly = zcF(Vg, ig - at(0.02), ig + at(0.02)), gLate = zcF(Vg, at(1.5) + kLatencySamples, at(1.7) + kLatencySamples);
        std::printf("  GLIDE: around the new note's entry %.0f Hz (from 220 towards 277), later %.1f Hz\n", gEarly, gLate);
        check(gEarly > 225.0 && gEarly < 272.0 && std::abs(gLate - 277.18) < 3.0, "GLIDE: the new note slides from the previous one, then settles");
        // MONO: once the second note is in, the first (220 Hz) is gone from the voice
        const int a1 = at(1.4) + kLatencySamples, n1 = at(0.2);
        const double old = amp(V0, a1, n1, 220.0) / amp(V0, a1, n1, 277.18);
        Params poly = base; poly.voiceMono = false;
        const auto Vp = voice(x, poly);
        const double oldPoly = amp(Vp, a1, n1, 220.0) / amp(Vp, a1, n1, 277.18);
        std::printf("  MONO: the old note at %.3f of the new one (poly: %.3f)\n", old, oldPoly);
        check(old < 0.2 * oldPoly, "MONO: only the newest note sounds");

        // BOW wave: the bow-position notch (7th harmonic weak), rosin noise between the harmonics
        std::vector<float> y((size_t) at(1.2));
        for (size_t i = at(0.1); i < y.size(); ++i) { double v = 0; for (int h = 1; h <= 6; ++h) v += 0.08 / h * std::sin(2 * kPi * 220.0 * h * i / kSr); y[i] = (float) v; }
        Params bw; bw.attackMs = 10.f; bw.voiceMix = 1.f; bw.voiceWave = 8; bw.voiceToneHz = 12000.f;
        Params sw = bw; sw.voiceWave = 1;
        const auto Vb = voice(y, bw), Vs = voice(y, sw);
        const int a0 = at(0.7) + kLatencySamples, n = at(0.3);
        const double b7 = amp(Vb, a0, n, 1540.0) / amp(Vb, a0, n, 1320.0), s7 = amp(Vs, a0, n, 1540.0) / amp(Vs, a0, n, 1320.0);
        const double bn = amp(Vb, a0, n, 3410.0), sn = amp(Vs, a0, n, 3410.0);
        std::printf("  BOW: 7th / 6th harmonic %.2f (saw %.2f); noise between harmonics x%.0f\n", b7, s7, bn / std::max(sn, 1e-9));
        check(b7 < 0.6 * s7 && bn > 3.0 * sn, "BOW: bowed-string spectrum with rosin noise");

        // human vibrato: shallow just after the delay, full later (a steady vibrato is full almost at once)
        std::vector<float> z((size_t) at(2.6));
        for (size_t i = at(0.1); i < z.size(); ++i) z[i] = 0.2f * (float) std::sin(2 * kPi * 330.0 * i / kSr);
        Params vh; vh.attackMs = 10.f; vh.voiceMix = 1.f; vh.voiceWave = 5; vh.voiceToneHz = 400.f;
        vh.vibCents = 40.f; vh.vibRateHz = 5.f; vh.vibDelaySec = 0.3f; vh.vibHuman = true;
        Params vs = vh; vs.vibHuman = false;
        auto spread = [&](const std::vector<float>& V, double t0, double t1)
        {
            double lo = 1e9, hi = 0;
            for (double t = t0; t < t1; t += 0.01) { const double f = zcF(V, at(t) + kLatencySamples, at(t + 0.03) + kLatencySamples); if (f > 0) { lo = std::min(lo, f); hi = std::max(hi, f); } }
            return hi - lo;
        };
        const auto Vh = voice(z, vh), Vst = voice(z, vs);
        const double hE = spread(Vh, 0.75, 0.95), hL = spread(Vh, 1.9, 2.3), sE = spread(Vst, 0.75, 0.95);
        std::printf("  human vibrato: %.1f Hz wide at 0.75-0.95 s, %.1f Hz at 1.9-2.3 s (steady: %.1f Hz early)\n", hE, hL, sE);
        check(hE < 0.75 * hL && hE < 0.8 * sE, "HUMAN vibrato deepens gradually");
    }

    std::printf("Test 33: HOLD freezes the ringing sound into a pad\n");
    {
        auto rmsOf = [](const std::vector<float>& v, double t0, double t1)
        { double e = 0; int n = 0; for (int i = (int) (t0 * kSr); i < (int) (t1 * kSr); ++i, ++n) e += (double) v[(size_t) i] * v[(size_t) i]; return std::sqrt(e / std::max(1, n)); };
        Hold h; h.prepare(kSr);
        const int total = (int) (5.0 * kSr);
        std::vector<float> in((size_t) total, 0.f), out((size_t) total, 0.f);
        for (int i = 0; i < (int) (1.0 * kSr); ++i)    // an A major chord that stops dead at 1 s
            in[(size_t) i] = 0.1f * (float) (std::sin(2 * kPi * 220.0 * i / kSr) + std::sin(2 * kPi * 277.2 * i / kSr) + std::sin(2 * kPi * 329.6 * i / kSr));
        for (int i = 0; i < total; ++i)
        {
            if (i == (int) (0.6 * kSr)) h.setHold(true);
            if (i == (int) (3.0 * kSr)) h.setHold(false);
            float l, r; h.process(in[(size_t) i], in[(size_t) i], 0.2f, l, r);
            out[(size_t) i] = l;
        }
        const double src = rmsOf(in, 0.3, 0.6), before = rmsOf(out, 0.1, 0.55), held = rmsOf(out, 1.5, 2.9), after = rmsOf(out, 3.6, 4.9);
        const double a220 = amp(out, (int) (1.5 * kSr), (int) (1.0 * kSr), 220.0), aOff = amp(out, (int) (1.5 * kSr), (int) (1.0 * kSr), 400.0);
        std::printf("  source %.4f, before the press %.5f, held (input silent) %.4f = %+.1f dB, after release %.5f; 220 Hz / 400 Hz (between notes) %.0fx\n",
                    src, before, held, 20 * std::log10(held / src), after, a220 / std::max(aOff, 1e-9));
        check(before < 1e-6, "HOLD: silent until pressed");
        check(std::abs(20 * std::log10(held / src)) < 3.0, "HOLD: the pad keeps the level of what was ringing");
        check(a220 > 8.0 * aOff, "HOLD: the pad keeps the notes (no smear between them)");
        check(after < 1e-5, "HOLD: fades out on release");
        // steady: the pad must not warble (a chord in 100 ms slices; the first version, random phases, was garbled)
        double lo = 1e9, hi = 0;
        for (double t = 1.5; t < 2.8; t += 0.1) { const double r = rmsOf(out, t, t + 0.1); lo = std::min(lo, r); hi = std::max(hi, r); }
        std::printf("  held pad, loudest / quietest 100 ms: %.2f\n", hi / lo);
        check(hi / lo < 1.15, "HOLD: the pad is steady (no warble)");
    }

    std::printf("Test 34: level-matched swell\n");
    {
        // a plucked note that decays 8 dB/s, swelled over 1 s: without MATCH the swell tops out about 8 dB under the
        // pick, with MATCH at about the pick's level; a steady note is left alone
        const int n = (int) (3.0 * kSr), t0 = (int) (0.2 * kSr);
        std::vector<float> pluck((size_t) n, 0.f), steady((size_t) n, 0.f);
        for (int i = t0; i < n; ++i)
        {
            const double t = (i - t0) / kSr;
            pluck[(size_t) i]  = 0.3f * (float) (std::sin(2 * kPi * 330.0 * i / kSr) * std::pow(10.0, -8.0 * t / 20.0));
            steady[(size_t) i] = 0.3f * (float) std::sin(2 * kPi * 330.0 * i / kSr);
        }
        Params off; off.attackMs = 1000.f;
        Params on = off; on.levelMatch = true;
        auto peakDb = [&](const std::vector<float>& x, const Params& p)
        {
            const auto o = run(x, p);
            double best = 0;
            for (double t = 0.3; t < 2.6; t += 0.02) best = std::max(best, amp(o.wet, t0 + (int) (t * kSr) + kLatencySamples, (int) (0.1 * kSr), 330.0));
            return 20 * std::log10(best / 0.3);
        };
        const double pOff = peakDb(pluck, off), pOn = peakDb(pluck, on), sOff = peakDb(steady, off), sOn = peakDb(steady, on);
        std::printf("  plucked note, loudest point of the swell vs the pick: %.1f dB, matched %.1f dB; steady note %.1f / %.1f dB\n", pOff, pOn, sOff, sOn);
        check(pOff < -5.0, "without MATCH a 1 s swell ends well under the pick's level");
        check(pOn > -2.5 && pOn < 1.0, "MATCH: the swell ends about as loud as the pick, never louder");
        check(std::abs(sOn - sOff) < 0.5, "MATCH leaves a note that does not decay alone");

        // a note whose upper partials fade fast: MATCH brings the note back up, not its 6th harmonic far above it
        std::vector<float> rich((size_t) n, 0.f);
        for (int i = t0; i < n; ++i)
        {
            const double t = (i - t0) / kSr;
            rich[(size_t) i] = (float) (0.3 * std::sin(2 * kPi * 220.0 * i / kSr) * std::pow(10.0, -6.0 * t / 20.0)
                                      + 0.1 * std::sin(2 * kPi * 1320.0 * i / kSr) * std::pow(10.0, -24.0 * t / 20.0));
        }
        const auto rOff = run(rich, off), rOn = run(rich, on);
        const int tp = t0 + at(1.05) + kLatencySamples, w = at(0.1);
        const double liftF = 20 * std::log10(amp(rOn.wet, tp, w, 220.0) / amp(rOff.wet, tp, w, 220.0));
        const double liftH = 20 * std::log10(amp(rOn.wet, tp, w, 1320.0) / amp(rOff.wet, tp, w, 1320.0));
        std::printf("  rich note at the end of a 1 s swell: MATCH lifts the fundamental %+.1f dB, the fast-fading 6th harmonic %+.1f dB\n", liftF, liftH);
        check(std::abs(liftH - liftF) < 2.0, "MATCH lifts the whole note alike (the upper partials are not pushed above it)");
    }

    std::printf("Test 35: feedback, amp model (DISTANCE picks the harmonic, it takes over, one note wins, muting kills it)\n");
    {
        const int n = at(4.0);
        std::vector<float> one((size_t) n), two((size_t) n), muted((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / kSr, d = std::pow(10.0, -6.0 * t / 20.0);
            one[(size_t) i] = (float) (0.25 * d * std::sin(2 * kPi * 220.0 * t));
            two[(size_t) i] = (float) (0.25 * d * std::sin(2 * kPi * 220.0 * t) + 0.10 * d * std::sin(2 * kPi * 311.1 * t));
            muted[(size_t) i] = t < 2.5 ? one[(size_t) i] : 0.f;
        }
        const int lat = kLatencySamples, w = 4800;
        auto P = [](float distance, bool amp, bool wins) { Params p; p.attackMs = 100.f; p.feedback = 1.f; p.fbRiseSec = 1.0f; p.fbAmp = amp; p.fbDistance = distance; p.fbOneWins = wins; return p; };
        const Out classic = run(one, P(0.25f, false, true)), oct = run(one, P(0.25f, true, true)), twelfth = run(one, P(0.5f, true, true));
        const double cF = amp(classic.wet, at(3.0) + lat, w, 220.0), aF = amp(oct.wet, at(3.0) + lat, w, 220.0), aO = amp(oct.wet, at(3.0) + lat, w, 440.0);
        const double t12 = amp(twelfth.wet, at(3.0) + lat, w, 660.0), tO = amp(twelfth.wet, at(3.0) + lat, w, 440.0);
        std::printf("  DISTANCE .25: 220 Hz %.4f (classic %.4f), 440 Hz %.4f | DISTANCE .5: 660 Hz %.4f, 440 Hz %.4f\n", aF, cF, aO, t12, tO);
        check(aO > 1.2 * aF && aF < 0.6 * cF, "the octave takes over from the note");
        check(t12 > 5.0 * tO, "further away the 12th wins instead of the octave");
        const Out all = run(two, P(0.25f, true, false)), win = run(two, P(0.25f, true, true));
        const double allR = amp(all.wet, at(3.0) + lat, w, 622.2) / amp(all.wet, at(3.0) + lat, w, 440.0);
        const double winR = amp(win.wet, at(3.0) + lat, w, 622.2) / amp(win.wet, at(3.0) + lat, w, 440.0);
        std::printf("  two notes: quiet note's overtone / loud note's overtone %.2f, with ONE WINS %.2f\n", allR, winR);
        check(winR < 0.6 * allR, "one note wins: the quieter note's feedback is held back");
        const Out mu = run(muted, P(0.25f, true, true));
        double e = 0; for (int i = at(2.75) + lat; i < at(3.25) + lat; ++i) e += (double) mu.wet[(size_t) i] * mu.wet[(size_t) i];
        const double rms = std::sqrt(e / (double) at(0.5));
        std::printf("  muted at 2.5 s: level 0.25..0.75 s later %.5f\n", rms);
        check(rms < 0.002, "muting the string kills the feedback");
    }

    std::printf("%s\n", failures ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return failures ? 1 : 0;
}
