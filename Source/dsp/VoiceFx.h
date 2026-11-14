#pragma once
// Effects for the VOICE layer only (the guitar stays clean): UNISON (detuned copies spread across the
// stereo field, with DRIFT = slow random pitch wander), an 80s polysynth-style CHORUS (I, II, I+II) and a
// PHASER. No JUCE dependency, no latency on the main voice (only the extra copies are delayed).
#include "Detuner.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace swello
{
class VoiceFx
{
public:
    struct Settings
    {
        float unison  = 0.f;   // 0 .. 1: number and width of the detuned copies
        float detune  = 15.f;  // cents between the outermost copies and the centre
        float drift   = 0.f;   // 0 .. 1: slow random pitch wander (warbly, VG-like)
        int   chorus  = 0;     // 0 off, 1 I, 2 II, 3 I+II
        float phaser  = 0.f;   // 0 .. 1: phaser depth/mix
        bool  supersaw = false; // supersaw wave: at least a wide unison
    };
    static constexpr int kMaxCopies = 6;

    void prepare(double sampleRate)
    {
        sr = (float) sampleRate;
        for (auto& d : copies) d.prepare(sampleRate);
        const int n = nextPow2((int) (0.05f * sr) + 4);
        chBuf.assign((size_t) n, 0.f); chMask = n - 1;
        wowBuf.assign((size_t) nextPow2((int) (0.01f * sr) + 4), 0.f); wowMask = (int) wowBuf.size() - 1;
        reset();
    }

    void reset()
    {
        for (auto& d : copies) d.reset();
        std::fill(chBuf.begin(), chBuf.end(), 0.f); std::fill(wowBuf.begin(), wowBuf.end(), 0.f);
        chW = wowW = 0; chPhase = 0.f; phPhase = 0.f;
        for (auto& s : apL) s = 0.f;
        for (auto& s : apR) s = 0.f;
        fbL = fbR = 0.f;
        for (int i = 0; i <= kMaxCopies; ++i) { wander[(size_t) i] = 0.f; wanderTarget[(size_t) i] = 0.f; }
        wanderCount = 0;
    }

    void process(float& l, float& r, const Settings& s)
    {
        // ---- drift: smooth random walk per voice (new target every ~0.6 s)
        if (s.drift > 0.f || s.unison > 0.f || s.supersaw)
        {
            if (--wanderCount <= 0)
            {
                wanderCount = (int) (0.6f * sr);
                for (auto& t : wanderTarget) t = rand01() * 2.f - 1.f;
            }
            const float k = 1.f / (0.5f * sr);
            for (int i = 0; i <= kMaxCopies; ++i) wander[(size_t) i] += (wanderTarget[(size_t) i] - wander[(size_t) i]) * k;
        }

        // ---- centre voice: drift as a gentle wow from a short modulated delay (no added latency on average)
        float cl = l, cr = r;
        if (s.drift > 0.f)
        {
            const float mono = 0.5f * (l + r);
            wowBuf[(size_t) wowW] = mono;
            const float d = (1.0f + 0.9f * s.drift * wander[0]) * 0.001f * sr;   // 0.1 .. 1.9 ms
            const float w = readFrac(wowBuf, wowMask, (float) wowW - d);
            wowW = (wowW + 1) & wowMask;
            cl = l + (w - mono); cr = r + (w - mono);
        }

        // ---- unison copies: detuned, alternately left / right
        const float u = s.supersaw ? std::max(s.unison, 0.6f) : s.unison;
        float ul = cl, ur = cr;
        if (u > 0.f)
        {
            const int n = std::clamp((int) std::lround(u * kMaxCopies), 1, kMaxCopies);
            const float mono = 0.5f * (l + r);
            float addL = 0.f, addR = 0.f;
            for (int i = 0; i < n; ++i)
            {
                const int   pair  = i / 2 + 1, pairs = (n + 1) / 2;
                const float side  = (i % 2 == 0) ? 1.f : -1.f;
                const float cents = side * s.detune * (float) pair / (float) pairs + s.drift * 9.f * wander[(size_t) i + 1];
                const float y = copies[(size_t) i].process(mono, std::exp2(cents / 1200.f));
                const float pan = side * std::min(1.f, 0.4f + 0.6f * u);       // wider with more unison
                addL += y * std::sqrt(0.5f * (1.f - pan));
                addR += y * std::sqrt(0.5f * (1.f + pan));
            }
            const float g = 1.f / std::sqrt(1.f + 0.6f * (float) n);
            ul = (cl + addL * 1.2f) * g * 1.25f;
            ur = (cr + addR * 1.2f) * g * 1.25f;
        }

        // ---- chorus: modulated delay on the mid signal, left and right taps in opposite phase
        if (s.chorus > 0)
        {
            static const float rate[]  = { 0.f, 0.513f, 0.863f, 9.75f };
            static const float depth[] = { 0.f, 1.85f, 1.85f, 0.30f };   // ms
            static const float base[]  = { 0.f, 3.5f, 3.5f, 3.2f };      // ms
            const float mono = 0.5f * (ul + ur);
            chBuf[(size_t) chW] = mono;
            chPhase += rate[s.chorus] / sr; if (chPhase >= 1.f) chPhase -= 1.f;
            const float tri = 4.f * std::abs(chPhase - 0.5f) - 1.f;          // -1 .. 1
            const float dL = (base[s.chorus] + depth[s.chorus] * tri) * 0.001f * sr;
            const float dR = (base[s.chorus] - depth[s.chorus] * tri) * 0.001f * sr;
            const float wl = readFrac(chBuf, chMask, (float) chW - dL), wr = readFrac(chBuf, chMask, (float) chW - dR);
            chW = (chW + 1) & chMask;
            ul = 0.75f * ul + 0.7f * wl;
            ur = 0.75f * ur + 0.7f * wr;
        }

        // ---- phaser: 6 all-pass stages per side, swept 250 Hz .. 2.5 kHz, a little feedback
        if (s.phaser > 0.f)
        {
            phPhase += 0.22f / sr; if (phPhase >= 1.f) phPhase -= 1.f;
            const float lfoL = 0.5f + 0.5f * std::sin(6.2831853f * phPhase);
            const float lfoR = 0.5f + 0.5f * std::sin(6.2831853f * (phPhase + 0.25f));
            ul = phase(ul, lfoL, apL, fbL, s.phaser, coefL, cntL);
            ur = phase(ur, lfoR, apR, fbR, s.phaser, coefR, cntR);
        }
        l = ul; r = ur;
    }

private:
    static int nextPow2(int n) { int p = 1; while (p < n) p <<= 1; return p; }
    static float readFrac(const std::vector<float>& b, int mask, float pos)
    {
        const float fl = std::floor(pos);
        const int i0 = (int) fl; const float fr = pos - fl;
        const float a = b[(size_t) (i0 & mask)], c = b[(size_t) ((i0 + 1) & mask)];
        return a + (c - a) * fr;
    }
    float rand01() { rng = rng * 1664525u + 1013904223u; return (float) (rng >> 8) / 16777216.f; }

    float phase(float x, float lfo, std::array<float, 6>& st, float& fb, float amount, float& coef, int& countdown)
    {
        if (--countdown <= 0)                                                // the sweep is slow: update every 16 samples
        {
            countdown = 16;
            const float fc = 250.f * std::pow(10.f, lfo);                   // 250 .. 2500 Hz
            const float t = std::tan(3.14159265f * std::min(fc / sr, 0.45f));
            coef = (1.f - t) / (1.f + t);
        }
        const float a = coef;
        float y = x + 0.5f * fb;
        for (auto& z : st) { const float o = -a * y + z; z = y + a * o; y = o; }
        fb = y;
        return x * (1.f - 0.5f * amount) + y * 0.5f * amount;
    }

    float sr = 48000.f;
    std::array<Detuner, kMaxCopies> copies;
    std::vector<float> chBuf, wowBuf;
    int chMask = 0, chW = 0, wowMask = 0, wowW = 0;
    float chPhase = 0.f, phPhase = 0.f, fbL = 0.f, fbR = 0.f, coefL = 0.f, coefR = 0.f;
    int cntL = 0, cntR = 0;
    std::array<float, 6> apL {}, apR {};
    std::array<float, kMaxCopies + 1> wander {}, wanderTarget {};
    int wanderCount = 0;
    uint32_t rng = 777u;
};
}
