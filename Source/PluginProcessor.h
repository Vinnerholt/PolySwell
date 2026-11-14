#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/SwelloEngine.h"
#include "dsp/Detuner.h"
#include "dsp/Limiter.h"
#include "dsp/VoiceFx.h"
#include "dsp/Smear.h"
#include "dsp/Drive.h"
#include "dsp/Hold.h"
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>

class PolySwellProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    PolySwellProcessor();
    ~PolySwellProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PolySwell II"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }    // MIDI out of the tracked notes (midiOut)
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::UndoManager undo { 30000, 60 };   // knob moves in the editor (UNDO button); must be built before apvts
    juce::AudioProcessorValueTreeState apvts;

    // 1/16, 1/8, 1/4, 1/2, 1 bar, 2 bars (length in beats)
    static float divisionBeats (int idx);
    static juce::StringArray tremDivNames();
    static double tremDivBeats (int idx);   // length of one tremolo cycle in beats

    // live spectrum for the editor (lock-free, see SwelloEngine::readSpectrum)
    static constexpr int kSpecBands = swello::SwelloEngine::kSpecBands;
    uint32_t readSpectrum (float* in, float* out) const
    { return lowLatencyActive.load() ? engineLL.readSpectrum (in, out) : engine.readSpectrum (in, out); }
    int readVoiceNotes (swello::SwelloEngine::VoiceNote* v) const
    {
        if (lowLatencyActive.load())
        {
            swello::SwelloEngineLL::VoiceNote t[swello::SwelloEngine::kVoicePub];
            const int n = engineLL.readVoiceNotes (t);
            for (int i = 0; i < n; ++i) v[i] = { t[i].hz, t[i].level, t[i].timeSec, t[i].scoop, t[i].vowel };
            return n;
        }
        return engine.readVoiceNotes (v);
    }
    void readVoiceSpectrum (float* v) const
    { if (lowLatencyActive.load()) engineLL.readVoiceSpectrum (v); else engine.readVoiceSpectrum (v); }
    swello::SwelloEngine::NoteInfo latestNote() const
    {
        if (lowLatencyActive.load())
        {
            const auto n = engineLL.latestNote();
            return { n.seq, n.ageSec, n.attackSec, n.strength, n.pan, n.pickStrength };
        }
        return engine.latestNote();
    }

    // Knobs that can be switched off with a right-click: { value id, on/off id, neutral value }
    struct Switchable { const char* id; const char* onId; float neutral; };
    static const std::vector<Switchable>& switchables();

    // user presets (Documents/PolySwell/Presets/*.polyswell) and the name shown in the preset bar
    static juce::File userPresetFolder();
    static juce::Array<juce::File> userPresets();
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::File& file);
    juce::String currentPresetName { "Init" };
    bool userPresetLoaded = false;

    // your own body IR (BODY > Load IR...), remembered with the session
    juce::File customIr;
    void setCustomIr (const juce::File& f);
    bool uiVoiceMore = false;

    // editor layout, remembered with the session (effect pedals slid out, folded pedals, open rows)
    bool uiPedalsOpen = true;
    float uiScale = 1.f;                    // editor size (0.75 .. 2)
    std::atomic<float> holdLevelNow { 0.f };   // HOLD pad fade 0..1 (for the UI)
    std::array<bool, 5> uiFold { false, false, false, false, false };   // BLOOM, MOTION, VOICE, SMEAR, DRIVE
    std::array<int, 3>  uiOpenRow { -1, -1, -1 };

private:
    void setParam (const char* id, float plainValue);

    void handleAsyncUpdate() override;      // reports a latency change to the host (message thread)
    float effective (std::atomic<float>* value, std::atomic<float>* on, float neutral) const
    { return on->load() > 0.5f ? value->load() : neutral; }

    swello::SwelloEngine   engine;          // 1024-point FFT
    swello::SwelloEngineLL engineLL;        // 512-point FFT (low latency); only the active engine runs
    std::atomic<bool> lowLatencyActive { false };
    swello::Detuner detL, detR;
    swello::Limiter limiter;
    swello::VoiceFx voiceFx;                // unison / drift / chorus / phaser on the voice only
    swello::Smear smear;                    // SMEAR: gated reverb + drive, after the swell and the voice
    swello::Hold hold;                      // HOLD: spectral freeze of what is ringing, added after SMEAR
    std::atomic<float> *pHoldOn = nullptr, *pHoldFade = nullptr, *pHoldLevel = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> holdLevelSmooth;
    swello::Drive drive;                    // DRIVE: tape / tube saturation, after the body IR, before SMEAR
    juce::dsp::Convolution bodyConv { juce::dsp::Convolution::NonUniform { 256 } };   // BODY IR (zero latency)
    juce::AudioBuffer<float> wetBuf, convBuf, dryBuf;   // the block between the two passes (around the convolution)
    std::atomic<int> requestedIr { -1 };
    std::atomic<int> loadedIr { -1 };
    int lastMidiMode = 0;
    void loadBodyIr (int index);
    std::atomic<float> *pBodyIr = nullptr, *pBodyMix = nullptr, *pDriveOn = nullptr, *pDriveType = nullptr,
                       *pDriveAmt = nullptr, *pDriveTone = nullptr, *pDriveWow = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> bodyMixSmooth, driveMixSmooth;
    float smearSizeF = 1.f;   // SMEAR level matching: how much the reverb's SIZE adds
    std::atomic<float> *pSmearOn = nullptr, *pSmearLevel = nullptr, *pSmearSize = nullptr, *pSmearDrive = nullptr,
                       *pSmearGate = nullptr, *pSmearDark = nullptr, *pSmearHold = nullptr, *pSmearCut = nullptr,
                       *pSmearSend = nullptr, *pSmearPress = nullptr;
    bool smearWasOn = false;
    float smearSendNow = 0.f;
    struct Extra { std::atomic<float> *bloomOn, *motionOn, *voiceOn, *voiceMix, *voiceWave, *voiceOct, *voiceTone, *voiceUnison, *voiceDrift, *voiceDetune, *voicePwmRate, *voicePhaser, *voiceStab, *voiceChorus, *vibDepth, *vibRate, *vibDelay, *vibTarget, *voiceMixOn, *vibDepthOn; } ex {};   // newer parameters, resolved once in the constructor

    std::atomic<float> *pVol, *pAttack, *pFreq, *pMod, *pFilter, *pResLow, *pResHigh, *pSustain, *pBypass,
                       *pSlope, *pDetune, *pDry, *pSync, *pSyncDiv, *pLimiter, *pTouch, *pFollow,
                       *pFeedback, *pFbHarmonic, *pFbRise, *pFbTone, *pTouchRange, *pTouchDepth, *pFollowTight,
                       *pChordMs, *pSustainMax, *pLowLatency,
                       *pModOn, *pSustainOn, *pDetuneOn, *pDryOn, *pTouchOn, *pFollowOn, *pFeedbackOn,
                       *pTremolo, *pTremRate, *pTremShape, *pTremSyncOn, *pTremDiv, *pFbMode, *pSpread, *pSpreadMode, *pTremoloOn, *pSpreadOn;

    void convertOldFilter();
    std::atomic<float> *pVocalOn = nullptr, *pPhaserOn = nullptr, *pSwellShape = nullptr, *pSmoothRepick = nullptr, *pEven = nullptr, *pBowedBody = nullptr;
    std::atomic<float> *pMidiOut = nullptr, *pCharacter = nullptr, *pGlide = nullptr, *pMono = nullptr, *pVibHuman = nullptr;
public:
    std::atomic<float> smearKeyDb { -100.f };   // the playing level the SMEAR THRESH compares (for the UI)
private:
    std::atomic<float> *pString = nullptr, *pLife = nullptr, *pVRes = nullptr, *pVDecay = nullptr, *pSmearThresh = nullptr;
    std::atomic<float> *pWarmth = nullptr, *pVowel = nullptr, *pVowelPick = nullptr, *pScoop = nullptr, *pBreath = nullptr;
    std::atomic<float>* pPickRange = nullptr, *pPickDepth = nullptr;
    std::atomic<float>* pWobble = nullptr, *pPick = nullptr, *pPickOn = nullptr, *pUnisonOn = nullptr, *pDriftOn = nullptr;
    float wobL = 0.f, wobR = 0.f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> volSmooth, bypassSmooth, drySmooth, detuneSmooth, mixSmooth, smearLevelSmooth;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PolySwellProcessor)
};
