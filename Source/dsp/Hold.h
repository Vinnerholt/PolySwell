#pragma once
// HOLD (PolySwell II): freezes what is ringing into a pad that can be played over.
// A 2048-point spectral freeze: when HOLD is pressed, the magnitudes of the last 2048 samples are captured (per
// channel); while held, every hop resynthesises them with phases that advance at each bin's own frequency plus a small
// random jitter (so the pad shimmers slightly instead of buzzing like a looped frame). The frozen layer is ADDED to the
// live signal by the caller, so new notes still swell over it. It adds no latency (nothing passes through it).
#include "SwelloEngine.h"
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>

namespace swello
{
class Hold
{
public:
    static constexpr int kOrder = 11, kN = 1 << kOrder, kHop = kN / 4;

    void prepare(double sampleRate)
    {
        sr = sampleRate;
        for (int i = 0; i < kN; ++i) win[(size_t) i] = 0.5f - 0.5f * (float) std::cos(2.0 * 3.14159265358979323846 * i / kN);
        reset();
    }

    void reset()
    {
        for (auto& r : inRing) r.fill(0.f);
        for (auto& r : outRing) r.fill(0.f);
        for (auto& m : mag) m.fill(0.f);
        pos = 0; hopCount = 0; gain = 0.f; on = false; captured = false; wantCapture = false;
    }

    // press / release. A new press while the old pad is still fading captures the new sound.
    void setHold(bool shouldHold)
    {
        if (shouldHold && ! on) wantCapture = true;
        if (! shouldHold) wantCapture = false;
        on = shouldHold;
    }

    bool isOn() const { return on; }
    float level() const { return gain; }            // 0..1, the pad's fade (for the UI)

    // in: the live signal (always call it, so the last 2048 samples are known); out: the frozen layer (0 when idle)
    void process(float inL, float inR, float fadeSec, float& outL, float& outR)
    {
        inRing[0][(size_t) pos] = inL; inRing[1][(size_t) pos] = inR;
        outL = outRing[0][(size_t) pos] * gain; outR = outRing[1][(size_t) pos] * gain;
        outRing[0][(size_t) pos] = 0.f; outRing[1][(size_t) pos] = 0.f;
        pos = (pos + 1) & (kN - 1);

        const float step = 1.f / (float) (std::max(0.005f, fadeSec) * sr);
        // a new press over a pad that is still fading: fade the old one out quickly first, then capture
        const float target = (on && captured && ! wantCapture) ? 1.f : 0.f;
        if (gain < target) gain = std::min(target, gain + step);
        else if (gain > target) gain = std::max(target, gain - step * (wantCapture ? 4.f : 1.f));

        if (++hopCount >= kHop)
        {
            hopCount = 0;
            if (wantCapture && gain <= 0.f) { capture(); wantCapture = false; }
            if (captured && (on || gain > 0.f)) synthFrame();
        }
    }

private:
    using Cpx = std::complex<float>;

    void capture()
    {
        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < kN; ++i) buf[(size_t) i] = Cpx(inRing[(size_t) c][(size_t) ((pos + i) & (kN - 1))] * win[(size_t) i], 0.f);
            fft.forward(buf.data());
            for (int k = 0; k <= kN / 2; ++k) mag[(size_t) c][(size_t) k] = std::abs(buf[(size_t) k]);
        }
        for (int k = 0; k <= kN / 2; ++k) phase[(size_t) k] = rnd() * 6.2831853f;
        for (auto& r : outRing) r.fill(0.f);
        captured = true;
    }

    void synthFrame()
    {
        // each bin moves on by its own centre frequency (2 pi k hop / N = k pi / 2) plus a little jitter
        for (int k = 0; k <= kN / 2; ++k)
        {
            float ph = phase[(size_t) k] + 1.5707963f * (float) (k & 3) + (rnd() - 0.5f) * 0.3f;
            if (ph > 6.2831853f) ph -= 6.2831853f * std::floor(ph / 6.2831853f);
            phase[(size_t) k] = ph;
        }
        for (int c = 0; c < 2; ++c)
        {
            for (int k = 0; k <= kN / 2; ++k)
            {
                // the two channels share the phases (keeps the stereo picture), a small offset widens it slightly
                const float ph = phase[(size_t) k] + (c == 1 ? 0.25f * std::sin(0.37f * (float) k) : 0.f);
                buf[(size_t) k] = std::polar(mag[(size_t) c][(size_t) k], ph);
                if (k > 0 && k < kN / 2) buf[(size_t) (kN - k)] = std::conj(buf[(size_t) k]);
            }
            buf[0] = Cpx(buf[0].real(), 0.f); buf[(size_t) (kN / 2)] = Cpx(buf[(size_t) (kN / 2)].real(), 0.f);
            fft.inverse(buf.data());
            // synthesis window; Hann at 75 % overlap sums to 2, and the analysis window already shaped the frame
            for (int i = 0; i < kN; ++i)
                outRing[(size_t) c][(size_t) ((pos + i) & (kN - 1))] += buf[(size_t) i].real() * win[(size_t) i] * kNorm;
        }
    }

    float rnd()   // 0..1
    {
        seed = seed * 1664525u + 1013904223u;
        return (float) (seed >> 8) * (1.f / 16777216.f);
    }

    // random phases turn the frame into noise-like grains: the overlap-add of 4 such frames (each x Hann, the capture
    // already x Hann) comes out at about the captured level with this factor (test 33 checks it within 3 dB)
    static constexpr float kNorm = 0.86f;

    double sr = 48000.0;
    FftT<kOrder> fft;
    std::array<float, kN> win {};
    std::array<std::array<float, kN>, 2> inRing {}, outRing {};
    std::array<std::array<float, kN / 2 + 1>, 2> mag {};
    std::array<float, kN / 2 + 1> phase {};
    std::array<Cpx, kN> buf {};
    int pos = 0, hopCount = 0;
    float gain = 0.f;
    bool on = false, captured = false, wantCapture = false;
    uint32_t seed = 0x1234567u;
};
} // namespace swello
