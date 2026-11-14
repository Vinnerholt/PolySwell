#pragma once
// DRIVE: smooth saturation for the swelled sound (owner: "should be smooth"; it feeds amp sims, so warmth, not grit).
// TAPE (cassette / Portastudio style): pre-emphasis -> symmetric soft clip -> de-emphasis and a gentle top roll-off
// that darkens with drive, a small head bump, and a little wow / flutter. TUBE (triode preamp): asymmetric soft clip
// (even harmonics, more touch-sensitive), DC blocked. The clipper uses first-order antiderivative anti-aliasing
// (ADAA), so it stays clean without oversampling (cheap). No JUCE dependency, no latency (the wow delay is
// centred on ~1 ms and only on the wet path).
#include <algorithm>
#include <cmath>
#include <vector>

namespace swello
{
class Drive
{
public:
    struct Settings
    {
        int   type = 0;      // 0 tape, 1 tube
        float drive = 0.3f;  // 0 .. 1
        float tone = 0.5f;   // 0 dark .. 1 bright
        float wow = 0.2f;    // 0 .. 1 (tape only)
    };

    void prepare(double sampleRate)
    {
        sr = (float) sampleRate;
        dl.assign(512, 0.f); dr.assign(512, 0.f);
        reset();
    }
    void reset()
    {
        for (auto* c : { &chL, &chR }) *c = Chan {};
        std::fill(dl.begin(), dl.end(), 0.f); std::fill(dr.begin(), dr.end(), 0.f);
        w = 0; wowPh = 0.f; flPh = 0.f;
    }

    void process(float& l, float& r, const Settings& s)
    {
        const float k   = 1.f + 14.f * s.drive * s.drive;            // input gain into the clipper
        const float makeup = 1.f / std::pow(k, 0.62f);               // keep the level roughly steady
        const bool  tape = s.type == 0;
        // tone: post low-pass from ~2.5 kHz (dark) to ~16 kHz (bright); tape darkens a little more with drive
        const float lpHz = 2500.f * std::pow(6.4f, s.tone) * (tape ? (1.f - 0.25f * s.drive) : 1.f);
        const float lpA  = 1.f - std::exp(-2.f * 3.14159265f * lpHz / sr);
        const float emA  = 1.f - std::exp(-2.f * 3.14159265f * 2000.f / sr);   // emphasis split at ~2 kHz
        const float bumpA = 1.f - std::exp(-2.f * 3.14159265f * 90.f / sr);    // tape head bump region
        const float dcA  = 1.f - std::exp(-2.f * 3.14159265f * 12.f / sr);

        if (tape && s.wow > 0.001f)   // wow (~0.6 Hz) and flutter (~7 Hz) as a tiny modulated delay
        {
            wowPh += 0.6f / sr; if (wowPh >= 1.f) wowPh -= 1.f;
            flPh  += 7.1f / sr; if (flPh >= 1.f) flPh -= 1.f;
            const float d = (1.f + s.wow * (0.6f * std::sin(6.2831853f * wowPh) + 0.08f * std::sin(6.2831853f * flPh))) * 0.001f * sr;
            dl[(size_t) w] = l; dr[(size_t) w] = r;
            l = readFrac(dl, (float) w - d); r = readFrac(dr, (float) w - d);
            w = (w + 1) & 511;
        }
        l = chan(chL, l, k, makeup, tape, lpA, emA, bumpA, dcA);
        r = chan(chR, r, k, makeup, tape, lpA, emA, bumpA, dcA);
    }

private:
    struct Chan { float xPrev = 0.f, fPrev = 0.f, lp = 0.f, em = 0.f, de = 0.f, bump = 0.f, dc = 0.f, dcIn = 0.f; };

    // soft clip with first-order ADAA: y = (F(x) - F(x1)) / (x - x1), F = antiderivative of the curve
    static float logCosh(float x) { const float a = std::abs(x); return a + std::log1p(std::exp(-2.f * a)) - 0.69314718f; }
    static float clipF(float x, bool tape)          // antiderivative of the curve (log cosh, overflow-safe)
    {
        return tape ? logCosh(x) : logCosh(x + 0.25f);   // tube: tanh(x + 0.25), its DC removed later
    }
    static float clip(float x, bool tape) { return tape ? std::tanh(x) : std::tanh(x + 0.25f); }

    float chan(Chan& c, float x, float k, float makeup, bool tape, float lpA, float emA, float bumpA, float dcA)
    {
        // tape: pre-emphasis (highs pushed into the clipper a little harder) and a head bump below ~90 Hz
        float in = x;
        if (tape)
        {
            c.em += (in - c.em) * emA;                    // low part
            in = c.em + 1.6f * (in - c.em);               // highs +4 dB into the tape
            c.bump += (x - c.bump) * bumpA;
            in += 0.25f * c.bump;
        }
        const float u = in * k;
        float y;
        const float du = u - c.xPrev;
        const float F = clipF(u, tape);
        if (std::abs(du) > 1.0e-4f) y = (F - c.fPrev) / du;
        else y = clip(0.5f * (u + c.xPrev), tape);
        c.xPrev = u; c.fPrev = F;
        if (! tape)   // remove the DC the asymmetric curve makes (high-pass ~12 Hz)
        {
            c.dc += (y - c.dc) * dcA;
            y -= c.dc;
        }
        else          // de-emphasis: highs back down by the same amount
        {
            c.de += (y - c.de) * emA;
            y = c.de + (y - c.de) / 1.6f;
        }
        c.lp += (y - c.lp) * lpA;                          // tone
        return c.lp * makeup;
    }

    float readFrac(const std::vector<float>& b, float pos) const
    {
        const float fl = std::floor(pos);
        const int i0 = (int) fl; const float fr = pos - fl;
        const float a = b[(size_t) (i0 & 511)], c = b[(size_t) ((i0 + 1) & 511)];
        return a + (c - a) * fr;
    }

    float sr = 48000.f, wowPh = 0.f, flPh = 0.f;
    Chan chL, chR;
    std::vector<float> dl, dr;
    int w = 0;
};
}
