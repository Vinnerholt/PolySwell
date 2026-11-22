#pragma once
#include "PluginProcessor.h"

// PolySwell II editor: one pedal (always visible) and an expanded panel of six modules.
// Everything lives in PluginEditor.cpp (class Board); the editor only sizes and scales it.
class PolySwellEditor : public juce::AudioProcessorEditor
{
public:
    explicit PolySwellEditor (PolySwellProcessor&);
    ~PolySwellEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    void setPanelOpen (bool open);      // show / hide the effects panel (the window gets narrower)
    void setUiScale (float scale);      // 0.75 .. 2
    void showKnobView (const juce::String& paramId, bool on);   // as if the mouse were over that knob (ui_snapshot)
    void setSkin (int index);           // 0 Sunburst, 1 Lagoon, 2 Midnight, 3 Sunburst Night

    static constexpr int kBaseW = 1076, kPedalOnlyW = 436, kBaseH = 720;

private:
    void applySize();

    PolySwellProcessor& proc;
    struct Board;
    std::unique_ptr<Board> board;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PolySwellEditor)
};
