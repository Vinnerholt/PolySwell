#pragma once
// Two-tap crossfading delay-line pitch shifter, tuned for tiny shifts (a few cents).
// At +-25 cents the delay sweeps so slowly that the crossfade is inaudible. The shifted
// copy is delayed by ~window/2 (about 20 ms); the dry path is not touched.
#include <algorithm>
#include <cmath>
#include <vector>

namespace swello
{
class Detuner
{
public:
    void prepare(double sr)
    {
        W = (float) (0.040 * sr);
        int n = 1; while (n < (int) W + 8) n <<= 1;
        buf.assign((size_t) n, 0.f);
        mask = n - 1;
        reset();
    }
    void reset() { std::fill(buf.begin(), buf.end(), 0.f); w = 0; p = 0.f; }

    // only feed the delay line (the shifter is idle, but stays ready without a click when it comes back)
    void push(float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }

    // ratio > 1 shifts up, < 1 shifts down
    float process(float x, float ratio)
    {
        buf[(size_t) w] = x;
        float out = 0.f;
        for (int k = 0; k < 2; ++k)
        {
            float pp = p + 0.5f * (float) k;
            if (pp >= 1.f) pp -= 1.f;
            const float d   = pp * W + 1.f;
            const float rp  = (float) w - d;
            const float fl  = std::floor(rp);
            const int   i0  = (int) fl;
            const float fr  = rp - fl;
            const float a   = buf[(size_t) (i0 & mask)];
            const float b   = buf[(size_t) ((i0 + 1) & mask)];
            // sin^2(pi pp) without a sin: Bhaskara's approximation of sin (error < 0.2 %); the two taps, half a
            // cycle apart, still sum to ~unity gain (the ripple is far below audibility)
            const float q   = pp * (1.f - pp);
            const float s   = 16.f * q / (5.f - 4.f * q);
            out += (a + (b - a) * fr) * s * s;
        }
        p -= (ratio - 1.f) / W;
        if (p < 0.f) p += 1.f;
        if (p >= 1.f) p -= 1.f;
        w = (w + 1) & mask;
        return out;
    }

private:
    std::vector<float> buf;
    int mask = 0, w = 0;
    float W = 1920.f, p = 0.f;
};
}
