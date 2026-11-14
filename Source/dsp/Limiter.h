#pragma once
// Stereo-linked safety limiter: instant attack (output never exceeds the ceiling), 100 ms release.
#include <algorithm>
#include <cmath>

namespace swello
{
class Limiter
{
public:
    void prepare(double sr) { rel = 1.f - std::exp(-1.f / (0.100f * (float) sr)); g = 1.f; }
    void reset() { g = 1.f; }
    void process(float& l, float& r)
    {
        const float peak = std::max(std::abs(l), std::abs(r));
        const float target = peak > ceiling ? ceiling / peak : 1.f;
        if (target < g) g = target; else g += (target - g) * rel;
        l *= g; r *= g;
    }
    static constexpr float ceiling = 0.97f;   // about -0.26 dBFS
private:
    float g = 1.f, rel = 0.001f;
};
}
