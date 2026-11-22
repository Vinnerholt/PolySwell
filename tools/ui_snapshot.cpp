#include <cstdio>
#include <algorithm>
#include <chrono>
#include <functional>
// Dev tool: renders the editor to PNG files without a DAW.
// Build: cmake --build build --config Release --target ui_snapshot ; run: ui_snapshot.exe <outdir>
// A plucked chord is fed through the processor first, so the live spectrum has something to show.
#include "PluginProcessor.h"
#include "PluginEditor.h"

static void setParam (PolySwellProcessor& p, const char* id, float v)
{
    if (auto* prm = p.apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

static void shoot (juce::Component& c, const juce::File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
}

// play `seconds` of an A minor chord (plucked at t = 0) through the processor
static void feedChord (PolySwellProcessor& proc, double seconds, double& t)
{
    constexpr double sr = 48000.0, pi = 3.14159265358979323846;
    const double f0[] = { 110.0, 164.8, 220.0, 261.6, 329.6 };
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (int blocks = (int) (seconds * sr / 512); blocks > 0; --blocks)
    {
        for (int i = 0; i < 512; ++i, t += 1.0 / sr)
        {
            double v = 0;
            for (double f : f0)
                for (int h = 1; h <= 6; ++h) v += 0.05 / h * std::sin (2 * pi * f * h * t) * std::exp (-t * (1.5 + h * 0.6));
            buf.setSample (0, i, (float) v); buf.setSample (1, i, (float) v);
        }
        proc.processBlock (buf, midi);
    }
}

// --bench: the whole processor (engine + voice effects + detune + SMEAR + limiter) at the owner's setting,
// 128 samples @ 44.1 kHz, timing every block. Strummed chords every 1.5 s.
static void benchCase (const char* name, std::function<void (PolySwellProcessor&)> setup)
{
    constexpr double sr = 44100.0, pi = 3.14159265358979323846;
    constexpr int block = 128;
    PolySwellProcessor proc;
    proc.setPlayConfigDetails (2, 2, sr, block);
    proc.prepareToPlay (sr, block);
    setup (proc);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    const double f0[] = { 82.4, 123.5, 164.8, 207.7, 246.9, 329.6 };
    const int total = (int) (sr * 9.0) / block;
    std::vector<double> us;
    double t = 0.0;
    for (int b = 0; b < total; ++b)
    {
        for (int i = 0; i < block; ++i, t += 1.0 / sr)
        {
            const double tc = std::fmod (t, 1.5);
            double v = 0;
            for (int s = 0; s < 6; ++s)
            {
                const double ts = tc - s * 0.012;
                if (ts < 0) continue;
                for (int h = 1; h <= 6; ++h) v += 0.04 / h * std::sin (2 * pi * f0[s] * h * t) * std::exp (-ts * (1.2 + h * 0.4));
            }
            buf.setSample (0, i, (float) v); buf.setSample (1, i, (float) v);
        }
        const auto a = std::chrono::high_resolution_clock::now();
        proc.processBlock (buf, midi);
        us.push_back (std::chrono::duration<double, std::micro> (std::chrono::high_resolution_clock::now() - a).count());
    }
    us.erase (us.begin(), us.begin() + 40);   // warm-up
    const double budget = block / sr * 1e6;
    double sum = 0, worst = 0; int over25 = 0;
    for (double u : us) { sum += u; worst = std::max (worst, u); if (u > 0.25 * budget) ++over25; }
    std::vector<double> sorted (us); std::sort (sorted.begin(), sorted.end());
    std::printf ("  %-34s avg %5.1f %%  p99 %5.0f us  worst %5.0f us (%4.0f %% of %4.0f us)  blocks > 25 %%: %d\n",
                 name, 100.0 * sum / (double) us.size() / budget, sorted[(size_t) (0.99 * (double) sorted.size())], worst,
                 100.0 * worst / budget, budget, over25);
}

static void runBench()
{
    auto P = [] (PolySwellProcessor& p, std::initializer_list<std::pair<const char*, float>> kv) { for (auto& k : kv) setParam (p, k.first, k.second); };
    std::printf ("Whole plugin, 128 samples @ 44.1 kHz (budget 2902 us per block), strummed 6-string chords:\n");
    benchCase ("swell only (Init)", [] (PolySwellProcessor&) {});
    benchCase ("+ filter + mod", [P] (PolySwellProcessor& p) { P (p, { { "filterOn", 1 }, { "mod", 0.5f } }); });
    benchCase ("+ bloom (sustain, feedback)", [P] (PolySwellProcessor& p) { P (p, { { "sustain", 0.5f }, { "feedback", 0.6f } }); });
    benchCase ("+ motion (trem, spread pitch, vib, detune)", [P] (PolySwellProcessor& p)
        { P (p, { { "tremolo", 0.4f }, { "spread", 0.8f }, { "spreadMode", 2 }, { "vibDepth", 15 }, { "detune", 8 } }); });
    benchCase ("+ voice (saw)", [P] (PolySwellProcessor& p) { P (p, { { "voiceMix", 0.5f } }); });
    benchCase ("+ voice vocal (vowel, scoop, breath)", [P] (PolySwellProcessor& p)
        { P (p, { { "voiceMix", 0.5f }, { "voiceVowel", 0.5f }, { "voiceScoop", 0.5f }, { "voiceBreath", 0.5f }, { "voiceWarmth", 0.5f } }); });
    benchCase ("+ voice unison full + chorus + phaser", [P] (PolySwellProcessor& p)
        { P (p, { { "voiceMix", 0.5f }, { "voiceUnison", 1 }, { "voiceChorus", 3 }, { "voicePhaser", 0.6f } }); });
    benchCase ("+ voice BOW, glide, mono, human vib", [P] (PolySwellProcessor& p)
        { P (p, { { "voiceMix", 0.5f }, { "voiceWave", 8 }, { "voiceGlide", 0.12f }, { "voiceMono", 1 }, { "vibHuman", 1 }, { "vibDepth", 15 } }); });
    benchCase ("+ smear (gate, drive)", [P] (PolySwellProcessor& p) { P (p, { { "smearOn", 1 }, { "smearGate", 1 }, { "smearDrive", 0.4f } }); });
    benchCase ("Bowed (VG-8) preset", [] (PolySwellProcessor& p) { p.setCurrentProgram (p.getNumPrograms() - 1); });
    benchCase ("EVERYTHING on", [P] (PolySwellProcessor& p)
        { P (p, { { "filterOn", 1 }, { "mod", 0.5f }, { "sustain", 0.5f }, { "feedback", 0.6f }, { "tremolo", 0.4f }, { "spread", 0.8f },
                  { "spreadMode", 2 }, { "vibDepth", 15 }, { "vibTarget", 1 }, { "detune", 8 }, { "voiceMix", 0.5f }, { "voiceVowel", 0.5f },
                  { "voiceScoop", 0.5f }, { "voiceBreath", 0.5f }, { "voiceUnison", 1 }, { "voiceChorus", 3 }, { "voicePhaser", 0.6f },
                  { "smearOn", 1 }, { "smearGate", 1 }, { "smearDrive", 0.4f }, { "bowedBody", 1 }, { "smoothRepick", 1 } }); });
}

// --levels: output loudness (RMS dB over strummed chords) for a few settings, e.g. how much SMEAR adds
static double rmsDb (std::function<void (PolySwellProcessor&)> setup)
{
    constexpr double sr = 44100.0, pi = 3.14159265358979323846;
    constexpr int block = 128;
    PolySwellProcessor proc;
    proc.setPlayConfigDetails (2, 2, sr, block);
    proc.prepareToPlay (sr, block);
    setup (proc);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    const double f0[] = { 82.4, 123.5, 164.8, 207.7, 246.9, 329.6 };
    double t = 0.0, e = 0.0; long cnt = 0;
    for (int b = 0; b < (int) (sr * 9.0) / block; ++b)
    {
        for (int i = 0; i < block; ++i, t += 1.0 / sr)
        {
            const double tc = std::fmod (t, 3.0);
            double v = 0;
            for (int s = 0; s < 6; ++s)
            {
                const double ts = tc - s * 0.012;
                if (ts < 0) continue;
                for (int h = 1; h <= 6; ++h) v += 0.04 / h * std::sin (2 * pi * f0[s] * h * t) * std::exp (-ts * (1.2 + h * 0.4));
            }
            buf.setSample (0, i, (float) v); buf.setSample (1, i, (float) v);
        }
        proc.processBlock (buf, midi);
        if (t > 3.0) for (int i = 0; i < block; ++i) { const double l = buf.getSample (0, i), r = buf.getSample (1, i); e += 0.5 * (l * l + r * r); ++cnt; }
    }
    return 10.0 * std::log10 (e / (double) cnt + 1e-20);
}

static void runLevels()
{
    auto P = [] (std::initializer_list<std::pair<const char*, float>> kv)
    { std::vector<std::pair<const char*, float>> v (kv); return [v] (PolySwellProcessor& p) { for (auto& k : v) setParam (p, k.first, k.second); }; };
    const double ref = rmsDb (P ({}));
    std::printf ("Output RMS (strummed chords every 3 s), relative to Init (%.1f dB):\n", ref);
    const std::pair<const char*, std::function<void (PolySwellProcessor&)>> cases[] = {
        { "SMEAR level .25", P ({ { "smearOn", 1 }, { "smearLevel", 0.25f } }) },
        { "SMEAR level .5 (default)", P ({ { "smearOn", 1 } }) },
        { "SMEAR level 1", P ({ { "smearOn", 1 }, { "smearLevel", 1.f } }) },
        { "SMEAR level .5 + GATE", P ({ { "smearOn", 1 }, { "smearGate", 1 } }) },
        { "SMEAR level 1, size 1", P ({ { "smearOn", 1 }, { "smearLevel", 1.f }, { "smearSize", 1.f } }) },
    };
    for (auto& c : cases) std::printf ("  %-28s %+5.1f dB\n", c.first, rmsDb (c.second) - ref);
}

// --peaks: each preset's output peak over strummed chords with the limiter off (> -0.3 dBFS = the limiter would act)
static void runPeaks()
{
    constexpr double sr = 44100.0, pi = 3.14159265358979323846;
    constexpr int block = 128;
    const double f0[] = { 82.4, 123.5, 164.8, 207.7, 246.9, 329.6 };
    const int programs = std::make_unique<PolySwellProcessor>()->getNumPrograms();   // (on the heap: the engines are large)
    for (int pr = 0; pr < programs; ++pr)
    {
        auto procPtr = std::make_unique<PolySwellProcessor>();
        auto& proc = *procPtr;
        proc.setPlayConfigDetails (2, 2, sr, block);
        proc.prepareToPlay (sr, block);
        proc.setCurrentProgram (pr);
        setParam (proc, "limiter", 0.f);
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        double t = 0.0, peak = 0.0, inPeak = 0.0; long over = 0, n = 0;
        for (int b = 0; b < (int) (sr * 8.0) / block; ++b)
        {
            for (int i = 0; i < block; ++i, t += 1.0 / sr)
            {
                const double tc = std::fmod (t, 2.0);
                double v = 0;
                for (int s = 0; s < 6; ++s)
                {
                    const double ts = tc - s * 0.012;
                    if (ts < 0) continue;
                    for (int h = 1; h <= 6; ++h) v += 0.05 / h * std::sin (2 * pi * f0[s] * h * t) * std::exp (-ts * (1.2 + h * 0.4));
                }
                inPeak = std::max (inPeak, std::abs (v));
                buf.setSample (0, i, (float) v); buf.setSample (1, i, (float) v);
            }
            proc.processBlock (buf, midi);
            for (int i = 0; i < block; ++i)
            {
                const double p = std::max (std::abs (buf.getSample (0, i)), std::abs (buf.getSample (1, i)));
                peak = std::max (peak, p); over += p > 0.97 ? 1 : 0; ++n;
            }
        }
        std::printf ("  %-22s peak %+6.1f dBFS (input %+5.1f)  over the limiter %5.2f %%\n", proc.getProgramName (pr).toRawUTF8(),
                     20 * std::log10 (peak + 1e-9), 20 * std::log10 (inPeak), 100.0 * (double) over / (double) n);
    }
}

static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 1 && juce::String (argv[1]) == "--bench") { runBench(); return 0; }
    if (argc > 1 && juce::String (argv[1]) == "--levels") { runLevels(); return 0; }
    if (argc > 1 && juce::String (argv[1]) == "--peaks") { runPeaks(); return 0; }
    if (argc > 2 && juce::String (argv[2]) == "--fonts")
    {
        for (auto& st : juce::Font::findAllTypefaceStyles ("Bahnschrift")) std::printf ("style: %s\n", st.toRawUTF8());
        return 0;
    }
    const juce::File out (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    out.createDirectory();

    PolySwellProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    setParam (proc, "attack", 800.f);
    setParam (proc, "filterOn", 1.f); setParam (proc, "mod", 0.5f); setParam (proc, "freq", 2500.f);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto& e = *ed;
    auto* pe = dynamic_cast<PolySwellEditor*> (&e);
    double t = 0;

    feedChord (proc, 0.6, t); pump (300);
    shoot (e, out.getChildFile ("1_default.png"));

    // a sound with most things in use, one module off, the pad held
    setParam (proc, "sustain", 0.5f); setParam (proc, "feedback", 0.6f); setParam (proc, "touch", 0.4f);
    setParam (proc, "vibDepth", 14.f); setParam (proc, "spread", 0.7f); setParam (proc, "detune", 8.f);
    setParam (proc, "voiceMix", 0.4f); setParam (proc, "voiceWave", 8.f); setParam (proc, "voiceGlide", 0.08f);
    setParam (proc, "smearOn", 1.f); setParam (proc, "motionOn", 1.f); setParam (proc, "bodyIr", 1.f);
    setParam (proc, "pickFilter", 0.4f); setParam (proc, "res", 0.3f);
    for (int rep = 0; rep < 3; ++rep) { t = 0; feedChord (proc, 0.9, t); pump (900); }
    setParam (proc, "holdOn", 1.f);
    t = 0; feedChord (proc, 0.5, t); pump (500);
    shoot (e, out.getChildFile ("2_in_use.png"));

    setParam (proc, "smearOn", 0.f); setParam (proc, "voiceOn", 0.f); setParam (proc, "filterOn", 0.f); setParam (proc, "holdOn", 0.f);
    pump (300);
    shoot (e, out.getChildFile ("3_modules_off.png"));

    if (pe != nullptr) pe->setPanelOpen (false);
    pump (300);
    shoot (e, out.getChildFile ("4_pedal_only.png"));

    if (pe != nullptr) { pe->setPanelOpen (true); pe->setUiScale (1.5f); }
    setParam (proc, "filterOn", 1.f); setParam (proc, "voiceOn", 1.f);
    pump (300);
    shoot (e, out.getChildFile ("5_size_150.png"));

    // the window explaining a knob (as when the mouse is over it)
    if (pe != nullptr)
    {
        pe->setUiScale (1.f);
        setParam (proc, "attack", 900.f); setParam (proc, "follow", 0.6f); setParam (proc, "mod", 0.55f); setParam (proc, "freq", 2500.f);
        setParam (proc, "bypass", 0.f); setParam (proc, "levelMatch", 1.f);
        // the live view: a chord 0.7 s into a 0.9 s swell
        setParam (proc, "holdOn", 0.f); setParam (proc, "feedback", 0.f);
        pump (1200);
        t = 0;
        for (int i = 0; i < 14; ++i) { feedChord (proc, 0.05, t); pump (40); }   // audio running while the window animates
        shoot (e, out.getChildFile ("14_live.png"));
        const char* ids[] = { "attack", "sustain", "touch", "follow", "feedback", "fbDistance", "vibDepth", "spread", "voiceMix", "holdFade", "smearSize", "levelMatch" };
        int n = 6;
        for (auto* id : ids)
        {
            pe->showKnobView (id, true); pump (500);
            shoot (e, out.getChildFile (juce::String (n++) + "_view_" + id + ".png"));
            pe->showKnobView (id, false);
        }
    }

    ed.reset();
    proc.releaseResources();
    return 0;
}
