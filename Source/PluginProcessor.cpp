#include "PluginProcessor.h"
#include "BinaryData.h"
#include "PluginEditor.h"

using APF = juce::AudioParameterFloat;
using PID = juce::ParameterID;

namespace
{
    // PolySwell II presets. Every preset starts from Default (all parameters at their defaults, the main pedal only:
    // the POG2 swell, the filter open at 18 dB/oct so TONE works) and sets only what it lists.
    struct NewPreset { const char* name; const char* tag; std::vector<std::pair<const char*, float>> set; };
    const std::vector<std::pair<const char*, float>>& defaultSetup()
    {
        static const std::vector<std::pair<const char*, float>> d {
            { "attack", 500 }, { "sustain", 0 }, { "swellShape", 2 }, { "levelMatch", 1 },
            { "filterOn", 1 }, { "freq", 12000 }, { "slope", 4 }, { "mod", 0 }, { "res", 0 }, { "pickFilter", 0 },
            { "feedbackOn", 1 }, { "feedback", 0 }, { "fbAmp", 1 },
            { "smearOn", 0 }, { "voiceMix", 0 }, { "detuneMix", 0 }, { "detune", 10.1f } };
        return d;
    }
    // Baselines for the owner to tune by ear (2026-11-23); MATCH is on in all unless listed. vibTarget: 0 voice,
    // 1 guitar + voice, 2 guitar. voiceWave: 0 supersaw 1 saw 2 PWM 3 square 4 organ 5 glass 6 reed 7 pad (VG) 8 bow.
    const std::vector<NewPreset>& newPresets()
    {
        static const std::vector<NewPreset> p {
            // the owner's own Default (tuned by ear, saved 2026-11-23): a 2 s POG2 swell with some SUSTAIN, the effect modules
            // off; their settings are kept so switching a module on brings the owner's values (feedback, slow tremolo, detune)
            { "Default", "Default", { { "attack", 2066.7f }, { "sustain", 0.423f }, { "filterOn", 0 }, { "freq", 6854 },
                                      { "feedbackOn", 0 }, { "feedback", 0.386f }, { "fbRise", 0.2f },
                                      { "motionOn", 0 }, { "tremolo", 0.177f }, { "tremRate", 1.62f }, { "detuneMix", 0.209f },
                                      { "voiceOn", 0 } } },
            // the pedal reacting to the playing
            { "Default dynamic", "Default", { { "attack", 450 }, { "touch", 0.4f }, { "follow", 0.5f }, { "even", 0.3f }, { "sustain", 0.35f },
                                               { "freq", 5000 }, { "pickFilter", 0.3f },
                                               { "feedback", 0.2f }, { "fbDistance", 0.3f }, { "fbRise", 1.5f } } },
            // rolled-off highs, a little dynamic, a saw voice gliding under it, vibrato that sets in late
            { "Smooth lead", "Lead", { { "attack", 350 }, { "sustain", 0.55f }, { "touch", 0.3f }, { "follow", 0.35f }, { "even", 0.25f },
                                       { "freq", 2800 }, { "res", 0.1f }, { "pickFilter", 0.2f },
                                       { "voiceMix", 0.25f }, { "voiceWave", 1 }, { "voiceTone", 2500 }, { "voiceGlide", 0.06f }, { "voiceMono", 1 }, { "voiceCharacter", 0.3f },
                                       { "vibDepth", 8 }, { "vibRate", 5.2f }, { "vibDelay", 0.4f }, { "vibTarget", 1 }, { "vibHuman", 1 },
                                       { "smearOn", 1 }, { "smearSize", 0.45f }, { "smearLevel", 0.25f } } },
            // bowed attack, a violin body, singing vibrato
            { "Violin", "Lead", { { "attack", 400 }, { "even", 0.5f }, { "sustain", 0.45f }, { "smoothRepick", 1 },
                                  { "slope", 0 }, { "freq", 6000 }, { "mod", 0.15f }, { "bodyIr", 1 }, { "bodyMix", 0.8f },
                                  { "voiceMix", 0.35f }, { "voiceWave", 1 }, { "voiceGlide", 0.05f }, { "voiceMono", 1 },
                                  { "vibDepth", 14 }, { "vibDelay", 0.35f }, { "vibTarget", 1 }, { "vibHuman", 1 } } },
            // the VG-8 bowed pad under the guitar, a driven gated reverb, a little chorus-like detune (the mk.gee kind of sound)
            { "Gated VG", "Lead", { { "vol", -4.5f }, { "attack", 400 }, { "sustain", 0.4f }, { "even", 0.3f }, { "dry", 0.15f }, { "freq", 4000 },
                                    { "voiceMix", 0.45f }, { "voiceWave", 7 }, { "voiceTone", 4000 }, { "voiceCharacter", 0.5f }, { "voiceUnison", 0.2f },
                                    { "detuneMix", 0.3f }, { "vibDepth", 5 }, { "vibRate", 4.5f }, { "vibTarget", 2 },
                                    { "smearOn", 1 }, { "smearSize", 0.55f }, { "smearLevel", 0.5f }, { "smearDrive", 0.5f },
                                    { "smearGate", 1 }, { "smearHold", 150 }, { "smearCut", 40 } } },
            // slow, wide, a pad voice under it, the reverb blooming with the swell
            { "Ambient pad", "Pad", { { "vol", 4.5f }, { "attack", 2500 }, { "sustain", 0.8f }, { "even", 0.4f }, { "freq", 2200 }, { "mod", 0.25f },
                                      { "voiceMix", 0.4f }, { "voiceWave", 7 }, { "voiceTone", 3000 }, { "voiceUnison", 0.4f }, { "voiceCharacter", 0.3f },
                                      { "spread", 0.6f }, { "detuneMix", 0.35f },
                                      { "smearOn", 1 }, { "smearSize", 0.8f }, { "smearLevel", 0.55f }, { "smearSend", 1 }, { "smearDark", 1 } } },
            // a bell-like voice an octave up, a big space
            { "Glass shimmer", "Pad", { { "attack", 1400 }, { "sustain", 0.6f }, { "voiceMix", 0.45f }, { "voiceWave", 5 }, { "voiceOct", 2 }, { "voiceUnison", 0.3f },
                                        { "spread", 0.5f }, { "smearOn", 1 }, { "smearSize", 0.75f }, { "smearLevel", 0.6f } } },
            // the owner's measured POG2 setting (run G1): ATTACK 2 s, 18 dB/oct at 1 kHz, the +-10 cent copies (MIX 0.39)
            { "POG2", "Pad", { { "vol", 7.5f }, { "attack", 2000 }, { "freq", 1000 }, { "detune", 10.1f }, { "detuneMix", 0.39f } } },
            // held notes bloom into an octave of feedback, as in front of a loud amp
            { "Amp in the room", "Feedback", { { "vol", -3.5f }, { "attack", 80 }, { "sustain", 0.5f }, { "dry", 0.3f },
                                               { "feedback", 0.8f }, { "fbDistance", 0.3f }, { "fbRise", 1.2f }, { "fbTone", 1600 } } },
            // classic volume swells (pedal steel) that still speak when the playing gets fast
            { "Slow gear", "Effect", { { "vol", 1.5f }, { "attack", 900 }, { "follow", 0.6f }, { "sustain", 0.2f }, { "smearOn", 1 }, { "smearSize", 0.35f }, { "smearLevel", 0.2f } } },
            // a backwards-sounding bloom: no make-up, the reverb swelling with the note
            { "Reverse tape", "Effect", { { "vol", 5.0f }, { "attack", 500 }, { "levelMatch", 0 }, { "smearOn", 1 }, { "smearSend", 1 }, { "smearSize", 0.5f }, { "smearLevel", 0.45f } } },
            // an auto-wah on every note: the filter opens with each pick, harder picks further
            { "Envelope filter", "Effect", { { "vol", -4.5f }, { "attack", 20 }, { "slope", 1 }, { "freq", 600 }, { "mod", 0.7f }, { "res", 0.55f }, { "pickFilter", 0.6f } } },
            // lo-fi and seasick
            { "Warble", "Effect", { { "vol", 2.5f }, { "attack", 600 }, { "tremolo", 0.4f }, { "tremRate", 5.5f }, { "tremShape", 0 },
                                    { "vibDepth", 6 }, { "vibRate", 4.5f }, { "vibTarget", 2 }, { "detuneMix", 0.4f } } } };
        return p;
    }
}

// tremolo note values: name and length of one tremolo cycle in beats
static const std::vector<std::pair<const char*, double>>& tremDivs()
{
    static const std::vector<std::pair<const char*, double>> d {
        { "1/1", 4.0 }, { "1/2", 2.0 }, { "1/2 dotted", 3.0 }, { "1/2 triplet", 4.0 / 3.0 },
        { "1/4", 1.0 }, { "1/4 dotted", 1.5 }, { "1/4 triplet", 2.0 / 3.0 },
        { "1/8", 0.5 }, { "1/8 dotted", 0.75 }, { "1/8 triplet", 1.0 / 3.0 },
        { "1/16", 0.25 }, { "1/16 dotted", 0.375 }, { "1/16 triplet", 1.0 / 6.0 }, { "1/32", 0.125 } };
    return d;
}

juce::StringArray PolySwellProcessor::tremDivNames()
{
    juce::StringArray a;
    for (auto& d : tremDivs()) a.add (d.first);
    return a;
}

double PolySwellProcessor::tremDivBeats (int idx)
{
    return tremDivs()[(size_t) juce::jlimit (0, (int) tremDivs().size() - 1, idx)].second;
}

float PolySwellProcessor::divisionBeats (int idx)
{
    static const float b[] = { 0.25f, 0.5f, 1.f, 2.f, 4.f, 8.f };
    return b[juce::jlimit (0, 5, idx)];
}

// a knob taper through measured points (position, value): log-spaced between points, linear from a 0 value.
// ATTACK and TONE follow the POG2's sliders (docs/POG2_TEST_PLAN.md): most of the travel for short swells / low
// cutoffs, the long swells and the open filter crowded into the top.
static juce::NormalisableRange<float> taper (std::vector<std::pair<float, float>> pts)
{
    auto toValue = [pts] (float, float, float p)
    {
        p = juce::jlimit (0.f, 1.f, p);
        for (size_t i = 1; i < pts.size(); ++i)
            if (p <= pts[i].first || i + 1 == pts.size())
            {
                const auto a = pts[i - 1], b = pts[i];
                const float u = juce::jlimit (0.f, 1.f, (p - a.first) / (b.first - a.first));
                return a.second <= 0.f ? a.second + (b.second - a.second) * u : a.second * std::pow (b.second / a.second, u);
            }
        return pts.back().second;
    };
    auto toPos = [pts] (float, float, float v)
    {
        for (size_t i = 1; i < pts.size(); ++i)
            if (v <= pts[i].second || i + 1 == pts.size())
            {
                const auto a = pts[i - 1], b = pts[i];
                const float u = a.second <= 0.f ? (v - a.second) / (b.second - a.second)
                                                : std::log (juce::jmax (v, 1.0e-6f) / a.second) / std::log (b.second / a.second);
                return juce::jlimit (0.f, 1.f, a.first + (b.first - a.first) * juce::jlimit (0.f, 1.f, u));
            }
        return 1.f;
    };
    return { pts.front().second, pts.back().second, toValue, toPos,
             [lo = pts.front().second, hi = pts.back().second] (float, float, float v) { return juce::jlimit (lo, hi, v); } };
}

float PolySwellProcessor::detuneCopyLevel (float k)
{
    static const float pos[4] = { 0.f, 0.375f, 0.5f, 1.f }, lev[4] = { 0.f, 0.21f, 0.5f, 1.f };
    k = juce::jlimit (0.f, 1.f, k);
    for (int i = 1; i < 4; ++i) if (k <= pos[i]) return lev[i - 1] + (lev[i] - lev[i - 1]) * (k - pos[i - 1]) / (pos[i] - pos[i - 1]);
    return 1.f;
}

juce::AudioProcessorValueTreeState::ParameterLayout PolySwellProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    l.add (std::make_unique<APF> (PID { "vol", 1 }, "Vol",
           // two linear halves so 0 dB sits at 12 o'clock: left half -24..0 dB, right half 0..+12 dB
           juce::NormalisableRange<float> (-24.f, 12.f,
               [] (float, float, float p) { return p < 0.5f ? -24.f + 48.f * p : (p - 0.5f) * 24.f; },
               [] (float, float, float v) { return v < 0.f ? (v + 24.f) / 48.f : 0.5f + v / 24.f; },
               [] (float s, float e, float v) { return juce::jlimit (s, e, std::round (v * 10.f) / 10.f); }), 0.f,
           juce::AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<APF> (PID { "attack", 1 }, "Attack",
           taper ({ { 0.f, 0.f }, { 0.125f, 50.f }, { 0.25f, 95.f }, { 0.375f, 220.f }, { 0.5f, 500.f }, { 0.75f, 4400.f }, { 1.f, 12000.f } }),
           300.f,   // 0 = no swell; the POG2's slider: 95 ms at 1/4, 0.5 s in the middle, 4.4 s at 3/4
           juce::AudioParameterFloatAttributes().withLabel ("ms")));
    l.add (std::make_unique<APF> (PID { "freq", 1 }, "Freq",
           taper ({ { 0.f, 120.f }, { 0.25f, 247.f }, { 0.5f, 830.f }, { 0.75f, 3500.f }, { 1.f, 12000.f } }), 2000.f,   // the POG2's LP FILTER slider
           juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    l.add (std::make_unique<APF> (PID { "mod", 1 }, "Mod",
           juce::NormalisableRange<float> (-1.f, 1.f, 0.01f), 0.f));
    l.add (std::make_unique<juce::AudioParameterBool> (PID { "filterOn", 1 }, "Filter", false));
    l.add (std::make_unique<APF> (PID { "res", 1 }, "Res",
           juce::NormalisableRange<float> (0.f, 1.f, 0.001f), 0.f));
    // old filter switch (Off / Low Res / High Res with a resonance each): only read to convert old sessions/presets
    l.add (std::make_unique<juce::AudioParameterChoice> (PID { "filter", 1 }, "Filter (old)",
           juce::StringArray { "Off", "Low Res", "High Res" }, 0, juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    l.add (std::make_unique<juce::AudioParameterBool> (PID { "bypass", 1 }, "Bypass", false));

    l.add (std::make_unique<APF> (PID { "resLow", 1 },  "Res Low (old)",
           juce::NormalisableRange<float> (0.f, 1.f, 0.001f), 0.f, juce::AudioParameterFloatAttributes().withAutomatable (false)));
    l.add (std::make_unique<APF> (PID { "resHigh", 1 }, "Res High (old)",
           juce::NormalisableRange<float> (0.f, 1.f, 0.001f), 0.65f, juce::AudioParameterFloatAttributes().withAutomatable (false)));
    l.add (std::make_unique<APF> (PID { "sustain", 1 }, "Sustain",
           juce::NormalisableRange<float> (0.f, 1.f, 0.001f), 0.f));

    // Batch 1 additions (not on the pedal; defaults = pedal behaviour)
    l.add (std::make_unique<juce::AudioParameterChoice> (PID { "slope", 1 }, "Slope",
           juce::StringArray { "6 dB/oct", "12 dB/oct", "24 dB/oct", "48 dB/oct", "18 dB/oct (POG2)" }, 4));
    l.add (std::make_unique<APF> (PID { "detune", 1 }, "Detune Cents",   // fine tuning (gear); the POG2's copies are +-10.1 c
           juce::NormalisableRange<float> (0.f, 25.f, 0.1f), 10.1f,
           juce::AudioParameterFloatAttributes().withLabel ("cents")));
    l.add (std::make_unique<APF> (PID { "dry", 1 }, "Dry Mix",
           juce::NormalisableRange<float> (0.f, 1.f, 0.001f), 0.f));
    l.add (std::make_unique<juce::AudioParameterBool> (PID { "sync", 1 }, "Tempo Sync", false));
    l.add (std::make_unique<juce::AudioParameterChoice> (PID { "syncDiv", 1 }, "Sync Division",
           juce::StringArray { "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars" }, 2));
    l.add (std::make_unique<juce::AudioParameterBool> (PID { "limiter", 1 }, "Limiter", true));

    // Batch 2: playing dynamics (0 = off = pedal behaviour)
    l.add (std::make_unique<APF> (PID { "touch", 1 }, "Dynamics",
           juce::NormalisableRange<float> (-1.f, 1.f, 0.01f), 0.f));
    l.add (std::make_unique<APF> (PID { "follow", 1 }, "Pace",
           juce::NormalisableRange<float> (0.f, 1.f, 0.001f), 0.f));

    // Batch 3: feedback and low latency
    l.add (std::make_unique<APF> (PID { "feedback", 1 }, "Feedback",
           juce::NormalisableRange<float> (0.f, 1.f, 0.001f), 0.f));
    l.add (std::make_unique<juce::AudioParameterBool> (PID { "lowLatency", 1 }, "Low Latency", false));

    // Fine-tune (right-click menu); defaults are the tuned values
    auto unit = [] (const char* u, int decimals)
    {
        return juce::AudioParameterFloatAttributes().withLabel (u).withStringFromValueFunction (
            [u, decimals] (float v, int) { return juce::String (v, decimals) + " " + u; });
    };
    l.add (std::make_unique<APF> (PID { "touchRange", 1 }, "Dynamics Range",
           juce::NormalisableRange<float> (3.f, 24.f, 0.5f), 12.f, unit ("dB", 1)));
    l.add (std::make_unique<APF> (PID { "touchDepth", 1 }, "Dynamics Depth",
           juce::NormalisableRange<float> (0.5f, 3.f, 0.1f), 2.f, unit ("oct", 1)));
    l.add (std::make_unique<APF> (PID { "pickRange", 1 }, "Filter Dyn Range",
           juce::NormalisableRange<float> (3.f, 24.f, 0.5f), 9.f, unit ("dB", 1)));
    l.add (std::make_unique<APF> (PID { "pickDepth", 1 }, "Filter Dyn Depth",
           juce::NormalisableRange<float> (1.f, 6.f, 0.1f), 4.f, unit ("oct", 1)));
    l.add (std::make_unique<APF> (PID { "followTight", 1 }, "Pace Tightness",
           juce::NormalisableRange<float> (0.1f, 1.f, 0.05f), 0.4f, unit ("x gap", 2)));
    l.add (std::make_unique<APF> (PID { "chordMs", 1 }, "Strum Window",
           juce::NormalisableRange<float> (5.f, 250.f, 1.f, 0.6f), 60.f, unit ("ms", 0)));
    l.add (std::make_unique<APF> (PID { "sustainMax", 1 }, "Bloom Max Boost",
           juce::NormalisableRange<float> (6.f, 36.f, 1.f), 24.f, unit ("dB", 0)));
    l.add (std::make_unique<juce::AudioParameterChoice> (PID { "fbHarmonic", 1 }, "Feedback Harmonic",
           juce::StringArray { "Octave", "12th (octave + fifth)", "Unison", "Sub octave" }, 0));
    l.add (std::make_unique<APF> (PID { "fbRise", 1 }, "Feedback Rise",
           juce::NormalisableRange<float> (0.2f, 5.f, 0.05f, 0.5f), 1.2f, unit ("s", 2)));
    l.add (std::make_unique<APF> (PID { "fbTone", 1 }, "Feedback Tone",
           juce::NormalisableRange<float> (300.f, 8000.f, 1.f, 0.35f), 1100.f, unit ("Hz", 0)));

    // Motion: per-note tremolo and stereo spread
    l.add (std::make_unique<APF> (PID { "tremolo", 1 }, "Tremolo",
           juce::NormalisableRange<float> (0.f, 1.f, 0.001f), 0.f));
    l.add (std::make_unique<APF> (PID { "tremRate", 1 }, "Tremolo Rate",
           juce::NormalisableRange<float> (0.5f, 12.f, 0.01f, 0.5f), 5.f, unit ("Hz", 2)));
    l.add (std::make_unique<APF> (PID { "spread", 1 }, "Spread",
           juce::NormalisableRange<float> (0.f, 1.f, 0.001f), 0.f));
    l.add (std::make_unique<juce::AudioParameterChoice> (PID { "tremShape", 1 }, "Tremolo Shape",
           juce::StringArray { "Sine", "Triangle", "Square" }, 0));
    l.add (std::make_unique<juce::AudioParameterBool> (PID { "tremSyncOn", 1 }, "Tremolo Sync", false));
    l.add (std::make_unique<juce::AudioParameterChoice> (PID { "tremDiv", 1 }, "Tremolo Division",
           tremDivNames(), 7));
    l.add (std::make_unique<juce::AudioParameterChoice> (PID { "spreadMode", 1 }, "Spread Mode",
           juce::StringArray { "Alternate", "Random", "By pitch", "By pick strength", "Loud in the middle", "Soft in the middle" }, 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (PID { "fbMode", 1 }, "Feedback Mode",
           juce::StringArray { "Bloom (amp feedback)", "Immediate (harmonizer)" }, 0));

    l.add (std::make_unique<APF> (PID { "pickFilter", 1 }, "Pick",
           juce::NormalisableRange<float> (-1.f, 1.f, 0.01f), 0.f));
    l.add (std::make_unique<APF> (PID { "detuneMix", 1 }, "Detune",       // the DETUNE knob: how loud the copies are (POG2 taper)
           juce::NormalisableRange<float> (0.f, 1.f, 0.001f), 0.f,
           juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.f)) + " %"; })));
    l.add (std::make_unique<APF> (PID { "detuneWobble", 1 }, "Detune Wobble",
           juce::NormalisableRange<float> (0.f, 1.f, 0.001f), 0.f));

    // Pedals: each effect pedal's footswitch
    l.add (std::make_unique<juce::AudioParameterBool> (PID { "bloomOn", 1 },  "Bloom Pedal",  true));
    l.add (std::make_unique<juce::AudioParameterBool> (PID { "motionOn", 1 }, "Motion Pedal", true));
    l.add (std::make_unique<juce::AudioParameterBool> (PID { "voiceOn", 1 },  "Voice Pedal",  true));

    // VOICE
    l.add (std::make_unique<APF> (PID { "voiceMix", 1 }, "Voice Mix",
           juce::NormalisableRange<float> (0.f, 1.f, 0.001f), 0.f));
    l.add (std::make_unique<juce::AudioParameterChoice> (PID { "voiceWave", 1 }, "Voice Wave",
           juce::StringArray { "Supersaw", "Saw", "PWM", "Square", "Organ", "Glass", "Reed", "VG", "Bow" }, 1));
    l.add (std::make_unique<APF> (PID { "voiceGlide", 1 }, "Voice Glide",
           juce::NormalisableRange<float> (0.f, 0.3f, 0.001f, 0.6f), 0.f, juce::AudioParameterFloatAttributes().withLabel ("s")));
    l.add (std::make_unique<juce::AudioParameterBool> (PID { "voiceMono", 1 }, "Voice Mono", false));
    l.add (std::make_unique<juce::AudioParameterBool> (PID { "vibHuman", 1 }, "Vibrato Human", false));
    l.add (std::make_unique<juce::AudioParameterChoice> (PID { "voiceOct", 1 }, "Voice Octave",
           juce::StringArray { "-1", "0", "+1" }, 1));
    l.add (std::make_unique<APF> (PID { "voiceTone", 1 }, "Voice Tone",
           juce::NormalisableRange<float> (200.f, 12000.f, 1.f, 0.3f), 3000.f, unit ("Hz", 0)));
    l.add (std::make_unique<APF> (PID { "voiceUnison", 1 }, "Voice Unison",
           juce::NormalisableRange<float> (0.f, 1.f, 0.001f), 0.f));
    l.add (std::make_unique<APF> (PID { "voiceDrift", 1 }, "Voice Drift",
           juce::NormalisableRange<float> (0.f, 1.f, 0.001f), 0.f));
    l.add (std::make_unique<APF> (PID { "voiceDetune", 1 }, "Unison Detune",
           juce::NormalisableRange<float> (0.f, 50.f, 0.1f), 15.f, unit ("cents", 1)));
    l.add (std::make_unique<APF> (PID { "voicePwmRate", 1 }, "PWM Rate",
           juce::NormalisableRange<float> (0.1f, 8.f, 0.01f, 0.5f), 0.8f, unit ("Hz", 2)));
    l.add (std::make_unique<APF> (PID { "voicePhaser", 1 }, "Voice Phaser",
           juce::NormalisableRange<float> (0.f, 1.f, 0.001f), 0.f));
    l.add (std::make_unique<juce::AudioParameterBool> (PID { "vocalOn", 1 }, "Voice Vocal On", true));
    // swell character (VG-8 BOWED spec): shape of the swell, smooth re-picks, EVEN (picks levelled)
    l.add (std::make_unique<juce::AudioParameterChoice> (PID { "swellShape", 1 }, "Swell Shape", juce::StringArray { "Slow", "Bowed", "POG2" }, 2));   // "Slow" was "Classic" (display only)
    l.add (std::make_unique<juce::AudioParameterBool> (PID { "smoothRepick", 1 }, "Smooth Re-pick", false));
    l.add (std::make_unique<juce::AudioParameterBool> (PID { "levelMatch", 1 }, "Level-matched Swell", true));   // on by default: closest to the POG2 on plucks
    // FEEDBACK amp model (PolySwell II)
    l.add (std::make_unique<juce::AudioParameterBool> (PID { "fbAmp", 1 }, "Feedback Amp Model", true));
    l.add (std::make_unique<APF> (PID { "fbDistance", 1 }, "Feedback Distance", juce::NormalisableRange<float> (0.f, 1.f, 0.001f), 0.3f));
    l.add (std::make_unique<juce::AudioParameterBool> (PID { "fbOneWins", 1 }, "Feedback One Note Wins", true));
    l.add (std::make_unique<APF> (PID { "even", 1 }, "Even", juce::NormalisableRange<float> (0.f, 1.f, 0.001f), 0.f));
    l.add (std::make_unique<juce::AudioParameterBool> (PID { "bowedBody", 1 }, "Bowed Body", false));
    l.add (std::make_unique<juce::AudioParameterChoice> (PID { "bodyIr", 1 }, "Body IR",
           juce::StringArray { "None", "Violin A", "Violin EN", "Violin EU", "Violin G1", "Violin G2", "Violin S1", "Violin S2", "Your IR" }, 0));
    l.add (std::make_unique<APF> (PID { "bodyMix", 1 }, "Body IR Mix", juce::NormalisableRange<float> (0.f, 1.f, 0.01f), 1.f));
    // DRIVE: tape / tube saturation
    l.add (std::make_unique<juce::AudioParameterBool> (PID { "driveOn", 1 }, "Drive On", false));
    l.add (std::make_unique<juce::AudioParameterChoice> (PID { "driveType", 1 }, "Drive Type", juce::StringArray { "Tape", "Tube" }, 0));
    l.add (std::make_unique<APF> (PID { "driveAmt", 1 }, "Drive", juce::NormalisableRange<float> (0.f, 1.f, 0.01f), 0.35f));
    l.add (std::make_unique<APF> (PID { "driveTone", 1 }, "Drive Tone", juce::NormalisableRange<float> (0.f, 1.f, 0.01f), 0.5f));
    l.add (std::make_unique<APF> (PID { "driveWow", 1 }, "Drive Wow", juce::NormalisableRange<float> (0.f, 1.f, 0.01f), 0.2f));
    // SMEAR: gated reverb + drive (after the Rainger FX Reverb-X)
    l.add (std::make_unique<juce::AudioParameterBool> (PID { "smearOn", 1 }, "Smear On", false));
    l.add (std::make_unique<APF> (PID { "smearLevel", 1 }, "Smear Level", juce::NormalisableRange<float> (0.f, 1.f, 0.001f), 0.5f));
    l.add (std::make_unique<APF> (PID { "smearSize", 1 }, "Smear Size", juce::NormalisableRange<float> (0.f, 1.f, 0.001f), 0.5f));
    l.add (std::make_unique<APF> (PID { "smearDrive", 1 }, "Smear Drive", juce::NormalisableRange<float> (0.f, 1.f, 0.001f), 0.f));
    l.add (std::make_unique<juce::AudioParameterBool> (PID { "smearGate", 1 }, "Smear Gate", false));
    l.add (std::make_unique<juce::AudioParameterBool> (PID { "smearDark", 1 }, "Smear Dark", false));
    l.add (std::make_unique<APF> (PID { "smearHold", 1 }, "Smear Gate Hold",
           juce::NormalisableRange<float> (0.f, 1000.f, 1.f, 0.5f), 150.f, juce::AudioParameterFloatAttributes().withLabel ("ms")));
    l.add (std::make_unique<APF> (PID { "smearCut", 1 }, "Smear Gate Cut",
           juce::NormalisableRange<float> (2.f, 500.f, 1.f, 0.4f), 40.f, juce::AudioParameterFloatAttributes().withLabel ("ms")));
    l.add (std::make_unique<juce::AudioParameterChoice> (PID { "smearSend", 1 }, "Smear Send",
           juce::StringArray { "Always", "Follows swell", "Pedal" }, 0));
    l.add (std::make_unique<APF> (PID { "smearPress", 1 }, "Smear Press", juce::NormalisableRange<float> (0.f, 1.f, 0.001f), 1.f));
    l.add (std::make_unique<APF> (PID { "smearThresh", 1 }, "Smear Thresh",
           juce::NormalisableRange<float> (-80.f, 0.f, 0.1f), -80.f, juce::AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<APF> (PID { "voiceString", 1 }, "Voice String", juce::NormalisableRange<float> (0.f, 1.f, 0.01f), 0.f));
    l.add (std::make_unique<APF> (PID { "voiceCharacter", 1 }, "Voice Character", juce::NormalisableRange<float> (0.f, 1.f, 0.01f), 0.f));
    l.add (std::make_unique<juce::AudioParameterChoice> (PID { "midiOut", 1 }, "MIDI Out",
           juce::StringArray { "Off", "Notes", "MPE" }, 0));
    l.add (std::make_unique<APF> (PID { "voiceLife", 1 }, "Voice Life", juce::NormalisableRange<float> (0.f, 1.f, 0.01f), 0.f));
    l.add (std::make_unique<APF> (PID { "voiceRes", 1 }, "Voice Res", juce::NormalisableRange<float> (0.f, 1.f, 0.01f), 0.f));
    l.add (std::make_unique<APF> (PID { "voiceDecay", 1 }, "Voice Env Decay",
           juce::NormalisableRange<float> (0.03f, 2.f, 0.001f, 0.4f), 0.18f, juce::AudioParameterFloatAttributes().withLabel ("s")));
    l.add (std::make_unique<APF> (PID { "voiceWarmth", 1 }, "Voice Warmth",
           juce::NormalisableRange<float> (0.f, 1.f, 0.01f), 0.f));
    l.add (std::make_unique<APF> (PID { "voiceVowel", 1 }, "Voice Vowel",
           juce::NormalisableRange<float> (0.f, 1.f, 0.001f), 0.f));
    l.add (std::make_unique<APF> (PID { "voiceVowelPick", 1 }, "Vowel By Pick",
           juce::NormalisableRange<float> (-1.f, 1.f, 0.01f), 0.5f));
    l.add (std::make_unique<APF> (PID { "voiceScoop", 1 }, "Voice Scoop",
           juce::NormalisableRange<float> (0.f, 1.f, 0.01f), 0.f));
    l.add (std::make_unique<APF> (PID { "voiceBreath", 1 }, "Voice Breath",
           juce::NormalisableRange<float> (0.f, 1.f, 0.01f), 0.f));
    l.add (std::make_unique<APF> (PID { "voiceStab", 1 }, "Voice Stab",
           juce::NormalisableRange<float> (0.f, 1.f, 0.001f), 0.f));
    l.add (std::make_unique<juce::AudioParameterChoice> (PID { "voiceChorus", 1 }, "Voice Chorus",
           juce::StringArray { "Off", "I", "II", "I + II" }, 0));

    // HOLD (PolySwell II): freeze what is ringing into a pad

    // VIBRATO
    l.add (std::make_unique<APF> (PID { "vibDepth", 1 }, "Vibrato",
           juce::NormalisableRange<float> (0.f, 50.f, 0.1f), 0.f, unit ("cents", 1)));
    l.add (std::make_unique<APF> (PID { "vibRate", 1 }, "Vibrato Rate",
           juce::NormalisableRange<float> (3.f, 8.f, 0.01f), 5.5f, unit ("Hz", 2)));
    l.add (std::make_unique<APF> (PID { "vibDelay", 1 }, "Vibrato Delay",
           juce::NormalisableRange<float> (0.f, 1.5f, 0.01f), 0.4f, unit ("s", 2)));
    l.add (std::make_unique<juce::AudioParameterChoice> (PID { "vibTarget", 1 }, "Vibrato On",
           juce::StringArray { "Voice", "Guitar + voice", "Guitar" }, 0));   // order kept for old sessions

    // On/off per knob (right-click). Off = the knob acts as if at its neutral value.
    for (const auto& s : switchables())
        l.add (std::make_unique<juce::AudioParameterBool> (PID { s.onId, 1 }, juce::String (s.onId), true));
    return l;
}

const std::vector<PolySwellProcessor::Switchable>& PolySwellProcessor::switchables()
{
    static const std::vector<Switchable> s {
        { "mod", "modOn", 0.f },     { "sustain", "sustainOn", 0.f }, { "detune", "detuneOn", 0.f },
        { "dry", "dryOn", 0.f },     { "touch", "touchOn", 0.f },     { "follow", "followOn", 0.f },
        { "feedback", "feedbackOn", 0.f }, { "tremolo", "tremoloOn", 0.f }, { "spread", "spreadOn", 0.f },
        { "voiceMix", "voiceMixOn", 0.f }, { "vibDepth", "vibDepthOn", 0.f },
        { "voiceUnison", "voiceUnisonOn", 0.f }, { "voiceDrift", "voiceDriftOn", 0.f }, { "pickFilter", "pickFilterOn", 0.f },
        { "voicePhaser", "voicePhaserOn", 0.f } };
    return s;
}

PolySwellProcessor::PolySwellProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::mono(),   true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undo, "STATE", createLayout())
{
    for (auto& m : ccMap) m = -1;
    for (auto* prm : getParameters()) if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm)) ccParams.add (r);
    pVol     = apvts.getRawParameterValue ("vol");
    pAttack  = apvts.getRawParameterValue ("attack");
    pFreq    = apvts.getRawParameterValue ("freq");
    pMod     = apvts.getRawParameterValue ("mod");
    pFilter  = apvts.getRawParameterValue ("filterOn");
    pResLow  = apvts.getRawParameterValue ("res");
    pResHigh = apvts.getRawParameterValue ("resHigh");
    pPickRange = apvts.getRawParameterValue ("pickRange");
    pPickDepth = apvts.getRawParameterValue ("pickDepth");
    pSustain = apvts.getRawParameterValue ("sustain");
    pBypass  = apvts.getRawParameterValue ("bypass");
    pSlope   = apvts.getRawParameterValue ("slope");
    pDetune  = apvts.getRawParameterValue ("detune");
    pDry     = apvts.getRawParameterValue ("dry");
    pSync    = apvts.getRawParameterValue ("sync");
    pSyncDiv = apvts.getRawParameterValue ("syncDiv");
    pLimiter = apvts.getRawParameterValue ("limiter");
    pTouch   = apvts.getRawParameterValue ("touch");
    pFollow  = apvts.getRawParameterValue ("follow");
    pFeedback    = apvts.getRawParameterValue ("feedback");
    pLowLatency  = apvts.getRawParameterValue ("lowLatency");
    pTouchRange  = apvts.getRawParameterValue ("touchRange");
    pTouchDepth  = apvts.getRawParameterValue ("touchDepth");
    pFollowTight = apvts.getRawParameterValue ("followTight");
    pChordMs     = apvts.getRawParameterValue ("chordMs");
    pSustainMax  = apvts.getRawParameterValue ("sustainMax");
    pFbHarmonic  = apvts.getRawParameterValue ("fbHarmonic");
    pFbRise      = apvts.getRawParameterValue ("fbRise");
    pFbTone      = apvts.getRawParameterValue ("fbTone");
    pModOn       = apvts.getRawParameterValue ("modOn");
    pSustainOn   = apvts.getRawParameterValue ("sustainOn");
    pDetuneOn    = apvts.getRawParameterValue ("detuneOn");
    pDryOn       = apvts.getRawParameterValue ("dryOn");
    pTouchOn     = apvts.getRawParameterValue ("touchOn");
    pFollowOn    = apvts.getRawParameterValue ("followOn");
    pFeedbackOn  = apvts.getRawParameterValue ("feedbackOn");
    pTremolo     = apvts.getRawParameterValue ("tremolo");
    pTremRate    = apvts.getRawParameterValue ("tremRate");
    pTremShape   = apvts.getRawParameterValue ("tremShape");
    pTremSyncOn  = apvts.getRawParameterValue ("tremSyncOn");
    pTremDiv     = apvts.getRawParameterValue ("tremDiv");
    pFbMode      = apvts.getRawParameterValue ("fbMode");
    pSpread      = apvts.getRawParameterValue ("spread");
    pSpreadMode  = apvts.getRawParameterValue ("spreadMode");
    pTremoloOn   = apvts.getRawParameterValue ("tremoloOn");
    pSpreadOn    = apvts.getRawParameterValue ("spreadOn");
    ex.bloomOn = apvts.getRawParameterValue ("bloomOn");
    ex.motionOn = apvts.getRawParameterValue ("motionOn");
    ex.voiceOn = apvts.getRawParameterValue ("voiceOn");
    ex.voiceMix = apvts.getRawParameterValue ("voiceMix");
    ex.voiceWave = apvts.getRawParameterValue ("voiceWave");
    ex.voiceOct = apvts.getRawParameterValue ("voiceOct");
    ex.voiceTone = apvts.getRawParameterValue ("voiceTone");
    ex.voiceUnison = apvts.getRawParameterValue ("voiceUnison");
    ex.voiceDrift = apvts.getRawParameterValue ("voiceDrift");
    ex.voiceDetune = apvts.getRawParameterValue ("voiceDetune");
    ex.voicePwmRate = apvts.getRawParameterValue ("voicePwmRate");
    ex.voicePhaser = apvts.getRawParameterValue ("voicePhaser");
    ex.voiceStab = apvts.getRawParameterValue ("voiceStab");
    pWarmth    = apvts.getRawParameterValue ("voiceWarmth");
    pVowel     = apvts.getRawParameterValue ("voiceVowel");
    pVowelPick = apvts.getRawParameterValue ("voiceVowelPick");
    pScoop     = apvts.getRawParameterValue ("voiceScoop");
    pBreath    = apvts.getRawParameterValue ("voiceBreath");
    pString = apvts.getRawParameterValue ("voiceString"); pLife = apvts.getRawParameterValue ("voiceLife");
    pVRes = apvts.getRawParameterValue ("voiceRes");      pVDecay = apvts.getRawParameterValue ("voiceDecay");
    pSmearThresh = apvts.getRawParameterValue ("smearThresh");
    pMidiOut = apvts.getRawParameterValue ("midiOut");
    pGlide = apvts.getRawParameterValue ("voiceGlide"); pMono = apvts.getRawParameterValue ("voiceMono");
    pVibHuman = apvts.getRawParameterValue ("vibHuman"); pCharacter = apvts.getRawParameterValue ("voiceCharacter");
    pVocalOn   = apvts.getRawParameterValue ("vocalOn");
    pSwellShape = apvts.getRawParameterValue ("swellShape");
    pSmoothRepick = apvts.getRawParameterValue ("smoothRepick");
    pEven = apvts.getRawParameterValue ("even");
    pBowedBody = apvts.getRawParameterValue ("bowedBody");
    pBodyIr = apvts.getRawParameterValue ("bodyIr"); pBodyMix = apvts.getRawParameterValue ("bodyMix");
    pDriveOn = apvts.getRawParameterValue ("driveOn"); pDriveType = apvts.getRawParameterValue ("driveType");
    pDriveAmt = apvts.getRawParameterValue ("driveAmt"); pDriveTone = apvts.getRawParameterValue ("driveTone");
    pDriveWow = apvts.getRawParameterValue ("driveWow");
    pSmearOn = apvts.getRawParameterValue ("smearOn");       pSmearLevel = apvts.getRawParameterValue ("smearLevel");
    pSmearSize = apvts.getRawParameterValue ("smearSize");   pSmearDrive = apvts.getRawParameterValue ("smearDrive");
    pSmearGate = apvts.getRawParameterValue ("smearGate");   pSmearDark = apvts.getRawParameterValue ("smearDark");
    pSmearHold = apvts.getRawParameterValue ("smearHold");   pSmearCut = apvts.getRawParameterValue ("smearCut");
    pSmearSend = apvts.getRawParameterValue ("smearSend");   pSmearPress = apvts.getRawParameterValue ("smearPress");
    pPhaserOn  = apvts.getRawParameterValue ("voicePhaserOn");
    ex.voiceChorus = apvts.getRawParameterValue ("voiceChorus");
    ex.vibDepth = apvts.getRawParameterValue ("vibDepth");
    ex.vibRate = apvts.getRawParameterValue ("vibRate");
    ex.vibDelay = apvts.getRawParameterValue ("vibDelay");
    ex.vibTarget = apvts.getRawParameterValue ("vibTarget");
    ex.voiceMixOn = apvts.getRawParameterValue ("voiceMixOn");
    ex.vibDepthOn = apvts.getRawParameterValue ("vibDepthOn");
    pWobble = apvts.getRawParameterValue ("detuneWobble");
    pPick = apvts.getRawParameterValue ("pickFilter");
    pPickOn = apvts.getRawParameterValue ("pickFilterOn");
    pUnisonOn = apvts.getRawParameterValue ("voiceUnisonOn");
    pDriftOn = apvts.getRawParameterValue ("voiceDriftOn");

    setLatencySamples (swello::kLatencySamples);
    setCurrentProgram (0);   // a new instance starts as Default (a saved session overrides it in setStateInformation)
    undo.clearUndoHistory();
}

void PolySwellProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    engineLL.prepare (sampleRate);
    const bool ll = pLowLatency->load() > 0.5f;
    lowLatencyActive = ll;
    detL.prepare (sampleRate);
    detR.prepare (sampleRate);
    limiter.prepare (sampleRate);
    voiceFx.prepare (sampleRate);
    smear.prepare (sampleRate);
    drive.prepare (sampleRate);
    const int maxBlock = juce::jmax (samplesPerBlock, 32);
    wetBuf.setSize (2, maxBlock); convBuf.setSize (2, maxBlock); dryBuf.setSize (1, maxBlock);
    bodyConv.prepare ({ sampleRate, (juce::uint32) maxBlock, 2 });
    loadedIr = -1; requestedIr = -1;   // (re)load the chosen IR after prepare
    bodyMixSmooth.reset (sampleRate, 0.05); bodyMixSmooth.setCurrentAndTargetValue (0.f);
    driveMixSmooth.reset (sampleRate, 0.03); driveMixSmooth.setCurrentAndTargetValue (0.f);
    smearLevelSmooth.reset (sampleRate, 0.03);
    smearLevelSmooth.setCurrentAndTargetValue (0.f);
    mixSmooth.reset (sampleRate, 0.03);
    mixSmooth.setCurrentAndTargetValue (0.f);
    volSmooth.reset (sampleRate, 0.02);
    volSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pVol->load()));
    bypassSmooth.reset (sampleRate, 0.015);
    bypassSmooth.setCurrentAndTargetValue (pBypass->load() > 0.5f ? 1.f : 0.f);
    drySmooth.reset (sampleRate, 0.02);
    drySmooth.setCurrentAndTargetValue (pDry->load());
    detuneSmooth.reset (sampleRate, 0.05);
    detuneSmooth.setCurrentAndTargetValue (pDetune->load());
    detuneMixSmooth.reset (sampleRate, 0.05);
    detuneMixSmooth.setCurrentAndTargetValue (detuneCopyLevel (apvts.getRawParameterValue ("detuneMix")->load()));
    setLatencySamples (ll ? swello::SwelloEngineLL::kLatencySamples : swello::SwelloEngine::kLatencySamples);
}

bool PolySwellProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return (in == juce::AudioChannelSet::mono()   || in  == juce::AudioChannelSet::stereo())
        && (out == juce::AudioChannelSet::mono()  || out == juce::AudioChannelSet::stereo());
}

void PolySwellProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiOut)
{
    juce::ScopedNoDenormals noDenormals;

    // MIDI in: learned CCs move parameters (the buffer is cleared further down; only tracked notes go out)
    for (const auto meta : midiOut)
    {
        const auto msg = meta.getMessage();
        if (msg.isController()) handleCc (msg.getControllerNumber(), msg.getControllerValue());
    }

    // host tempo (120 BPM if unknown), for SYNC and tremolo sync
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                if (*b > 20.0) bpm = *b;

    // attack time: knob, or a note value from the host tempo when SYNC is on
    float attackMs = pAttack->load();
    if (pSync->load() > 0.5f)
    {
        const float beats = divisionBeats ((int) std::lround (pSyncDiv->load()));
        attackMs = juce::jlimit (10.f, 8000.f, (float) (beats * 60000.0 / bpm));
    }

    // tremolo rate: knob, or locked to the tempo (fine-tune)
    float tremRate = pTremRate->load();
    // (PolySwell II has no tremolo sync control: tremSyncOn / tremDiv are kept for old sessions but not used)

    swello::Params p;
    p.attackMs   = attackMs;
    // a pedal switched off with its footswitch acts as if all its knobs were at their neutral values
    const bool bloom = true,   // PolySwell II has no BLOOM footswitch
               motion = ex.motionOn->load() > 0.5f, voice = ex.voiceOn->load() > 0.5f;
    const float bl = bloom ? 1.f : 0.f, mo = motion ? 1.f : 0.f;
    p.sustain    = effective (pSustain, pSustainOn, 0.f) * bl;
    p.freqHz     = pFreq->load();
    p.mod        = effective (pMod, pModOn, 0.f);
    p.slope      = juce::jlimit (0, 4, (int) std::lround (pSlope->load()));
    p.filterMode = pFilter->load() > 0.5f ? 1 : 0;   // on / off; one RES
    p.resLow     = pResLow->load();
    // PolySwell II: the gen-1 fine-tune values are fixed at their tuned defaults (no control for them)
    p.pickRangeDb  = 9.f;
    p.pickDepthOct = 4.f;
    p.touch      = effective (pTouch, pTouchOn, 0.f);
    p.follow     = effective (pFollow, pFollowOn, 0.f);
    p.feedback   = effective (pFeedback, pFeedbackOn, 0.f) * bl;
    p.touchRangeDb  = 12.f;
    p.touchDepthOct = 2.f;
    p.followTight   = 0.4f;
    p.chordGroupMs  = 60.f;
    p.sustainMaxDb  = pSustainMax->load();
    p.fbHarmonic    = juce::jlimit (0, 3, (int) std::lround (pFbHarmonic->load()));   // 3 = sub octave
    p.fbRiseSec     = pFbRise->load();
    p.fbToneHz      = pFbTone->load();
    p.tremDepth     = effective (pTremolo, pTremoloOn, 0.f) * mo;
    p.tremRateHz    = tremRate;
    p.tremShape     = juce::jlimit (0, 2, (int) std::lround (pTremShape->load()));
    p.spread        = effective (pSpread, pSpreadOn, 0.f) * mo;
    p.spreadMode    = juce::jlimit (0, 5, (int) std::lround (pSpreadMode->load()));
    const float voiceMix = voice ? effective (ex.voiceMix, ex.voiceMixOn, 0.f) : 0.f;
    p.voiceMix      = voiceMix;
    p.voiceWave     = juce::jlimit (0, 8, (int) std::lround (ex.voiceWave->load()));
    p.voiceGlide    = pGlide->load(); p.voiceMono = pMono->load() > 0.5f; p.vibHuman = pVibHuman->load() > 0.5f;
    p.voiceOct      = juce::jlimit (0, 2, (int) std::lround (ex.voiceOct->load()));
    p.voiceToneHz   = ex.voiceTone->load();
    // the voice controls PolySwell II does not show are neutral (CHARACTER drives the living ones)
    p.voicePwmRate  = 0.8f;
    p.voiceStab     = 0.f;
    p.voiceWarmth   = 0.f;
    p.voiceRes      = 0.f;   p.voiceDecay = 0.18f;
    const bool vocal = true;
    p.voiceVowel    = 0.f;
    p.voiceVowelPick = 0.5f;
    p.voiceScoop    = 0.f;
    p.voiceBreath   = 0.f;
    // CHARACTER (macro): from a clean synth to an alive, vocal one - adds STRING, per-note wander, a vowel, scoop and
    // breath on top of their own knobs. DRIFT now means each note wandering on its own (LIFE folded in).
    const float chr = pCharacter->load();
    const float drift = 0.f;
    p.voiceString   = juce::jlimit (0.f, 1.f, 0.6f * chr);
    p.voiceLife     = juce::jlimit (0.f, 1.f, 0.6f * chr);
    p.voiceScoop    = juce::jlimit (0.f, 1.f, p.voiceScoop + (vocal ? 0.3f * chr : 0.f));
    p.voiceBreath   = juce::jlimit (0.f, 1.f, p.voiceBreath + (vocal ? 0.25f * chr : 0.f));
    if (p.voiceVowel <= 0.001f && chr > 0.01f && vocal) p.voiceVowel = 0.05f + 0.45f * chr;   // "oh" .. "ah"
    p.midiOut       = (int) std::lround (pMidiOut->load());
    p.vibCents      = effective (ex.vibDepth, ex.vibDepthOn, 0.f) * mo;
    p.vibRateHz     = ex.vibRate->load();
    p.vibDelaySec   = ex.vibDelay->load();
    {   // parameter order Voice / Guitar + voice / Guitar -> engine 0 voice, 1 guitar, 2 both
        static constexpr int kMap[] = { 0, 2, 1 };
        p.vibTarget = kMap[juce::jlimit (0, 2, (int) std::lround (ex.vibTarget->load()))];
    }
    p.pickFilter    = effective (pPick, pPickOn, 0.f);
    p.swellShape    = (int) std::lround (pSwellShape->load());
    p.smoothRepick  = pSmoothRepick->load() > 0.5f;
    p.levelMatch    = apvts.getRawParameterValue ("levelMatch")->load() > 0.5f;
    p.fbAmp         = apvts.getRawParameterValue ("fbAmp")->load() > 0.5f;
    p.fbDistance    = apvts.getRawParameterValue ("fbDistance")->load();
    p.fbOneWins     = apvts.getRawParameterValue ("fbOneWins")->load() > 0.5f;
    p.uiTrack       = editorOpen.load (std::memory_order_relaxed);
    p.even          = pEven->load();
    p.bowedBody     = pBowedBody->load() > 0.5f && apvts.getRawParameterValue ("filterOn")->load() > 0.5f;   // BODY is part of FILTER

    swello::VoiceFx::Settings vfx;
    vfx.unison   = effective (ex.voiceUnison, pUnisonOn, 0.f);
    vfx.detune   = 15.f;
    vfx.drift    = 0.4f * drift;   // a little whole-voice wow; the main DRIFT is per note (engine)
    vfx.chorus   = 0;
    vfx.phaser   = 0.f;
    vfx.supersaw = p.voiceWave == 0;
    mixSmooth.setTargetValue (voiceMix);
    p.fbMode        = juce::jlimit (0, 1, (int) std::lround (pFbMode->load()));

    // low-latency switch: only the active engine runs (half the CPU). A set-and-forget setting, so a
    // switch simply starts the other engine fresh (a short gap/pop is accepted) and tells the host.
    const bool ll = pLowLatency->load() > 0.5f;
    if (ll != lowLatencyActive.load())
    {
        if (ll) engineLL.reset(); else engine.reset();
        lowLatencyActive = ll;
        triggerAsyncUpdate();
    }
    if (ll) engineLL.setParams (p); else engine.setParams (p);

    volSmooth.setTargetValue (juce::Decibels::decibelsToGain (pVol->load()));
    bypassSmooth.setTargetValue (pBypass->load() > 0.5f ? 1.f : 0.f);
    drySmooth.setTargetValue (effective (pDry, pDryOn, 0.f));   // DRY sits on the main pedal (not BLOOM)

    // SMEAR settings; the send follows the mode: always, each note's swell (like pressing the Igor pad as the
    // note blooms), or the PRESS knob (for an expression pedal / automation)
    const bool smearOn = pSmearOn->load() > 0.5f;
    if (smearOn && ! smearWasOn) smear.reset();
    smearWasOn = smearOn;
    smearLevelSmooth.setTargetValue (smearOn ? 2.5f * pSmearLevel->load() : 0.f);
    smearSizeF = std::exp2 (2.83f * (pSmearSize->load() - 0.5f));
    swello::Smear::Settings sms;
    sms.size = pSmearSize->load(); sms.drive = pSmearDrive->load();
    sms.gate = pSmearGate->load() > 0.5f; sms.dark = pSmearDark->load() > 0.5f;
    sms.holdMs = pSmearHold->load(); sms.cutMs = pSmearCut->load();
    sms.threshDb = pSmearThresh->load();
    float sendTarget = 1.f;
    const int sendMode = (int) std::lround (pSmearSend->load());
    if (sendMode == 1)
    {
        const auto note = latestNote();
        const float ph = juce::jlimit (0.f, 1.f, note.ageSec / juce::jmax (note.attackSec, 0.02f));
        sendTarget = note.seq == 0 ? 1.f : ph * ph;
    }
    else if (sendMode == 2) sendTarget = pSmearPress->load();
    const float sendStep = 1.f - std::exp (-1.f / (0.015f * (float) getSampleRate()));
    detuneSmooth.setTargetValue (effective (pDetune, pDetuneOn, 0.f) * mo);
    detuneMixSmooth.setTargetValue (detuneCopyLevel (apvts.getRawParameterValue ("detuneMix")->load()));   // DETUNE lives on the MOTION pedal
    const bool useLimiter = pLimiter->load() > 0.5f;
    const float wobble = 0.f;   // the POG2's detuned copies are steady (measured); no WOBBLE on PolySwell II
    const float sr = (float) getSampleRate();

    const int numIn  = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    const int numOut = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels());
    const int n      = buffer.getNumSamples();

    midiOut.clear();   // what came in was read above; the tracked notes go out (MIDI OUT)
    const bool llNow = lowLatencyActive.load();
    const uint64_t blockStart = llNow ? engineLL.samplesProcessed() : engine.samplesProcessed();
    float keyPeak = 0.f;
    if (n > wetBuf.getNumSamples())   // a host block bigger than announced: grow once (rare)
    {
        wetBuf.setSize (2, n, false, false, true); convBuf.setSize (2, n, false, false, true); dryBuf.setSize (1, n, false, false, true);
    }
    // ---- DRIVE settings
    swello::Drive::Settings dvs;
    dvs.type = (int) std::lround (pDriveType->load()); dvs.drive = pDriveAmt->load();
    dvs.tone = pDriveTone->load(); dvs.wow = pDriveWow->load();
    driveMixSmooth.setTargetValue (0.f);   // PolySwell II has no DRIVE (a preset or old session can not switch it on)

    // ---- pass 1: engine, voice, detune, DRIVE -> wetBuf (dry -> dryBuf). DRIVE comes before the BODY IR, like an amp
    //      before a cab: a driven, sustained string into a violin body is what makes it sound bowed.
    for (int i = 0; i < n; ++i)
    {
        float in = 0.f;                                   // mono sum (a guitar is mono)
        for (int c = 0; c < numIn; ++c) in += buffer.getReadPointer (c)[i];
        if (numIn > 1) in /= (float) numIn;

        float dry = 0.f, right = 0.f, vL = 0.f, vR = 0.f;
        const float left = ll ? engineLL.processStereo (in, dry, right, &vL, &vR)
                              : engine.processStereo (in, dry, right, &vL, &vR);
        const float vol  = volSmooth.getNextValue();
        const float wetL = left * vol, wetR = right * vol;   // differ only while SPREAD is up
        const float m    = mixSmooth.getNextValue();          // VOICE MIX: guitar <-> voice, equal power
        if (m > 0.0001f) voiceFx.process (vL, vR, vfx);

        // DETUNE: +cents on the left, -cents on the right, blended in under the swell
        const float cents = detuneSmooth.getNextValue();
        const float detuneMixNow = detuneMixSmooth.getNextValue();
        float wl = wetL, wr = wetR;
        // WOBBLE: the detune amount drifts slowly (left and right at different rates), so the doubled copy
        // breathes like a chorus instead of beating at a fixed, note-dependent speed
        float cL = cents, cR = cents;
        if (wobble > 0.f)
        {
            wobL += 0.23f / sr; if (wobL >= 1.f) wobL -= 1.f;
            wobR += 0.31f / sr; if (wobR >= 1.f) wobR -= 1.f;
            cL *= 1.f + 0.5f * wobble * std::sin (juce::MathConstants<float>::twoPi * wobL);
            cR *= 1.f + 0.5f * wobble * std::sin (juce::MathConstants<float>::twoPi * (wobR + 0.37f));
        }
        if (cents > 0.01f && detuneMixNow > 0.0005f)
        {
            const float up = detL.process (wetL, std::exp2 (cL / 1200.f));
            const float dn = detR.process (wetR, std::exp2 (-cR / 1200.f));
            // the DETUNE knob: how loud the copies are (0.5 = half each, the old fixed blend; 1 = only the copies, as a POG2 at the top)
            const float a  = detuneMixNow * juce::jmin (1.f, cents / 5.f);   // fades in over the first 5 cents
            wl = wetL * (1.f - a) + up * a;
            wr = wetR * (1.f - a) + dn * a;
        }
        else { detL.push (wetL); detR.push (wetR); }   // DETUNE at 0: only keep the delay lines filled (cheap)

        if (m > 0.0001f)
        {
            const float gG = std::cos (m * juce::MathConstants<float>::halfPi);
            const float gV = std::sin (m * juce::MathConstants<float>::halfPi) * vol;
            wl = wl * gG + vL * gV;
            wr = wr * gG + vR * gV;
        }

        const float dm = driveMixSmooth.getNextValue();
        if (dm > 0.0001f)
        {
            float dl = wl, dr = wr;
            drive.process (dl, dr, dvs);
            wl += (dl - wl) * dm; wr += (dr - wr) * dm;
        }
        wetBuf.setSample (0, i, wl); wetBuf.setSample (1, i, wr); dryBuf.setSample (0, i, dry);
    }

    // ---- BODY IR (violin bodies or your own): convolution on the wet block, blended by BODY MIX
    const int irChoice = (int) std::lround (pBodyIr->load());
    if (irChoice != requestedIr.load()) { requestedIr = irChoice; triggerAsyncUpdate(); }
    bodyMixSmooth.setTargetValue (irChoice > 0 && loadedIr == irChoice && apvts.getRawParameterValue ("filterOn")->load() > 0.5f ? pBodyMix->load() : 0.f);
    if (bodyMixSmooth.isSmoothing() || bodyMixSmooth.getTargetValue() > 0.f)
    {
        juce::dsp::AudioBlock<float> inB (wetBuf.getArrayOfWritePointers(), 2, (size_t) n), outB (convBuf.getArrayOfWritePointers(), 2, (size_t) n);
        bodyConv.process (juce::dsp::ProcessContextNonReplacing<float> (inB, outB));
        for (int i = 0; i < n; ++i)
        {
            const float bm = bodyMixSmooth.getNextValue();
            for (int c = 0; c < 2; ++c) wetBuf.setSample (c, i, wetBuf.getSample (c, i) * (1.f - bm) + convBuf.getSample (c, i) * bm);
        }
    }


    // ---- pass 2: SMEAR, dry, limiter, bypass
    for (int i = 0; i < n; ++i)
    {
        float wl = wetBuf.getSample (0, i), wr = wetBuf.getSample (1, i);
        const float dry = dryBuf.getSample (0, i);

        // SMEAR: reverb of the swelled sound (and the voice), gated by the playing
        const float smLvl = smearLevelSmooth.getNextValue();
        if (smearOn || smLvl > 0.0001f)
        {
            smearSendNow += (sendTarget - smearSendNow) * sendStep;
            sms.send = smearSendNow;
            float rl = 0.f, rr = 0.f;
            smear.process (wl, wr, dry, sms, rl, rr);
            // level-matched: the reverb adds energy (measured +1.6 dB at LEVEL .5, +7 dB at LEVEL 1 / SIZE 1), so the
            // sum is scaled back by about that much; LEVEL changes the balance, not the loudness (`ui_snapshot --levels`)
            const float L = smLvl * 0.4f, comp = 1.f / std::sqrt (1.f + 1.7f * L * L * smearSizeF);
            wl = (wl + rl * smLvl) * comp; wr = (wr + rr * smLvl) * comp;
        }

        // DRY: unswelled signal (pick attack) mixed in, same latency as the wet path
        const float d = dry * drySmooth.getNextValue();
        wl += d; wr += d;
        if (useLimiter) limiter.process (wl, wr);

        const float b = bypassSmooth.getNextValue();      // 1 = bypassed
        const float yl = dry * b + wl * (1.f - b);
        const float yr = dry * b + wr * (1.f - b);

        if (numOut >= 2) { buffer.getWritePointer (0)[i] = yl; buffer.getWritePointer (1)[i] = yr; }
        else if (numOut == 1) buffer.getWritePointer (0)[i] = 0.5f * (yl + yr);
        keyPeak = juce::jmax (keyPeak, std::abs (dry));
    }
    smearKeyDb.store (juce::Decibels::gainToDecibels (keyPeak, -100.f), std::memory_order_relaxed);

    // MPE: when MPE is switched on, tell the synth (MPE configuration message on channel 1: 15 member channels; member
    // channels 2..16 pitch-bend range 48 semitones via RPN 0), so it reads the per-note bends right
    const int midiMode = (int) std::lround (pMidiOut->load());
    if (midiMode == 2 && lastMidiMode != 2)
    {
        auto rpn = [&] (int ch, int num, int value)
        {
            midiOut.addEvent (juce::MidiMessage::controllerEvent (ch, 101, 0), 0);
            midiOut.addEvent (juce::MidiMessage::controllerEvent (ch, 100, num), 0);
            midiOut.addEvent (juce::MidiMessage::controllerEvent (ch, 6, value), 0);
            midiOut.addEvent (juce::MidiMessage::controllerEvent (ch, 38, 0), 0);
        };
        rpn (1, 6, 15);
        for (int ch = 2; ch <= 16; ++ch) rpn (ch, 0, 48);
    }
    lastMidiMode = midiMode;

    // MIDI OUT: the engine's note events, placed at the sample they happened
    auto drain = [&] (auto& eng)
    {
        typename std::decay_t<decltype (eng)>::MidiEv ev[128];
        const int ne = eng.popMidi (ev, 128);
        for (int k = 0; k < ne; ++k)
        {
            const int at = juce::jlimit (0, juce::jmax (0, n - 1), (int) (ev[k].sample - blockStart));
            const int ch = juce::jlimit (1, 16, (int) ev[k].ch);
            if (ev[k].type == 0)      midiOut.addEvent (juce::MidiMessage::noteOn (ch, ev[k].note, (juce::uint8) ev[k].vel), at);
            else if (ev[k].type == 1) midiOut.addEvent (juce::MidiMessage::noteOff (ch, ev[k].note), at);
            else                      midiOut.addEvent (juce::MidiMessage::pitchWheel (ch, ev[k].bend), at);
        }
    };
    if (llNow) drain (engineLL); else drain (engine);
}

// ---------------------------------------------------------------- presets
int PolySwellProcessor::getNumPrograms() { return (int) newPresets().size(); }
const juce::String PolySwellProcessor::getProgramName (int i)
{
    return newPresets()[(size_t) juce::jlimit (0, (int) newPresets().size() - 1, i)].name;
}
juce::String PolySwellProcessor::programTag (int i)
{
    return newPresets()[(size_t) juce::jlimit (0, (int) newPresets().size() - 1, i)].tag;
}
void PolySwellProcessor::applyNewPreset (int index) { setCurrentProgram (index); }

// ---------------------------------------------------------------- MIDI learn
void PolySwellProcessor::learnCc (const juce::String& id)
{
    for (int i = 0; i < ccParams.size(); ++i) if (ccParams[i]->getParameterID() == id) { ccLearn = i; return; }
}
void PolySwellProcessor::forgetCc (const juce::String& id)
{
    for (int c = 0; c < 128; ++c) { const int i = ccMap[(size_t) c].load(); if (i >= 0 && ccParams[i]->getParameterID() == id) ccMap[(size_t) c] = -1; }
    if (isLearning (id)) ccLearn = -1;
}
int PolySwellProcessor::ccFor (const juce::String& id) const
{
    for (int c = 0; c < 128; ++c) { const int i = ccMap[(size_t) c].load(); if (i >= 0 && ccParams[i]->getParameterID() == id) return c; }
    return -1;
}
bool PolySwellProcessor::isLearning (const juce::String& id) const
{
    const int i = ccLearn.load();
    return i >= 0 && ccParams[i]->getParameterID() == id;
}

// audio thread. Knobs follow the CC; ON and (latched) HOLD flip when a footswitch is pressed; other switches follow it.
void PolySwellProcessor::handleCc (int cc, int value)
{
    if (cc < 0 || cc > 127) return;
    const int learn = ccLearn.load();
    if (learn >= 0)
    {
        for (auto& m : ccMap) if (m.load() == learn) m = -1;
        ccMap[(size_t) cc] = learn; ccLearn = -1; ccLast[(size_t) cc] = value;
        return;
    }
    const int idx = ccMap[(size_t) cc].load();
    const int last = ccLast[(size_t) cc];
    ccLast[(size_t) cc] = value;
    if (idx < 0) return;
    auto* prm = ccParams[idx];
    const auto id = prm->getParameterID();
    const bool pressed = value >= 64 && last < 64;
    if (dynamic_cast<juce::AudioParameterBool*> (prm) != nullptr)
    {
        const bool flips = id == "bypass";
        if (flips) { if (pressed) prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.f : 1.f); }
        else prm->setValueNotifyingHost (value >= 64 ? 1.f : 0.f);
    }
    else prm->setValueNotifyingHost ((float) value / 127.f);
}

void PolySwellProcessor::setParam (const char* id, float v)
{
    if (auto* prm = apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
        prm->endChangeGesture();
    }
}

void PolySwellProcessor::setCurrentProgram (int i)
{
    i = juce::jlimit (0, getNumPrograms() - 1, i);
    // Default: every parameter at its default (bypass, limiter and low latency belong to the setup and stay)
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            const auto id = rp->getParameterID();
            if (id == "bypass" || id == "limiter" || id == "lowLatency" || id == "filter" || id == "resLow" || id == "resHigh") continue;
            rp->setValueNotifyingHost (rp->getDefaultValue());
        }
    gen2Defaults();
    for (auto& kv : defaultSetup()) setParam (kv.first, kv.second);
    const auto& np = newPresets()[(size_t) i];
    for (auto& kv : np.set) setParam (kv.first, kv.second);
    currentProgram = i;
    currentPresetName = np.name;
    userPresetLoaded = false;
}

// ---------------------------------------------------------------- user presets
// Saved as small XML files in Documents/PolySwell/Presets. They hold every parameter except bypass, limiter
// and low latency (those belong to the session / setup, not to a sound).
static bool isSetupParam (const juce::String& id) { return id == "bypass" || id == "limiter" || id == "lowLatency"; }
static bool isOldParam (const juce::String& id)   { return id == "filter" || id == "resLow" || id == "resHigh"; }

// sessions / presets from before the filter became on/off + one RES: convert the old 3-way switch
void PolySwellProcessor::convertOldFilter()
{
    const int mode = (int) std::lround (apvts.getRawParameterValue ("filter")->load());
    setParam ("filterOn", mode > 0 ? 1.f : 0.f);
    setParam ("res", mode == 2 ? apvts.getRawParameterValue ("resHigh")->load() : apvts.getRawParameterValue ("resLow")->load());
}

juce::File PolySwellProcessor::userPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("PolySwell").getChildFile ("Presets");
}

juce::Array<juce::File> PolySwellProcessor::userPresets()
{
    auto files = userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.polyswell");
    files.sort();
    return files;
}

bool PolySwellProcessor::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    juce::XmlElement xml ("POLYSWELLPRESET");
    xml.setAttribute ("name", name.trim());
    for (auto* prm : getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (! isSetupParam (p->getParameterID()) && ! isOldParam (p->getParameterID()))
            {
                auto* e = xml.createNewChildElement ("PARAM");
                e->setAttribute ("id", p->getParameterID());
                e->setAttribute ("value", (double) p->convertFrom0to1 (p->getValue()));
            }
    userPresetFolder().createDirectory();
    const auto file = userPresetFolder().getChildFile (clean + ".polyswell");
    if (! xml.writeTo (file)) return false;
    currentPresetName = name.trim(); userPresetLoaded = true;
    return true;
}

bool PolySwellProcessor::loadUserPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("POLYSWELLPRESET")) return false;
    bool hasNewFilter = false;
    for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
    {
        const auto id = e->getStringAttribute ("id");
        if (isSetupParam (id)) continue;
        hasNewFilter = hasNewFilter || id == "filterOn";
        if (auto* p = apvts.getParameter (id)) setParam (id.toRawUTF8(), (float) e->getDoubleAttribute ("value"));
    }
    if (! hasNewFilter) convertOldFilter();
    currentPresetName = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
    gen2Defaults();
    userPresetLoaded = true;
    return true;
}

// PolySwell II has no per-knob off switches and no BLOOM footswitch: an old session or preset must not leave one off
void PolySwellProcessor::gen2Defaults()
{
    for (const auto& s : switchables()) if (juce::String (s.onId) != "feedbackOn") setParam (s.onId, 1.f);
    setParam ("bloomOn", 1.f);
}

juce::AudioProcessorEditor* PolySwellProcessor::createEditor() { return new PolySwellEditor (*this); }

void PolySwellProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    state.setProperty ("customIr", customIr.getFullPathName(), nullptr);
    state.setProperty ("uiVoiceMore", uiVoiceMore, nullptr);
    state.setProperty ("presetName", currentPresetName, nullptr);
    state.setProperty ("userPreset", userPresetLoaded, nullptr);
    state.setProperty ("uiPedalsOpen", uiPedalsOpen, nullptr);
    state.setProperty ("uiScale", uiScale, nullptr);
    state.setProperty ("uiSkin", uiSkin, nullptr);
    state.setProperty ("uiWindowNotes", uiWindowNotes, nullptr);
    juce::StringArray ccs;
    for (int c = 0; c < 128; ++c) if (const int k = ccMap[(size_t) c].load(); k >= 0) ccs.add (juce::String (c) + ":" + ccParams[k]->getParameterID());
    state.setProperty ("ccMap", ccs.joinIntoString (","), nullptr);
    for (int i = 0; i < 3; ++i)
    {
        state.setProperty ("uiFold" + juce::String (i), uiFold[(size_t) i], nullptr);
        state.setProperty ("uiRow" + juce::String (i), uiOpenRow[(size_t) i], nullptr);
    }
    state.setProperty ("uiFold3", uiFold[3], nullptr);
    state.setProperty ("uiFold4", uiFold[4], nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void PolySwellProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) vt.getProperty ("program", 0));
            if (const auto ir = vt.getProperty ("customIr", "").toString(); ir.isNotEmpty()) customIr = juce::File (ir);
            uiVoiceMore = (bool) vt.getProperty ("uiVoiceMore", false);
            currentPresetName = vt.getProperty ("presetName", getProgramName (currentProgram)).toString();
            for (auto& m : ccMap) m = -1;
            for (auto pair : juce::StringArray::fromTokens (vt.getProperty ("ccMap", "").toString(), ",", ""))
            {
                const int cc = pair.upToFirstOccurrenceOf (":", false, false).getIntValue();
                const auto id = pair.fromFirstOccurrenceOf (":", false, false);
                for (int k = 0; k < ccParams.size(); ++k) if (cc >= 0 && cc < 128 && ccParams[k]->getParameterID() == id) ccMap[(size_t) cc] = k;
            }
            userPresetLoaded = (bool) vt.getProperty ("userPreset", false);
            uiPedalsOpen = (bool) vt.getProperty ("uiPedalsOpen", true);
            uiScale = juce::jlimit (0.75f, 2.f, (float) (double) vt.getProperty ("uiScale", 1.0));
            uiSkin = juce::jlimit (0, 3, (int) vt.getProperty ("uiSkin", 0));
            uiWindowNotes = (bool) vt.getProperty ("uiWindowNotes", false);
            for (int i = 0; i < 3; ++i)
            {
                uiFold[(size_t) i]    = (bool) vt.getProperty ("uiFold" + juce::String (i), false);
                uiOpenRow[(size_t) i] = (int) vt.getProperty ("uiRow" + juce::String (i), -1);
            }
            uiFold[3] = (bool) vt.getProperty ("uiFold3", false);
            uiFold[4] = (bool) vt.getProperty ("uiFold4", false);
            const bool hasNewFilter = vt.getChildWithProperty ("id", "filterOn").isValid();
            apvts.replaceState (vt);
            if (! hasNewFilter) convertOldFilter();
            gen2Defaults();
        }
}

void PolySwellProcessor::loadBodyIr (int index)
{
    struct Ir { const char* data; int size; };
    const Ir irs[] = { { nullptr, 0 },
        { BinaryData::violin_a_wav, BinaryData::violin_a_wavSize },   { BinaryData::violin_en_wav, BinaryData::violin_en_wavSize },
        { BinaryData::violin_eu_wav, BinaryData::violin_eu_wavSize }, { BinaryData::violin_g1_wav, BinaryData::violin_g1_wavSize },
        { BinaryData::violin_g2_wav, BinaryData::violin_g2_wavSize }, { BinaryData::violin_s1_wav, BinaryData::violin_s1_wavSize },
        { BinaryData::violin_s2_wav, BinaryData::violin_s2_wavSize } };
    using C = juce::dsp::Convolution;
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader;
    if (index >= 1 && index <= 7)
        reader.reset (fm.createReaderFor (std::make_unique<juce::MemoryInputStream> (irs[index].data, (size_t) irs[index].size, false)));
    else if (index == 8 && customIr.existsAsFile())
        reader.reset (fm.createReaderFor (customIr));
    if (reader == nullptr || reader->lengthInSamples <= 0) { loadedIr = 0; return; }
    const int len = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 2.0));   // max 2 s
    const int chans = juce::jlimit (1, 2, (int) reader->numChannels);
    juce::AudioBuffer<float> ir (chans, len);
    reader->read (&ir, 0, len, 0, true, chans > 1);
    // unity energy (white-noise gain 0 dB): the body colours the sound without changing its level much
    double e = 0.0;
    for (int c = 0; c < chans; ++c) for (int i = 0; i < len; ++i) e += (double) ir.getSample (c, i) * ir.getSample (c, i);
    e /= (double) chans;
    if (e > 1.0e-12) ir.applyGain ((float) (1.0 / std::sqrt (e)));
    bodyConv.loadImpulseResponse (std::move (ir), reader->sampleRate, chans > 1 ? C::Stereo::yes : C::Stereo::no,
                                  index == 8 ? C::Trim::yes : C::Trim::no, C::Normalise::no);
    loadedIr = index;
}

void PolySwellProcessor::setCustomIr (const juce::File& f)
{
    customIr = f;
    if (auto* p = apvts.getParameter ("bodyIr")) p->setValueNotifyingHost (p->convertTo0to1 (8.f));
    loadBodyIr (8);
}

void PolySwellProcessor::handleAsyncUpdate()
{
    if (const int want = requestedIr.load(); want >= 0 && want != loadedIr) loadBodyIr (want);
    setLatencySamples (lowLatencyActive.load() ? swello::SwelloEngineLL::kLatencySamples
                                               : swello::SwelloEngine::kLatencySamples);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PolySwellProcessor(); }
