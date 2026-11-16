#pragma once
// HOLD (PolySwell II): freezes what is ringing into a pad that can be played over.
// A 4096-point spectral freeze. When HOLD is pressed, two overlapping frames of the last samples are analysed: the
// newer one gives each bin's magnitude and phase, the difference between the two gives how far each bin's phase moves
// per hop (its true frequency). While held, every hop resynthesises the same magnitudes with the phases moved on by
// that amount, so every partial simply keeps sounding at its own pitch, in step with its neighbouring bins.
// (The first version started from random phases and moved each bin at its centre frequency plus jitter: the bins of one
// partial then beat against each other and the pad came out garbled. Do not go back.)
// The frozen layer is ADDED to the live signal by the caller, so new notes still swell over it. No latency.
#include "SwelloEngine.h"
#include <array>
#include <cmath>
#include <complex>

namespace swello
{
class Hold
{
public:
    static constexpr int kOrder = 12, kN = 1 << kOrder, kHop = kN / 4;

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

    // in: the live signal (always call it, so the last samples are known); out: the frozen layer (0 when idle)
    void process(float inL, float inR, float fadeSec, float& outL, float& outR)
    {
        inRing[0][(size_t) pos] = inL; inRing[1][(size_t) pos] = inR;
        outL = outRing[0][(size_t) (pos & (kN - 1))] * gain; outR = outRing[1][(size_t) (pos & (kN - 1))] * gain;
        outRing[0][(size_t) (pos & (kN - 1))] = 0.f; outRing[1][(size_t) (pos & (kN - 1))] = 0.f;
        pos = (pos + 1) & (kRing - 1);

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
    static constexpr int kRing = kN * 2;      // input history: two frames one hop apart need kN + kHop samples

    // spectrum of the kN samples that end `back` samples before now
    void analyse(int c, int back)
    {
        for (int i = 0; i < kN; ++i)
            buf[(size_t) i] = Cpx(inRing[(size_t) c][(size_t) ((pos - back - kN + i + 2 * kRing) & (kRing - 1))] * win[(size_t) i], 0.f);
        fft.forward(buf.data());
    }

    void capture()
    {
        for (int c = 0; c < 2; ++c)
        {
            analyse(c, kHop);
            for (int k = 0; k <= kN / 2; ++k) dphi[(size_t) c][(size_t) k] = std::arg(buf[(size_t) k]);
            analyse(c, 0);
            for (int k = 0; k <= kN / 2; ++k)
            {
                const float ph = std::arg(buf[(size_t) k]);
                mag[(size_t) c][(size_t) k] = std::abs(buf[(size_t) k]);
                dphi[(size_t) c][(size_t) k] = ph - dphi[(size_t) c][(size_t) k];     // phase travelled in one hop
                phase[(size_t) c][(size_t) k] = ph;
            }
        }
        for (auto& r : outRing) r.fill(0.f);
        captured = true;
    }

    void synthFrame()
    {
        for (int c = 0; c < 2; ++c)
        {
            for (int k = 0; k <= kN / 2; ++k)
            {
                float ph = phase[(size_t) c][(size_t) k] + dphi[(size_t) c][(size_t) k];
                ph -= 6.2831853f * std::floor(ph / 6.2831853f);
                phase[(size_t) c][(size_t) k] = ph;
                buf[(size_t) k] = std::polar(mag[(size_t) c][(size_t) k], ph);
                if (k > 0 && k < kN / 2) buf[(size_t) (kN - k)] = std::conj(buf[(size_t) k]);
            }
            buf[0] = Cpx(buf[0].real(), 0.f); buf[(size_t) (kN / 2)] = Cpx(buf[(size_t) (kN / 2)].real(), 0.f);
            fft.inverse(buf.data());
            // the frame is the Hann-windowed sound; x Hann again and overlapped 4 times it sums to 1.5
            const int wp = pos & (kN - 1);
            for (int i = 0; i < kN; ++i)
                outRing[(size_t) c][(size_t) ((wp + i) & (kN - 1))] += buf[(size_t) i].real() * win[(size_t) i] * (1.f / 1.5f);
        }
    }

    double sr = 48000.0;
    FftT<kOrder> fft;
    std::array<float, kN> win {};
    std::array<std::array<float, kRing>, 2> inRing {};
    std::array<std::array<float, kN>, 2> outRing {};
    std::array<std::array<float, kN / 2 + 1>, 2> mag {}, phase {}, dphi {};
    std::array<Cpx, kN> buf {};
    int pos = 0, hopCount = 0;
    float gain = 0.f;
    bool on = false, captured = false, wantCapture = false;
};
} // namespace swello
