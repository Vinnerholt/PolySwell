// Dev tool: CPU cost of the DSP, feature by feature (no JUCE). Build target engine_bench.
// Prints the average and the worst frame cost, as % of one core at 48 kHz, for a strummed chord.
#include "dsp/SwelloEngine.h"
#include "dsp/VoiceFx.h"
#include "dsp/Detuner.h"
#include <chrono>
#include <cstdio>
#include <functional>
#include <vector>

using namespace swello;
static constexpr double kSr = 48000.0, kPi = 3.14159265358979323846;

static std::vector<float> chord()
{
    std::vector<float> x((size_t) (kSr * 6.0), 0.f);
    const double f0[] = { 82.4, 123.5, 164.8, 207.7, 246.9, 329.6 };
    for (int rep = 0; rep < 3; ++rep)
        for (int s = 0; s < 6; ++s)
            for (size_t i = (size_t) ((rep * 2.0 + s * 0.012) * kSr); i < x.size(); ++i)
            {
                const double t = (double) i / kSr - (rep * 2.0 + s * 0.012);
                if (t > 2.0) break;
                double v = 0;
                for (int h = 1; h <= 8; ++h) v += 0.04 / h * std::sin (2 * kPi * f0[s] * h * t);
                x[i] += (float) (v * std::exp (-t * 1.2));
            }
    return x;
}

template <typename Engine>
static void bench (const char* name, const Params& p, const std::vector<float>& x)
{
    auto e = std::make_unique<Engine>();
    e->prepare (kSr); e->setParams (p);
    float dry = 0.f, r = 0.f, vl = 0.f, vr = 0.f;
    double worstBlockUs = 0.0;
    constexpr int block = 64;                                   // a small host buffer: where pops show up first
    const auto t0 = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < x.size(); i += block)
    {
        const auto b0 = std::chrono::high_resolution_clock::now();
        for (size_t j = i; j < std::min (x.size(), i + block); ++j) e->processStereo (x[j], dry, r, &vl, &vr);
        worstBlockUs = std::max (worstBlockUs, std::chrono::duration<double, std::micro> (std::chrono::high_resolution_clock::now() - b0).count());
    }
    const double sec = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
    const double audioSec = (double) x.size() / kSr, budgetUs = block / kSr * 1e6;
    std::printf ("  %-36s avg %5.2f %% of a core   worst 64-sample block %6.0f us = %5.1f %% of its %4.0f us budget\n",
                 name, 100.0 * sec / audioSec, worstBlockUs, 100.0 * worstBlockUs / budgetUs, budgetUs);
}

int main()
{
    const auto x = chord();
    std::printf ("Engine (1024-point), strummed 6-string chord, 48 kHz:\n");
    Params base; base.attackMs = 300.f;
    bench<SwelloEngine> ("swell only", base, x);
    { Params p = base; p.filterMode = 1; p.mod = 0.6f;              bench<SwelloEngine> ("+ filter with MOD sweep", p, x); }
    { Params p = base; p.sustain = 0.5f; p.touch = 0.5f; p.follow = 0.5f; bench<SwelloEngine> ("+ sustain, dynamics, pace", p, x); }
    { Params p = base; p.tremDepth = 0.5f;                          bench<SwelloEngine> ("+ tremolo", p, x); }
    { Params p = base; p.spread = 0.8f;                             bench<SwelloEngine> ("+ spread (alternate)", p, x); }
    { Params p = base; p.spread = 0.8f; p.spreadMode = 2;           bench<SwelloEngine> ("+ spread by pitch (pitch tracker)", p, x); }
    { Params p = base; p.feedback = 0.8f;                           bench<SwelloEngine> ("+ feedback", p, x); }
    { Params p = base; p.vibCents = 20.f; p.vibTarget = 1;          bench<SwelloEngine> ("+ guitar vibrato", p, x); }
    { Params p = base; p.voiceMix = 0.5f;                           bench<SwelloEngine> ("+ voice (saw)", p, x); }
    { Params p = base; p.voiceMix = 0.5f; p.filterMode = 1; p.spread = 0.8f; p.feedback = 0.8f; p.sustain = 0.5f; p.tremDepth = 0.5f;
      bench<SwelloEngine> ("everything on", p, x); }
    { Params p = base; p.voiceMix = 0.5f; p.filterMode = 1; p.spread = 0.8f; p.feedback = 0.8f; p.sustain = 0.5f; p.tremDepth = 0.5f;
      bench<SwelloEngineLL> ("everything on, LOW LATENCY engine", p, x); }

    std::printf ("Voice effects (per sample, stereo):\n");
    auto fx = [&] (const char* name, VoiceFx::Settings s)
    {
        VoiceFx v; v.prepare (kSr);
        const auto t0 = std::chrono::high_resolution_clock::now();
        float l, r;
        for (float s0 : x) { l = r = s0; v.process (l, r, s); }
        const double sec = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
        std::printf ("  %-36s avg %5.2f %% of a core\n", name, 100.0 * sec / ((double) x.size() / kSr));
    };
    { VoiceFx::Settings s; s.unison = 1.f; fx ("unison full (6 copies)", s); }
    { VoiceFx::Settings s; s.drift = 1.f; fx ("drift", s); }
    { VoiceFx::Settings s; s.chorus = 1; fx ("chorus", s); }
    { VoiceFx::Settings s; s.phaser = 1.f; fx ("phaser", s); }
    {
        Detuner a, b; a.prepare (kSr); b.prepare (kSr);
        const auto t0 = std::chrono::high_resolution_clock::now();
        float acc = 0.f;
        for (float s0 : x) acc += a.process (s0, 1.004f) + b.process (s0, 0.996f);
        const double sec = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
        std::printf ("  %-36s avg %5.2f %% of a core (%g)\n", "DETUNE (2 shifters)", 100.0 * sec / ((double) x.size() / kSr), (double) (acc * 0.f));
    }
    return 0;
}
