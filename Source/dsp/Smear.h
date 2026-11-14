#pragma once
// SMEAR: a gated reverb with drive in its tail, after the Rainger FX Reverb-X that mk.gee uses (owner's reference).
// An 8-line feedback delay network with input diffusion (decay 0.3 .. 6 s), optional DARK damping, DRIVE that
// distorts only the reverb (the notes stay clean) and a GATE keyed by the playing: after the guitar stops it stays
// open for HOLD, then closes over CUT (a few ms = a hard chop, up to 0.5 s = a soft fade). No JUCE dependency,
// no latency. The caller adds the returned reverb to its own signal (LEVEL = how much).
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace swello
{
class Smear
{
public:
    struct Settings
    {
        float size = 0.5f;      // 0 .. 1: decay 0.3 .. 6 s
        float drive = 0.f;      // 0 .. 1: distortion of the reverb tail
        bool  gate = false;     // cut the tail when the playing stops
        bool  dark = false;     // murkier, darker tail
        float holdMs = 150.f;   // gate: stays open this long after the playing stops
        float cutMs = 60.f;     // gate: then closes over this time
        float send = 1.f;       // 0 .. 1: how much of the signal goes into the reverb
        float threshDb = -80.f; // THRESH: only playing above this goes into the reverb (the Reverb-X's input gate)
    };

    void prepare(double sampleRate)
    {
        sr = (float) sampleRate;
        static constexpr int base[kLines] = { 1103, 1277, 1429, 1601, 1777, 1949, 2111, 2297 };
        for (int i = 0; i < kLines; ++i)
        {
            len[(size_t) i] = std::max(8, (int) std::lround(base[i] * sr / 48000.f));
            lines[(size_t) i].assign((size_t) nextPow2(len[(size_t) i] + 2), 0.f);
        }
        static constexpr int ap[kDiff] = { 142, 107, 379, 277 };
        for (int i = 0; i < kDiff; ++i)
        {
            apLen[(size_t) i] = std::max(4, (int) std::lround(ap[i] * sr / 48000.f));
            apBuf[(size_t) i].assign((size_t) nextPow2(apLen[(size_t) i] + 2), 0.f);
        }
        reset();
    }

    void reset()
    {
        for (auto& l : lines) std::fill(l.begin(), l.end(), 0.f);
        for (auto& a : apBuf) std::fill(a.begin(), a.end(), 0.f);
        lp.fill(0.f); w = 0; apW.fill(0); keyEnv = 0.f; inGate = 0.f; holdLeft = 0.f; gateGain = 0.f; toneL = toneR = 0.f; drvL = drvR = 0.f;
        lastSize = -1.f;
    }

    // in: the signal to smear (stereo), key: the dry playing (for the gate). Returns the reverb (to be added).
    void process(float inL, float inR, float key, const Settings& s, float& outL, float& outR)
    {
        if (s.size != lastSize) { setDecay(s.size); lastSize = s.size; }
        const float damp = s.dark ? dampDark : dampBright;

        // THRESH (input gate, before the reverb, like the Reverb-X by default): only playing above the threshold
        // feeds the reverb; opens in ~2 ms, closes over ~60 ms. The key follower is shared with the tail GATE.
        const float a = std::abs(key);
        keyEnv = a > keyEnv ? a + (keyEnv - a) * keyAtt : a + (keyEnv - a) * keyRel;
        const float thr = std::pow(10.f, s.threshDb / 20.f);
        const float inTarget = (s.threshDb <= -79.f || keyEnv > thr) ? 1.f : 0.f;
        inGate += (inTarget - inGate) * (inTarget > inGate ? inOpen : inClose);

        // input diffusion (series all-passes) on the mono send
        float x = 0.5f * (inL + inR) * s.send * inGate;
        for (int i = 0; i < kDiff; ++i)
        {
            auto& b = apBuf[(size_t) i];
            const int mask = (int) b.size() - 1;
            const float d = b[(size_t) ((apW[(size_t) i] - apLen[(size_t) i]) & mask)];
            const float y = -0.6f * x + d;
            b[(size_t) apW[(size_t) i]] = x + 0.6f * y;
            apW[(size_t) i] = (apW[(size_t) i] + 1) & mask;
            x = y;
        }

        // FDN: read, damp, Householder mix, write back with each line's decay gain
        std::array<float, kLines> y {};
        float sum = 0.f;
        for (int i = 0; i < kLines; ++i)
        {
            auto& l = lines[(size_t) i];
            const int mask = (int) l.size() - 1;
            const float v = l[(size_t) ((w - len[(size_t) i]) & mask)];
            lp[(size_t) i] += (v - lp[(size_t) i]) * damp;
            y[(size_t) i] = lp[(size_t) i];
            sum += y[(size_t) i];
        }
        sum *= 2.f / (float) kLines;
        float l = 0.f, r = 0.f;
        for (int i = 0; i < kLines; ++i)
        {
            auto& line = lines[(size_t) i];
            const int mask = (int) line.size() - 1;
            const float fb = (y[(size_t) i] - sum) * g[(size_t) i];
            line[(size_t) (w & mask)] = x * ((i & 1) ? -0.35f : 0.35f) + fb;
            if (i & 1) r += y[(size_t) i]; else l += y[(size_t) i];
        }
        ++w;
        l *= 0.5f; r *= 0.5f;

        if (s.dark) { toneL += (l - toneL) * toneDark; toneR += (r - toneR) * toneDark; l = toneL; r = toneR; }

        // DRIVE: only the tail, soft clip with a little smoothing of the fizz
        if (s.drive > 0.001f)
        {
            const float k = 1.f + 40.f * s.drive * s.drive;
            const float norm = 1.f / std::pow(k, 0.55f);
            l = std::tanh(k * l) * norm; r = std::tanh(k * r) * norm;
            drvL += (l - drvL) * drvSmooth; drvR += (r - drvR) * drvSmooth;
            l = drvL + (l - drvL) * (1.f - s.drive * 0.6f);
            r = drvR + (r - drvR) * (1.f - s.drive * 0.6f);
        }

        // GATE keyed by the playing: open while it sounds, HOLD after, then close over CUT
        float target = 1.f;
        if (s.gate)
        {
            if (keyEnv > kGateOpen) holdLeft = s.holdMs * 0.001f * sr;
            else if (holdLeft > 0.f) holdLeft -= 1.f;
            target = (keyEnv > kGateOpen || holdLeft > 0.f) ? 1.f : 0.f;
            const float step = target > gateGain ? 1.f / (0.003f * sr) : 1.f / (std::max(s.cutMs, 1.f) * 0.001f * sr);
            gateGain = target > gateGain ? std::min(target, gateGain + step) : std::max(target, gateGain - step);
        }
        else gateGain = 1.f;
        const float gg = gateGain * gateGain * (3.f - 2.f * gateGain);   // smooth edges, the time is CUT
        outL = l * gg; outR = r * gg;
    }

    float gateLevel() const { return gateGain; }

private:
    static constexpr int kLines = 8, kDiff = 4;
    static constexpr float kGateOpen = 0.003f;   // about -50 dBFS
    static int nextPow2(int n) { int p = 1; while (p < n) p <<= 1; return p; }

    void setDecay(float size)
    {
        const float t60 = 0.3f * std::pow(20.f, std::clamp(size, 0.f, 1.f));   // 0.3 .. 6 s
        for (int i = 0; i < kLines; ++i)
            g[(size_t) i] = std::pow(10.f, -3.f * (float) len[(size_t) i] / (sr * t60));
        auto onePole = [this](float hz) { return 1.f - std::exp(-2.f * 3.14159265f * hz / sr); };
        dampBright = onePole(7000.f); dampDark = onePole(2200.f); toneDark = onePole(2500.f); drvSmooth = onePole(5000.f);
        keyAtt = std::exp(-1.f / (0.001f * sr)); keyRel = std::exp(-1.f / (0.02f * sr));
        inOpen = 1.f - std::exp(-1.f / (0.002f * sr)); inClose = 1.f - std::exp(-1.f / (0.06f * sr));
    }

    float sr = 48000.f, lastSize = -1.f;
    std::array<std::vector<float>, kLines> lines;
    std::array<int, kLines> len {};
    std::array<float, kLines> g {}, lp {};
    std::array<std::vector<float>, kDiff> apBuf;
    std::array<int, kDiff> apLen {}, apW {};
    int w = 0;
    float dampBright = 0.5f, dampDark = 0.2f, toneDark = 0.2f, drvSmooth = 0.5f, keyAtt = 0.9f, keyRel = 0.99f;
    float inGate = 0.f, inOpen = 0.1f, inClose = 0.01f;
    float keyEnv = 0.f, holdLeft = 0.f, gateGain = 0.f, toneL = 0.f, toneR = 0.f, drvL = 0.f, drvR = 0.f;
};
}
