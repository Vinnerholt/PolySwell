// PolySwell II editor. Layout follows the approved mockup: an oxblood pedal (window, 3 big + 4 small knobs, two
// footswitches) and a panel of six tinted modules. Static artwork (enclosure, plates, lettering) is baked once into an
// image at the current size; knob bodies are baked per size too. Only indicators, LEDs, chips and the window repaint.
#include "PluginEditor.h"

namespace
{
using juce::Colour;
using juce::Graphics;
using juce::String;
using Rect = juce::Rectangle<float>;
constexpr float kPi = juce::MathConstants<float>::pi;

namespace col
{
    const Colour ink { 0xfff1e3c8 }, ink2 { 0xffd8c3a2 }, amber { 0xfff0a23a }, cream { 0xfff6ead2 }, dimText { 0xffa89a84 },
                 chipText { 0xffd9cbb0 }, body1 { 0xff5e1b21 }, body2 { 0xff380d12 };
    const Colour filter { 0xff6dbba9 }, feedback { 0xffe46f4c }, motion { 0xffe9b24a }, layer { 0xff7eaadb },
                 hold { 0xffb596dc }, space { 0xffa3c27c };
}

juce::Font uiFont (float h, bool bold = true, float track = 0.12f)
{
    return juce::Font (juce::FontOptions ("Bahnschrift", h, bold ? juce::Font::bold : juce::Font::plain))
             .withHorizontalScale (0.88f).withExtraKerningFactor (track);
}
juce::Font monoFont (float h) { return juce::Font (juce::FontOptions ("Consolas", h, juce::Font::plain)); }

// ------------------------------------------------------------------------------------------------ geometry
constexpr float kPedalX = 22, kPedalY = 56, kPedalW = 392, kPedalH = 646;
constexpr float kPanelX = 432, kPanelY = 56, kPanelW = 622, kPanelH = 646;
constexpr float kModW = 293, kModH = 199.3f;

Rect moduleRect (int i)
{
    return { kPanelX + 12 + (float) (i % 2) * (kModW + 12), kPanelY + 12 + (float) (i / 2) * (kModH + 12), kModW, kModH };
}

struct ModDef { const char* title; const char* sub; const char* onId; Colour ac; };
const ModDef kMods[6] = {
    { "FILTER",   "Sweeps each note on its own",       "filterOn",   col::filter },
    { "FEEDBACK", "Amp in the room",                   "feedbackOn", col::feedback },
    { "MOTION",   "Movement per note",                 "motionOn",   col::motion },
    { "LAYER",    "A synth voice under each note",     "voiceOn",    col::layer },
    { "HOLD",     "Freezes ringing notes into a pad",  nullptr,      col::hold },
    { "SPACE",    "Reverb that follows the swell",     "smearOn",    col::space } };

const char* tipFor (const String& id)
{
    static const std::map<String, const char*> t {
        { "attack", "ATTACK: how long each note takes to swell in. 0 = no swell." },
        { "sustain", "SUSTAIN: notes ring longer, never louder than their own peak." },
        { "freq", "TONE: the filter's frequency (needs FILTER switched on)." },
        { "touch", "FEEL: harder picks swell faster (right of noon) or slower (left)." },
        { "feedback", "FEEDBACK: held notes bloom into an overtone, like an amp feeding back." },
        { "dry", "DRY: mixes the unswelled guitar back in, so the pick is heard." },
        { "vol", "LEVEL: output level. Noon = 0 dB." },
        { "mod", "MOD: sweeps the filter over the swell. Right = opens up to TONE, left = closes down from it." },
        { "res", "RES: resonance, a peak at the filter frequency." },
        { "pickFilter", "DYN: harder picks open the filter further (right) or less (left)." },
        { "fbRise", "RISE: how long a held note takes to start feeding back." },
        { "fbTone", "TONE: how bright the feedback overtone is." },
        { "sustainMax", "MAX: the most that SUSTAIN and FEEDBACK may lift a fading note." },
        { "vibDepth", "VIBRATO: pitch movement on each note, in cents." },
        { "tremolo", "TREMOLO: each note pulses on its own, starting at its pick." },
        { "spread", "SPREAD: places each note or chord in the stereo field." },
        { "detune", "DETUNE: a slightly detuned copy, left up and right down." },
        { "voiceMix", "MIX: guitar on the left, synth voice on the right." },
        { "voiceTone", "TONE: the voice's own low-pass filter." },
        { "voiceGlide", "GLIDE: single notes slide from the previous one." },
        { "voiceCharacter", "CHARACTER: makes the voice more alive (string colour, wander, scoop, breath)." },
        { "holdFade", "FADE: how quickly the frozen pad fades in and out." },
        { "holdLevel", "LEVEL: how loud the frozen pad is." },
        { "smearSize", "SIZE: the reverb's length." },
        { "smearLevel", "LEVEL: how much reverb is heard (level-matched)." },
        { "smearDrive", "DRIVE: soft clipping on the reverb only." },
        { "follow", "PACE: fast playing shortens the swell, slow playing keeps the full ATTACK." },
        { "even", "EVEN: levels soft and hard picks. Drag up or down." },
        { "vibRate", "Vibrato rate. Drag up or down." }, { "vibDelay", "Time before the vibrato starts. Drag up or down." },
        { "tremRate", "Tremolo rate. Drag up or down." }, { "detuneWobble", "Slow wobble of the detuned copy. Drag up or down." },
        { "voiceUnison", "Unison: extra detuned copies of the voice. Drag up or down." },
        { "bodyMix", "How much of the body IR is heard. Drag up or down." },
        { "smearHold", "Gate hold time after you stop playing. Drag up or down." },
        { "smearCut", "How fast the gate closes. Drag up or down." },
        { "smearThresh", "Only playing above this level feeds the reverb (-80 = off). Drag up or down." } };
    const auto it = t.find (id);
    return it != t.end() ? it->second : "";
}

// ------------------------------------------------------------------------------------------------ look
struct Look : juce::LookAndFeel_V4
{
    Look()
    {
        setColour (juce::PopupMenu::backgroundColourId, Colour (0xff1d1815));
        setColour (juce::PopupMenu::textColourId, col::ink);
        setColour (juce::PopupMenu::headerTextColourId, col::dimText);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, col::amber);
        setColour (juce::PopupMenu::highlightedTextColourId, Colour (0xff2a1703));
        setColour (juce::TooltipWindow::backgroundColourId, Colour (0xff1d1815));
        setColour (juce::TooltipWindow::textColourId, col::ink);
        setColour (juce::TooltipWindow::outlineColourId, col::ink.withAlpha (0.25f));
        setColour (juce::AlertWindow::backgroundColourId, Colour (0xff231d19));
        setColour (juce::AlertWindow::textColourId, col::ink);
        setColour (juce::AlertWindow::outlineColourId, col::ink.withAlpha (0.25f));
        setColour (juce::TextEditor::backgroundColourId, Colour (0xff0d0906));
        setColour (juce::TextEditor::textColourId, col::amber);
        setColour (juce::TextEditor::outlineColourId, col::ink.withAlpha (0.25f));
        setColour (juce::TextButton::buttonColourId, Colour (0xff332b26));
        setColour (juce::TextButton::textColourOffId, col::ink);
    }

    juce::Font getPopupMenuFont() override { return uiFont (15.f, false, 0.04f); }
    juce::Font getAlertWindowTitleFont() override { return uiFont (18.f); }
    juce::Font getAlertWindowMessageFont() override { return uiFont (15.f, false, 0.04f); }
    juce::Font getAlertWindowFont() override { return uiFont (15.f, false, 0.04f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return uiFont (14.f); }

    // --- baked knob bodies -----------------------------------------------------------------------
    // kind 0 / 1: brushed aluminium on a knurled skirt (pedal, large / small); kind 2: bakelite (panel)
    const juce::Image& body (int kind, int px)
    {
        const int key = kind * 100000 + px;
        auto it = bodies.find (key);
        if (it != bodies.end()) return it->second;
        juce::Image img (juce::Image::ARGB, px, px, true);
        Graphics g (img);
        const float c = (float) px * 0.5f;
        const float R = (float) px * (kind == 0 ? 42.f / 124.f : kind == 1 ? 25.f / 78.f : 22.f / 64.f);
        juce::Path disc; disc.addEllipse (c - R, c - R, 2 * R, 2 * R);
        juce::DropShadow (juce::Colours::black.withAlpha (0.62f), juce::roundToInt (R * 0.36f), { 0, juce::roundToInt (R * 0.24f) }).drawForPath (g, disc);
        juce::DropShadow (juce::Colours::black.withAlpha (0.5f), juce::jmax (1, juce::roundToInt (R * 0.08f)), { 0, juce::jmax (1, juce::roundToInt (R * 0.05f)) }).drawForPath (g, disc);

        if (kind < 2)
        {
            // silk-screened ticks around the knob
            g.setColour (col::ink.withAlpha (0.85f));
            for (int i = 0; i <= 10; ++i)
            {
                const float a = -0.75f * kPi + 1.5f * kPi * (float) i / 10.f, s = std::sin (a), co = -std::cos (a);
                g.drawLine (c + s * R * 1.15f, c + co * R * 1.15f, c + s * R * 1.29f, c + co * R * 1.29f, juce::jmax (1.f, R * (i % 5 == 0 ? 0.045f : 0.03f)));
            }
            // knurled skirt
            g.setGradientFill (juce::ColourGradient (Colour (0xff3b3b3d), c, c - R, Colour (0xff0c0c0d), c, c + R, false));
            g.fillPath (disc);
            for (int i = 0; i < 48; ++i)
            {
                const float a = 2 * kPi * (float) i / 48.f, s = std::sin (a), co = -std::cos (a);
                g.setColour ((i & 1) ? juce::Colours::white.withAlpha (0.10f) : juce::Colours::black.withAlpha (0.45f));
                g.drawLine (c + s * R * 0.84f, c + co * R * 0.84f, c + s * R * 0.99f, c + co * R * 0.99f, juce::jmax (1.f, R * 0.045f));
            }
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.32f), c, c - R, juce::Colours::transparentWhite, c, c, false));
            g.drawEllipse (c - R + 0.5f, c - R + 0.5f, 2 * R - 1, 2 * R - 1, 1.f);

            // brushed-aluminium cap: thin wedges whose brightness follows the angle (one fixed light)
            const float Rc = R * 0.76f;
            juce::Path capDisc; capDisc.addEllipse (c - Rc, c - Rc, 2 * Rc, 2 * Rc);
            juce::DropShadow (juce::Colours::black.withAlpha (0.6f), juce::jmax (1, juce::roundToInt (R * 0.1f)), { 0, juce::jmax (1, juce::roundToInt (R * 0.06f)) }).drawForPath (g, capDisc);
            const int n = 240;
            for (int i = 0; i < n; ++i)
            {
                const float a = 2 * kPi * (float) i / (float) n;
                const float b = 0.70f + 0.17f * std::cos (2.f * (a - 0.6f)) + 0.09f * std::cos (4.f * (a + 0.3f)) + 0.03f * std::cos (10.f * a);
                juce::Path w; w.addPieSegment (c - Rc, c - Rc, 2 * Rc, 2 * Rc, a, a + 2 * kPi / (float) n + 0.012f, 0.f);
                g.setColour (Colour::fromFloatRGBA (juce::jlimit (0.f, 1.f, b * 1.01f), juce::jlimit (0.f, 1.f, b), juce::jlimit (0.f, 1.f, b * 0.965f), 1.f));
                g.fillPath (w);
            }
            g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, c, c, juce::Colours::black.withAlpha (0.22f), c + Rc, c, true));
            g.fillPath (capDisc);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.9f), c, c - Rc, juce::Colours::black.withAlpha (0.45f), c, c + Rc, false));
            g.drawEllipse (c - Rc + 0.5f, c - Rc + 0.5f, 2 * Rc - 1, 2 * Rc - 1, juce::jmax (1.f, R * 0.035f));
        }
        else
        {
            juce::ColourGradient gr (Colour (0xff625c58), c - R * 0.3f, c - R * 0.5f, Colour (0xff0f0d0c), c + R * 0.6f, c + R * 0.9f, true);
            gr.addColour (0.55, Colour (0xff262220));
            g.setGradientFill (gr);
            g.fillPath (disc);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.3f), c, c - R, juce::Colours::transparentWhite, c, c + R * 0.2f, false));
            g.drawEllipse (c - R + 0.5f, c - R + 0.5f, 2 * R - 1, 2 * R - 1, 1.f);
        }
        return bodies.emplace (key, img).first->second;
    }

    void drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        const int kind = (int) s.getProperties().getWithDefault ("kind", 2);
        const Colour ac ((juce::uint32) (juce::int64) s.getProperties().getWithDefault ("ac", (juce::int64) col::amber.getARGB()));
        const Rect b ((float) x, (float) y, (float) w, (float) h);
        if (kind == 3) { drawValueChip (g, b, pos, s, ac); return; }

        const float ps = g.getInternalContext().getPhysicalPixelScaleFactor();
        const int px = juce::jmax (8, juce::roundToInt ((float) w * ps));
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        g.drawImage (body (kind, px), b);

        const float c = b.getCentreX(), cy = b.getCentreY();
        const float R = (float) w * (kind == 0 ? 42.f / 124.f : kind == 1 ? 25.f / 78.f : 22.f / 64.f);
        const float a = a0 + pos * (a1 - a0), sn = std::sin (a), cs = -std::cos (a);
        if (kind < 2)
        {
            const float Rc = R * 0.76f;
            g.setColour (Colour (0xff1a1512));
            g.drawLine (juce::Line<float> (c + sn * Rc * 0.42f, cy + cs * Rc * 0.42f, c + sn * Rc * 0.9f, cy + cs * Rc * 0.9f), juce::jmax (2.f, R * 0.085f));
        }
        else
        {
            // value ring in the module's colour (from the middle for two-sided knobs)
            const float rr = R + 5.5f;
            juce::Path track; track.addCentredArc (c, cy, rr, rr, 0.f, a0, a1, true);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.strokePath (track, juce::PathStrokeType (3.f));
            const float from = s.getMinimum() < 0.0 ? (a0 + a1) * 0.5f : a0;
            if (std::abs (a - from) > 0.01f)
            {
                juce::Path val; val.addCentredArc (c, cy, rr, rr, 0.f, juce::jmin (from, a), juce::jmax (from, a), true);
                g.setColour (ac);
                g.strokePath (val, juce::PathStrokeType (3.f));
            }
            g.setColour (col::cream);
            g.drawLine (juce::Line<float> (c + sn * R * 0.34f, cy + cs * R * 0.34f, c + sn * R * 0.86f, cy + cs * R * 0.86f), 2.5f);
        }
    }

    static void chipFrame (Graphics& g, Rect b, bool lit, Colour ac, bool over)
    {
        b = b.reduced (0.5f);
        if (lit) { g.setColour (ac); g.fillRoundedRectangle (b, 4.f); }
        else
        {
            g.setColour (juce::Colours::white.withAlpha (over ? 0.07f : 0.025f)); g.fillRoundedRectangle (b, 4.f);
            g.setColour (col::ink.withAlpha (over ? 0.42f : 0.24f)); g.drawRoundedRectangle (b, 4.f, 1.f);
        }
    }

    // a number you drag: "RATE 5.5 Hz" with a thin bar showing where it sits in its range
    void drawValueChip (Graphics& g, Rect b, float pos, juce::Slider& s, Colour ac)
    {
        chipFrame (g, b, false, ac, s.isMouseOverOrDragging());
        g.setColour (ac.withAlpha (0.85f));
        g.fillRoundedRectangle (b.getX() + 4, b.getBottom() - 3.5f, (b.getWidth() - 8) * juce::jlimit (0.f, 1.f, pos), 2.f, 1.f);
        const String label = s.getProperties().getWithDefault ("label", "").toString();
        g.setFont (uiFont (11.5f, true, 0.08f));
        g.setColour (col::dimText);
        const float lw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label);
        g.drawText (label, b.withTrimmedLeft (8).withWidth (lw + 2), juce::Justification::centredLeft);
        g.setColour (col::chipText);
        g.drawText (s.getTextFromValue (s.getValue()).toUpperCase(), b.withTrimmedLeft (lw + 14).withTrimmedRight (6), juce::Justification::centredLeft);
    }

    std::map<int, juce::Image> bodies;
};

// "300.0000000" -> "300 ms", "5.50 Hz" -> "5.5 Hz": at most three significant digits, no trailing zeros, with its unit
static String tidyValue (const String& raw, const String& unit)
{
    const String t = raw.trim();
    const String num = t.upToFirstOccurrenceOf (" ", false, false);
    if (num.isEmpty() || ! num.containsOnly ("-+.0123456789")) return t;
    const double v = num.getDoubleValue(), a = std::abs (v);
    String n (v, a >= 100.0 ? 0 : a >= 10.0 ? 1 : 2);
    if (n.containsChar ('.')) { while (n.endsWithChar ('0')) n = n.dropLastCharacters (1); if (n.endsWithChar ('.')) n = n.dropLastCharacters (1); }
    if (n == "-0") n = "0";
    const String rest = t.fromFirstOccurrenceOf (" ", false, false).trim();
    const String u = rest.isNotEmpty() ? rest : unit;
    return u.isNotEmpty() ? n + " " + u : n;
}

// ------------------------------------------------------------------------------------------------ controls
struct Host   // what the controls need from the board
{
    virtual ~Host() = default;
    virtual void readout (const String& name, const String& value) = 0;
    virtual void focus (const String& paramId, bool on) = 0;   // the window explains this knob while on
};

struct Knob : juce::Slider
{
    Knob (PolySwellProcessor& p, Host& h, const String& paramId, const String& shownName, int kind, Colour ac)
        : host (h), name (shownName), id (paramId)
    {
        setSliderStyle (kind == 3 ? juce::Slider::RotaryVerticalDrag : juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        setRotaryParameters (1.25f * kPi, 2.75f * kPi, true);
        setMouseDragSensitivity (kind == 3 ? 160 : 220);
        setPopupDisplayEnabled (false, false, nullptr);
        getProperties().set ("kind", kind);
        getProperties().set ("ac", (juce::int64) ac.getARGB());
        getProperties().set ("label", shownName);
        setTooltip (tipFor (paramId));
        att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, paramId, *this);
        const String unit = p.apvts.getParameter (paramId)->getLabel();
        textFromValueFunction = [base = textFromValueFunction, unit] (double v) { return tidyValue (base ? base (v) : String (v), unit); };
        onValueChange = [this] { if (isMouseOverOrDragging()) show(); repaint(); };
        onDragStart = [this] { show(); host.focus (id, true); };
        onDragEnd = [this] { if (! isMouseOver()) host.focus (id, false); };
    }
    void mouseEnter (const juce::MouseEvent& e) override { juce::Slider::mouseEnter (e); show(); host.focus (id, true); repaint(); }
    void mouseExit (const juce::MouseEvent& e) override { juce::Slider::mouseExit (e); if (! e.mods.isAnyMouseButtonDown()) host.focus (id, false); repaint(); }
    void show() { host.readout (name, getTextFromValue (getValue())); }
    int chipWidth()   // wide enough for the label and the longest value
    {
        const auto f = uiFont (11.5f, true, 0.08f);
        float w = 0;
        for (double v : { getMinimum(), getMaximum(), getValue() }) w = juce::jmax (w, juce::GlyphArrangement::getStringWidth (f, getTextFromValue (v).toUpperCase()));
        return juce::roundToInt (juce::GlyphArrangement::getStringWidth (f, name) + w) + 26;
    }

    Host& host;
    String name, id;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

// a parameter seen as one float, with undo
struct Bound
{
    Bound (PolySwellProcessor& p, const String& id, std::function<void()> changed)
        : param (p.apvts.getParameter (id)), onChange (std::move (changed))
    {
        jassert (param != nullptr);
        att = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { value = v; if (onChange) onChange(); }, &p.undo);
        att->sendInitialUpdate();
    }
    void set (float v) { att->setValueAsCompleteGesture (v); }
    String text() const { return param->getCurrentValueAsText(); }

    juce::RangedAudioParameter* param;
    std::function<void()> onChange;
    std::unique_ptr<juce::ParameterAttachment> att;
    float value = 0.f;
};

// choice names as they fit on a chip
static String shortChoice (String s)
{
    s = s.upToFirstOccurrenceOf (" (", false, false).toUpperCase();
    static const std::pair<const char*, const char*> shortNames[] = {
        { "LOUD IN THE MIDDLE", "LOUD MID" }, { "SOFT IN THE MIDDLE", "SOFT MID" }, { "BY PICK STRENGTH", "BY PICK" },
        { "GUITAR + VOICE", "BOTH" }, { "ALTERNATE", "ALT" }, { "TRIANGLE", "TRI" }, { "FOLLOWS SWELL", "SWELL" },
        { "6 DB/OCT", "6 DB" }, { "12 DB/OCT", "12 DB" }, { "24 DB/OCT", "24 DB" }, { "48 DB/OCT", "48 DB" }, { "SUB OCTAVE", "SUB" } };
    for (auto& n : shortNames) if (s == n.first) return n.second;
    return s;
}

struct Chip : juce::Button
{
    enum Mode { toggle, menu, custom };
    Chip (PolySwellProcessor& p, Mode m, const String& paramId, const String& labelText, Colour accent)
        : juce::Button (labelText), mode (m), label (labelText), ac (accent)
    {
        if (paramId.isNotEmpty()) bound = std::make_unique<Bound> (p, paramId, [this] { repaint(); });
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    String shown() const
    {
        if (textFn) return textFn();
        if (mode == menu && bound != nullptr) return (label.isEmpty() ? String() : label + " ") + shortChoice (bound->text());
        return label;
    }
    bool lit() const { return litFn ? litFn() : (mode == toggle && bound != nullptr && bound->value > 0.5f); }

    int idealWidth() const
    {
        const auto f = uiFont (11.5f, true, 0.08f);
        float w = juce::GlyphArrangement::getStringWidth (f, shown());
        if (mode == menu && bound != nullptr && ! textFn)
            for (auto& s : bound->param->getAllValueStrings())
                w = juce::jmax (w, juce::GlyphArrangement::getStringWidth (f, (label.isEmpty() ? String() : label + " ") + shortChoice (s)));
        return juce::roundToInt (w) + (mode == toggle ? 18 : 28);
    }

    void paintButton (Graphics& g, bool over, bool) override
    {
        const auto b = getLocalBounds().toFloat();
        const bool on = lit();
        Look::chipFrame (g, b, on, ac, over);
        g.setFont (uiFont (11.5f, true, 0.08f));
        g.setColour (on ? Colour (0xff1f1408) : col::chipText);
        g.drawText (shown(), b.withTrimmedLeft (8).withTrimmedRight (mode == toggle ? 6.f : 16.f), juce::Justification::centredLeft);
        if (mode != toggle)
        {
            juce::Path t; const float x = b.getRight() - 12, y = b.getCentreY() - 1.5f;
            t.addTriangle (x - 3.5f, y, x + 3.5f, y, x, y + 4.5f);
            g.fillPath (t);
        }
    }

    void clicked() override
    {
        if (onPress) { onPress(); return; }
        if (bound == nullptr) return;
        if (mode == toggle) { bound->set (bound->value > 0.5f ? 0.f : 1.f); return; }
        juce::PopupMenu m;
        const auto names = bound->param->getAllValueStrings();
        const int cur = juce::roundToInt (bound->value);
        for (int i = 0; i < names.size(); ++i) m.addItem (i + 1, names[i], true, i == cur);
        m.setLookAndFeel (&getLookAndFeel());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [safe = juce::Component::SafePointer<Chip> (this)] (int r) { if (safe != nullptr && r > 0) safe->bound->set ((float) (r - 1)); });
    }

    Mode mode;
    String label;
    Colour ac;
    std::unique_ptr<Bound> bound;
    std::function<String()> textFn;
    std::function<bool()> litFn;
    std::function<void()> onPress;
};

// a few named choices side by side (the lit one is the parameter's value)
struct Seg : juce::Component, juce::SettableTooltipClient
{
    Seg (PolySwellProcessor& p, const String& paramId, std::vector<std::pair<String, int>> options, Colour accent)
        : opts (std::move (options)), ac (accent), bound (p, paramId, [this] { repaint(); })
    { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    int idealWidth() const
    {
        float w = 0; const auto f = uiFont (11.5f, true, 0.08f);
        for (auto& o : opts) w += juce::GlyphArrangement::getStringWidth (f, o.first) + 17.f;
        return juce::roundToInt (w);
    }
    void paint (Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (0.5f);
        const auto f = uiFont (11.5f, true, 0.08f);
        g.setFont (f);
        float x = b.getX();
        const int cur = juce::roundToInt (bound.value);
        for (size_t i = 0; i < opts.size(); ++i)
        {
            const float w = juce::GlyphArrangement::getStringWidth (f, opts[i].first) + 17.f;
            const Rect r (x, b.getY(), w, b.getHeight());
            if (opts[i].second == cur)
            {
                juce::Path p; p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 4.f, 4.f, i == 0, i + 1 == opts.size(), i == 0, i + 1 == opts.size());
                g.setColour (ac); g.fillPath (p);
                g.setColour (Colour (0xff1f1408));
            }
            else g.setColour (col::chipText);
            g.drawText (opts[i].first, r, juce::Justification::centred);
            if (i > 0) { g.setColour (col::ink.withAlpha (0.18f)); g.fillRect (x, b.getY() + 1, 1.f, b.getHeight() - 2); }
            x += w;
        }
        g.setColour (col::ink.withAlpha (0.24f));
        g.drawRoundedRectangle (b, 4.f, 1.f);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto f = uiFont (11.5f, true, 0.08f);
        float x = 0.5f;
        for (auto& o : opts)
        {
            const float w = juce::GlyphArrangement::getStringWidth (f, o.first) + 17.f;
            if ((float) e.x >= x && (float) e.x < x + w) { bound.set ((float) o.second); return; }
            x += w;
        }
    }
    std::vector<std::pair<String, int>> opts;
    Colour ac;
    Bound bound;
};

static void drawLed (Graphics& g, Rect b, bool on)
{
    const float c = b.getCentreX(), cy = b.getCentreY(), r = 5.f;
    if (on)
    {
        g.setGradientFill (juce::ColourGradient (Colour (0xffffa028).withAlpha (0.6f), c, cy, Colour (0x00ffa028), c + r * 3.2f, cy, true));
        g.fillEllipse (c - r * 3.2f, cy - r * 3.2f, r * 6.4f, r * 6.4f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.5f)); g.fillEllipse (c - r - 2, cy - r - 2, 2 * r + 4, 2 * r + 4);
    juce::ColourGradient gr (on ? Colour (0xfffff7dc) : Colour (0xff7a4a1a), c - r * 0.3f, cy - r * 0.4f,
                             on ? Colour (0xffc06400) : Colour (0xff2c1706), c + r, cy + r, true);
    if (on) gr.addColour (0.4, Colour (0xffffb030));
    g.setGradientFill (gr);
    g.fillEllipse (c - r, cy - r, 2 * r, 2 * r);
}

struct Led : juce::Button
{
    Led (PolySwellProcessor& p, const String& paramId, bool inverted, const String& tip)
        : juce::Button (paramId), invert (inverted), bound (p, paramId, [this] { repaint(); })
    { setTooltip (tip); setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    bool on() const { return (bound.value > 0.5f) != invert; }
    void paintButton (Graphics& g, bool, bool) override { drawLed (g, getLocalBounds().toFloat(), on()); }
    void clicked() override { bound.set (bound.value > 0.5f ? 0.f : 1.f); }
    bool invert;
    Bound bound;
};

struct FootSwitch : juce::Component, juce::SettableTooltipClient
{
    FootSwitch() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const float c = b.getCentreX(), cy = b.getCentreY(), R = 31.f;
        juce::Path nut; nut.addEllipse (c - R, cy - R, 2 * R, 2 * R);
        juce::DropShadow (juce::Colours::black.withAlpha (0.7f), 10, { 0, 7 }).drawForPath (g, nut);
        for (int i = 0; i < 120; ++i)   // chromed nut
        {
            const float a = 2 * kPi * (float) i / 120.f, v = 0.6f + 0.2f * std::cos (2.f * (a - 0.5f)) + 0.12f * std::cos (6.f * a);
            juce::Path w; w.addPieSegment (c - R, cy - R, 2 * R, 2 * R, a, a + 2 * kPi / 120.f + 0.02f, 0.f);
            g.setColour (Colour::fromFloatRGBA (v, v * 0.99f, v * 0.96f, 1.f)); g.fillPath (w);
        }
        g.setColour (juce::Colours::white.withAlpha (0.55f)); g.drawEllipse (c - R + 0.5f, cy - R + 0.5f, 2 * R - 1, 2 * R - 1, 1.f);
        const float r = down ? 19.f : 20.f, oy = down ? 1.f : 0.f;
        juce::Path cap; cap.addEllipse (c - r, cy - r + oy, 2 * r, 2 * r);
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), down ? 2 : 5, { 0, down ? 1 : 3 }).drawForPath (g, cap);
        juce::ColourGradient gr (juce::Colours::white, c - r * 0.3f, cy - r * 0.45f + oy, Colour (0xff55524e), c + r * 0.8f, cy + r + oy, true);
        gr.addColour (0.35, Colour (0xffc9c6bf)); gr.addColour (0.75, Colour (0xff77746f));
        g.setGradientFill (gr); g.fillPath (cap);
    }
    void mouseDown (const juce::MouseEvent&) override { down = true; repaint(); if (onDown) onDown(); }
    void mouseUp (const juce::MouseEvent&) override { down = false; repaint(); if (onUp) onUp(); }
    bool hitTest (int x, int y) override { return juce::Point<float> ((float) x, (float) y).getDistanceFrom (getLocalBounds().toFloat().getCentre()) < 32.f; }
    std::function<void()> onDown, onUp;
    bool down = false;
};

struct IconButton : juce::Button
{
    enum Icon { prev, next, undo, redo, gear, panel };
    IconButton (Icon i, const String& tip) : juce::Button (tip), icon (i) { setTooltip (tip); setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paintButton (Graphics& g, bool over, bool down) override
    {
        const auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setGradientFill (juce::ColourGradient (Colour (over ? 0xff40362f : 0xff332b26), 0, b.getY(), Colour (0xff211b18), 0, b.getBottom(), false));
        g.fillRoundedRectangle (b, 5.f);
        g.setColour (juce::Colours::white.withAlpha (0.08f)); g.drawRoundedRectangle (b, 5.f, 1.f);
        const float c = b.getCentreX(), cy = b.getCentreY() + (down ? 0.5f : 0.f);
        g.setColour (isEnabled() ? col::chipText : col::chipText.withAlpha (0.35f));
        juce::Path p;
        const juce::PathStrokeType st (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        switch (icon)
        {
            case prev: p.startNewSubPath (c + 2.5f, cy - 5); p.lineTo (c - 2.5f, cy); p.lineTo (c + 2.5f, cy + 5); break;
            case next: p.startNewSubPath (c - 2.5f, cy - 5); p.lineTo (c + 2.5f, cy); p.lineTo (c - 2.5f, cy + 5); break;
            case undo: case redo:
            {
                const float d = icon == undo ? 1.f : -1.f;
                p.startNewSubPath (c - d * 2.5f, cy - 5.5f); p.lineTo (c - d * 5.5f, cy - 2.5f); p.lineTo (c - d * 2.5f, cy + 0.5f);
                p.startNewSubPath (c - d * 5.5f, cy - 2.5f); p.lineTo (c + d * 1.5f, cy - 2.5f);
                p.addCentredArc (c + d * 1.5f, cy + 1.5f, 4.f, 4.f, 0.f, 0.f, d * kPi, false);
                p.lineTo (c - d * 1.5f, cy + 5.5f);
                break;
            }
            case gear:
                p.addEllipse (c - 2.4f, cy - 2.4f, 4.8f, 4.8f);
                for (int i = 0; i < 8; ++i) { const float a = kPi * (float) i / 4.f; p.startNewSubPath (c + std::sin (a) * 4.6f, cy + std::cos (a) * 4.6f); p.lineTo (c + std::sin (a) * 6.6f, cy + std::cos (a) * 6.6f); }
                break;
            case panel:
                p.addRoundedRectangle (c - 6.5f, cy - 5.f, 13.f, 10.f, 1.5f); p.startNewSubPath (c - 2.f, cy - 5.f); p.lineTo (c - 2.f, cy + 5.f);
                break;
        }
        g.strokePath (p, st);
    }
    Icon icon;
};

struct Lcd : juce::Component, juce::SettableTooltipClient
{
    Lcd() { setMouseCursor (juce::MouseCursor::PointingHandCursor); setTooltip ("Preset: click for the list, and to save your own."); }
    void paint (Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour (Colour (0xff0d0906)); g.fillRoundedRectangle (b, 5.f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.8f), 0, 0, juce::Colours::transparentBlack, 0, 7, false));
        g.fillRoundedRectangle (b, 5.f);
        g.setColour (juce::Colours::white.withAlpha (0.08f)); g.drawRoundedRectangle (b.reduced (0.5f), 5.f, 1.f);
        g.setFont (monoFont (14.f));
        g.setColour (Colour (0x55ff9628)); g.drawText (text, b.reduced (11, 0).translated (0, 0.8f), juce::Justification::centredLeft);
        g.setColour (Colour (0xffffb44a)); g.drawText (text, b.reduced (11, 0), juce::Justification::centredLeft);
        g.drawText (right, b.reduced (11, 0), juce::Justification::centredRight);
    }
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    String text, right;
    std::function<void()> onClick;
};

// ------------------------------------------------------------------------------------------------ signal window
// One glowing ribbon per sounding partial: time runs left to right (about 3.5 s), pitch bottom to top, thickness is
// level. Ribbons turn pale where the plugin is holding a note above what the guitar itself gives (sustain, feedback).
struct SignalWindow : juce::Component, juce::Timer
{
    static constexpr int kCols = 106, kB = PolySwellProcessor::kSpecBands, kLoBand = 3, kHiBand = 55;

    explicit SignalWindow (PolySwellProcessor& p) : proc (p) { setInterceptsMouseClicks (false, false); startTimerHz (30); }

    void setReadout (const String& n, const String& v) { roName = n.toUpperCase(); roValue = v; roTicks = 60; }

    // a knob is under the mouse or being turned: the window explains that knob until the mouse leaves
    void setFocus (const String& id, bool on)
    {
        if (on) { focusId = id; focusHeld = true; }
        else if (id == focusId) { focusHeld = false; linger = 26; }
    }

    // 1 = the swell over time, 2 = the filter over frequency, 0 = no picture (the live ribbons stay)
    static int viewKind (const String& id)
    {
        for (auto* e : { "attack", "sustain", "touch", "follow", "even", "feedback", "dry", "vol", "fbRise", "sustainMax", "swellShape" }) if (id == e) return 1;
        for (auto* f : { "freq", "mod", "res", "pickFilter", "slope" }) if (id == f) return 2;
        return 0;
    }

    float P (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    void timerCallback() override
    {
        float in[kB], out[kB];
        const uint32_t frame = proc.readSpectrum (in, out);
        idle = frame == lastFrame ? idle + 1 : 0;
        lastFrame = frame;
        auto& th = thick[(size_t) head]; auto& ht = hot[(size_t) head];
        bool any = false;
        for (int b = 0; b < kB; ++b)
        {
            const float o = idle > 4 ? 0.f : out[b];
            const float db = 20.f * std::log10 (o + 1.0e-9f);
            const bool peak = b > 0 && b < kB - 1 && o >= out[b - 1] && o > out[b + 1];
            const float target = peak ? juce::jlimit (0.f, 1.f, (db + 66.f) / 50.f) : 0.f;
            sm[(size_t) b] += (target - sm[(size_t) b]) * (target > sm[(size_t) b] ? 0.55f : 0.3f);
            if (sm[(size_t) b] < 0.02f) sm[(size_t) b] = 0.f;
            th[(size_t) b] = sm[(size_t) b];
            ht[(size_t) b] = juce::jlimit (0.f, 1.f, (20.f * std::log10 ((o + 1.0e-9f) / (in[b] + 1.0e-9f)) - 4.f) / 14.f);
            any = any || sm[(size_t) b] > 0.f;
        }
        const auto note = proc.latestNote();
        pick[(size_t) head] = note.seq != lastSeq;
        lastSeq = note.seq;
        head = (head + 1) % kCols;
        if (focusHeld) roTicks = 30; else if (roTicks > 0) --roTicks;
        if (! focusHeld && linger > 0) --linger;
        const float target = ((focusHeld || linger > 0) && viewKind (focusId) != 0) ? 1.f : 0.f;
        const float before = viewA;
        viewA += (target - viewA) * 0.35f;
        if (std::abs (target - viewA) < 0.02f) viewA = target;
        const float hl = proc.holdLevelNow.load (std::memory_order_relaxed);
        const bool byp = P ("bypass") > 0.5f;
        // nothing moving: stop repainting once the last ribbon has scrolled off
        quiet = any ? 0 : quiet + 1;
        if (quiet < kCols + 2 || roTicks > 0 || hadReadout || hl != lastHold || byp != lastByp || viewA > 0.f || before > 0.f) repaint();
        hadReadout = roTicks > 0; lastHold = hl; lastByp = byp;
    }

    float bandY (int b) const { return (float) getHeight() - 22.f - (float) (b - kLoBand) / (float) (kHiBand - kLoBand) * ((float) getHeight() - 34.f); }

    // ---------------------------------------------------------------- the swell picture
    static constexpr float kT = 3.5f;                 // seconds across the picture
    static float natural (float t) { return t < 0.f ? 0.f : std::pow (10.f, -8.f * t / 20.f); }   // a plucked note: about 8 dB/s
    static float swellGain (float ph, bool bowed)
    {
        ph = juce::jlimit (0.f, 1.f, ph);
        if (! bowed) return ph * ph;
        return std::pow ((1.f - std::exp (-3.1f * ph)) / (1.f - std::exp (-3.1f)), 2.45f);
    }
    // level of one note after the pedal: its swell over `attack`, and SUSTAIN slowing the decay
    float swelled (float t, float attack) const
    {
        if (t < 0.f) return 0.f;
        const float s = P ("sustain");
        const float rate = juce::jmin (8.f, 0.5f + 23.5f * (1.f - s) * (1.f - s));
        const float lift = juce::jmin (P ("sustainMax") * s, (8.f - rate) * t);
        return std::pow (10.f, (-8.f * t + lift) / 20.f) * swellGain (attack < 0.002f ? 1.f : t / attack, P ("swellShape") > 0.5f);
    }

    struct Plot { float x0, w, base, h; float x (float t) const { return x0 + w * t / kT; } float y (float a) const { return base - h * juce::jmin (a, 1.12f); } };

    template <typename Fn>
    static juce::Path curve (const Plot& pl, float from, Fn&& level)
    {
        juce::Path p;
        const int n = 150;
        bool started = false;
        for (int i = 0; i <= n; ++i)
        {
            const float t = kT * (float) i / (float) n;
            if (t < from - kT / (float) n) continue;
            const float tt = juce::jmax (t, from);
            if (! started) { p.startNewSubPath (pl.x (tt), pl.y (level (tt))); started = true; }
            else p.lineTo (pl.x (tt), pl.y (level (tt)));
        }
        return p;
    }
    static void dashed (Graphics& g, const juce::Path& p, Colour c, float thickness = 1.3f)
    {
        juce::Path d; const float dashes[2] = { 4.f, 4.f };
        juce::PathStrokeType (thickness).createDashedStroke (d, p, dashes, 2);
        g.setColour (c); g.fillPath (d);
    }
    static void glowLine (Graphics& g, const juce::Path& p, Colour c, const Plot* fillTo = nullptr)
    {
        if (fillTo != nullptr && ! p.isEmpty())
        {
            juce::Path f (p);
            f.lineTo (p.getBounds().getRight(), fillTo->base); f.lineTo (p.getBounds().getX(), fillTo->base); f.closeSubPath();
            g.setGradientFill (juce::ColourGradient (c.withAlpha (0.30f), 0, fillTo->base - fillTo->h, c.withAlpha (0.03f), 0, fillTo->base, false));
            g.fillPath (f);
        }
        g.setColour (c.withAlpha (0.14f)); g.strokePath (p, juce::PathStrokeType (7.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c.withAlpha (0.22f)); g.strokePath (p, juce::PathStrokeType (4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c); g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    static void tag (Graphics& g, const String& s, float x, float y, Colour c, juce::Justification j = juce::Justification::centredLeft)
    {
        g.setFont (monoFont (10.5f)); g.setColour (c);
        g.drawText (s, Rect (j == juce::Justification::centredLeft ? x : x - 120.f, y - 7.f, 120.f, 14.f), j);
    }

    String paintSwell (Graphics& g, Rect b)
    {
        const Colour amberC (0xffffb347), guitarC (0xffd8c3a2), paleC (0xfffff1cf);
        const Plot pl { 16.f, b.getWidth() - 32.f, b.getHeight() - 30.f, b.getHeight() - 62.f };
        g.setColour (Colour (0x33ff9a3a)); g.fillRect (pl.x0, pl.base, pl.w, 1.f);
        for (int s = 1; s <= 3; ++s)
        {
            g.setColour (Colour (0x22ff9a3a)); g.fillRect (pl.x ((float) s), pl.base - pl.h, 1.f, pl.h + 4.f);
            tag (g, String (s) + " s", pl.x ((float) s) + 3.f, pl.base - pl.h + 4.f, Colour (0x88e89a45));
        }
        const float A = P ("attack") * 0.001f;
        const String id = focusId;
        auto marker = [&] (float t, Colour c)
        {
            if (t > kT) return;
            for (float y = pl.base - pl.h; y < pl.base; y += 5.f) { g.setColour (c.withAlpha (0.55f)); g.fillRect (pl.x (t), y, 1.f, 2.5f); }
            juce::Path tri; tri.addTriangle (pl.x (t) - 4.f, pl.base + 6.f, pl.x (t) + 4.f, pl.base + 6.f, pl.x (t), pl.base);
            g.setColour (c); g.fillPath (tri);
        };
        auto guitar = [&] (float onset, float amp) { dashed (g, curve (pl, onset, [=] (float t) { return amp * natural (t - onset); }), guitarC.withAlpha (0.55f)); };

        if (id == "touch")
        {
            const float tch = P ("touch"), depth = P ("touchDepth");
            guitar (0.f, 1.f);
            if (std::abs (tch) < 0.02f)
            {
                glowLine (g, curve (pl, 0.f, [&] (float t) { return swelled (t, A); }), amberC, &pl);
                marker (A, amberC);
                return "at noon every pick swells alike";
            }
            const float hard = A * std::pow (2.f, -tch * depth), soft = A * std::pow (2.f, tch * depth);
            glowLine (g, curve (pl, 0.f, [&] (float t) { return 0.75f * swelled (t, soft); }), guitarC);
            glowLine (g, curve (pl, 0.f, [&] (float t) { return swelled (t, hard); }), amberC, &pl);
            marker (hard, amberC); marker (soft, guitarC);
            tag (g, "HARD PICK", pl.x (juce::jmin (hard, 2.6f)) + 5.f, pl.base - pl.h + 16.f, amberC);
            tag (g, "SOFT PICK", pl.x (juce::jmin (soft, 2.6f)) + 5.f, pl.base - pl.h + 28.f, guitarC);
            return tch > 0.f ? "hard picks swell faster, soft picks slower" : "soft picks swell faster, hard picks slower";
        }
        if (id == "follow")
        {
            const float f = P ("follow"), cap = 2.f + (P ("followTight") - 2.f) * f;
            const float on[6] = { 0.f, 1.15f, 2.1f, 2.4f, 2.7f, 3.0f };
            for (int i = 0; i < 6; ++i)
            {
                const float a = (i > 0 && f > 0.005f) ? juce::jmin (A, cap * (on[i] - on[i - 1])) : A;
                const float o = on[i];
                guitar (o, 0.8f);
                dashed (g, curve (pl, o, [&] (float t) { return 0.8f * swelled (t - o, A); }), amberC.withAlpha (0.35f));
                glowLine (g, curve (pl, o, [&] (float t) { return 0.8f * swelled (t - o, a); }), amberC, i == 0 ? &pl : nullptr);
            }
            tag (g, "SLOW NOTES", pl.x (0.05f), pl.base - pl.h + 16.f, Colour (0xcce89a45));
            tag (g, "FAST NOTES", pl.x (2.1f), pl.base - pl.h + 16.f, Colour (0xcce89a45));
            return f > 0.005f ? "fast playing gets a shorter swell" : "off: every note uses the full ATTACK";
        }
        if (id == "even")
        {
            const float e = P ("even");
            const float amp[2] = { 0.42f, 1.f }, on[2] = { 0.f, 1.7f };
            for (int i = 0; i < 2; ++i)
            {
                const float o = on[i], a = amp[i], lev = a + (0.72f - a) * e;
                guitar (o, a);
                glowLine (g, curve (pl, o, [&] (float t) { return lev * swelled (t - o, A); }), amberC, &pl);
                tag (g, i == 0 ? "SOFT PICK" : "HARD PICK", pl.x (o) + 22.f, pl.base - pl.h + 16.f, Colour (0xcce89a45));
            }
            return e > 0.01f ? "soft and hard picks come out closer in level" : "off: each note keeps its own level";
        }

        // the plain swell, plus what the touched knob adds
        guitar (0.f, 1.f);
        tag (g, "GUITAR", pl.x (0.f) + 16.f, pl.y (0.9f), guitarC.withAlpha (0.8f));
        const float gain = id == "vol" ? juce::Decibels::decibelsToGain (P ("vol")) : 1.f;
        if (id == "vol") dashed (g, curve (pl, 0.f, [&] (float t) { return swelled (t, A); }), amberC.withAlpha (0.4f));
        glowLine (g, curve (pl, 0.f, [&] (float t) { return gain * swelled (t, A); }), amberC, &pl);
        marker (A, amberC);
        if (id == "dry")
        {
            const float d = P ("dry");
            if (d > 0.005f) glowLine (g, curve (pl, 0.f, [=] (float t) { return d * natural (t); }), paleC);
            return d > 0.005f ? "the unswelled guitar is mixed in: the pick is heard" : "off: only the swelled sound";
        }
        if (id == "feedback" || id == "fbRise")
        {
            const float fb = P ("feedback"), rise = P ("fbRise");
            if (fb > 0.005f)
            {
                auto ov = [=] (float t)
                {
                    const float x = juce::jlimit (0.f, 1.f, (t - A - 0.25f * rise) / (0.75f * rise));
                    return 0.8f * fb * x * x * (3.f - 2.f * x) * std::pow (10.f, -0.6f * t / 20.f) * 0.75f;
                };
                glowLine (g, curve (pl, A, ov), paleC);
                tag (g, "OVERTONE", pl.x (juce::jmin (A + rise, 2.6f)) + 4.f, pl.y (ov (juce::jmin (A + rise, 2.6f))) - 10.f, paleC);
            }
            return fb > 0.005f ? "a held note blooms into an overtone" : "off: no feedback";
        }
        if (id == "sustain" || id == "sustainMax") return P ("sustain") > 0.005f ? "the note fades more slowly than the guitar does" : "off: the note fades like the guitar";
        if (id == "vol") return "output level (dashed: 0 dB)";
        if (id == "swellShape") return P ("swellShape") > 0.5f ? "bowed: a slower, S-shaped start" : "classic: the pedal's curve";
        return A < 0.002f ? "no swell: the note starts at once" : "each note fades in over this time";
    }

    // ---------------------------------------------------------------- the filter picture
    String paintFilter (Graphics& g, Rect b)
    {
        const Colour teal (0xff7fd1bd), guitarC (0xffd8c3a2);
        const float x0 = 16.f, w = b.getWidth() - 32.f, top = 30.f, bot = b.getHeight() - 30.f;
        const float fLo = 60.f, fHi = 14000.f;
        auto X = [&] (float f) { return x0 + w * std::log (f / fLo) / std::log (fHi / fLo); };
        auto Y = [&] (float db) { return juce::jlimit (top - 4.f, bot, top + (bot - top) * (16.f - db) / 58.f); };
        g.setColour (Colour (0x33ff9a3a)); g.fillRect (x0, Y (0.f), w, 1.f);
        const std::pair<float, const char*> marks[] = { { 100.f, "100" }, { 1000.f, "1 k" }, { 10000.f, "10 k" } };
        for (auto& m : marks)
        {
            g.setColour (Colour (0x22ff9a3a)); g.fillRect (X (m.first), top, 1.f, bot - top + 4.f);
            tag (g, m.second, X (m.first) + 3.f, bot - 7.f, Colour (0x88e89a45));
        }
        const bool on = P ("filterOn") > 0.5f;
        const float fc = P ("freq"), q = 0.5f * std::pow (20.f, juce::jlimit (0.f, 1.f, P ("res")));
        const int slope = juce::roundToInt (P ("slope"));
        auto response = [&] (float cut)
        {
            juce::Path p;
            for (int i = 0; i <= 140; ++i)
            {
                const float f = fLo * std::pow (fHi / fLo, (float) i / 140.f), r = f / cut, r2 = r * r;
                float h2 = slope == 0 ? 1.f / (1.f + r2) : 1.f / ((1.f - r2) * (1.f - r2) + r2 / (q * q));
                if (slope >= 2) h2 /= (1.f + r2 * r2);
                if (slope == 3) h2 /= (1.f + r2 * r2) * (1.f + r2 * r2);
                const float y = Y (10.f * std::log10 (h2 + 1.0e-9f));
                if (i == 0) p.startNewSubPath (X (f), y); else p.lineTo (X (f), y);
            }
            return p;
        };
        if (! on)
        {
            juce::Path flat; flat.startNewSubPath (x0, Y (0.f)); flat.lineTo (x0 + w, Y (0.f));
            glowLine (g, flat, guitarC);
            dashed (g, response (fc), teal.withAlpha (0.45f));
            return "FILTER is off: nothing is cut. Switch it on in the panel";
        }
        const float mod = P ("mod"), oct = std::abs (mod) < 0.03f ? 0.f : std::abs (mod) * 6.f;
        const float other = fc / std::pow (2.f, oct);      // the far end of the sweep (always below TONE)
        const float dyn = P ("pickFilter"), dynOct = dyn * P ("pickDepth") * 0.5f;
        if (focusId == "pickFilter" && std::abs (dyn) > 0.02f)
        {
            dashed (g, response (juce::jlimit (40.f, 16000.f, fc * std::pow (2.f, dynOct))), guitarC.withAlpha (0.6f));
            dashed (g, response (juce::jlimit (40.f, 16000.f, fc * std::pow (2.f, -dynOct))), guitarC.withAlpha (0.35f));
            tag (g, "HARD PICK", X (juce::jlimit (80.f, 5000.f, fc * std::pow (2.f, dynOct))) + 4.f, top + 20.f, guitarC.withAlpha (0.85f));
        }
        Plot pl { x0, w, bot, bot - top };
        if (oct > 0.f)
        {
            // the cutoff travels during the swell: from the dashed curve to the solid one
            const float from = mod > 0.f ? other : fc, to = mod > 0.f ? fc : other;
            dashed (g, response (juce::jmax (40.f, from)), teal.withAlpha (0.6f));
            glowLine (g, response (juce::jmax (40.f, to)), teal, &pl);
            const float ya = top + 4.f, xa = X (juce::jmax (fLo, from)), xb = X (juce::jmax (fLo, to));
            g.setColour (teal); g.fillRect (juce::jmin (xa, xb), ya, std::abs (xb - xa), 1.5f);
            juce::Path head; const float d = xb > xa ? -1.f : 1.f;
            head.addTriangle (xb, ya + 0.75f, xb + d * 7.f, ya - 3.5f, xb + d * 7.f, ya + 5.f);
            g.fillPath (head);
            tag (g, "START", xa + (xb > xa ? 0.f : -34.f), ya + 11.f, teal.withAlpha (0.8f));
            if (focusId == "mod") return mod > 0.f ? "the filter opens up to TONE during the swell" : "the filter closes down from TONE during the swell";
        }
        else glowLine (g, response (fc), teal, &pl);
        if (focusId == "res") return "a peak at the filter frequency";
        if (focusId == "pickFilter") return std::abs (dyn) > 0.02f ? (dyn > 0.f ? "harder picks open the filter further" : "harder picks close the filter") : "off: picks do not move the filter";
        if (focusId == "mod") return "noon: the filter stays where TONE puts it";
        return "everything above this is turned down";
    }

    void paint (Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        juce::Path clip; clip.addRoundedRectangle (b, 5.f);
        g.reduceClipRegion (clip);
        g.setGradientFill (juce::ColourGradient (Colour (0xff3a1a08), b.getCentreX(), b.getHeight() * 0.55f, Colour (0xff0c0503), 0.f, 0.f, true));
        g.fillAll();

        const float hl = lastHold;
        if (hl > 0.f) { g.setColour (Colour (0xffffb347).withAlpha (0.07f * hl)); g.fillAll(); }

        // ---- live ribbons (they step back while a knob is being explained)
        const float live = 1.f - 0.88f * viewA;
        g.beginTransparencyLayer (live);
        g.setColour (Colour (0x1aff9a3a));
        for (int i = 1; i < 4; ++i) g.fillRect (std::floor (b.getWidth() * (float) i / 4.f), 0.f, 1.f, b.getHeight());
        for (int i = 1; i < 5; ++i) g.fillRect (0.f, std::floor (b.getHeight() * (float) i / 5.f), b.getWidth(), 1.f);
        const float dx = b.getWidth() / (float) (kCols - 1);
        g.setColour (Colour (0x26ffc777));
        for (int i = 0; i < kCols; ++i)
            if (pick[(size_t) ((head + i) % kCols)]) g.fillRect ((float) i * dx, b.getHeight() - 20.f, 1.f, 6.f);

        for (int band = kLoBand; band <= kHiBand; ++band)
        {
            const float y = bandY (band);
            int start = -1;
            for (int i = 0; i <= kCols; ++i)
            {
                const float t = i < kCols ? thick[(size_t) ((head + i) % kCols)][(size_t) band] : 0.f;
                if (t > 0.f && start < 0) start = i;
                if (t <= 0.f && start >= 0)
                {
                    juce::Path p; float hotSum = 0.f;
                    const float x0 = (float) juce::jmax (0, start - 1) * dx;
                    p.startNewSubPath (x0, y);
                    for (int k = start; k < i; ++k)
                    {
                        const auto idx = (size_t) ((head + k) % kCols);
                        p.lineTo ((float) k * dx, y - (0.7f + 6.f * thick[idx][(size_t) band]));
                        hotSum += hot[idx][(size_t) band];
                    }
                    const float x1 = (float) juce::jmin (kCols - 1, i) * dx;
                    if (i < kCols) p.lineTo (x1, y);
                    for (int k = i - 1; k >= start; --k)
                        p.lineTo ((float) k * dx, y + (0.7f + 6.f * thick[(size_t) ((head + k) % kCols)][(size_t) band]));
                    p.closeSubPath();
                    const float h = hotSum / (float) (i - start);
                    const Colour bright = Colour (0xffffb347).interpolatedWith (Colour (0xfffff1cf), h);
                    g.setColour (Colour (0xffff8a1f).withAlpha (0.10f)); g.strokePath (p, juce::PathStrokeType (9.f, juce::PathStrokeType::curved));
                    g.setColour (Colour (0xffff8a1f).withAlpha (0.16f)); g.strokePath (p, juce::PathStrokeType (4.f, juce::PathStrokeType::curved));
                    g.setGradientFill (juce::ColourGradient (Colour (0xffc8641a).interpolatedWith (bright, 0.25f).withAlpha (0.85f), 0.f, 0.f, bright, b.getWidth(), 0.f, false));
                    g.fillPath (p);
                    start = -1;
                }
            }
        }
        g.endTransparencyLayer();

        // ---- the picture of the knob under the mouse
        const int kind = viewKind (focusId);
        if (viewA > 0.01f && kind != 0)
        {
            g.beginTransparencyLayer (viewA);
            const String says = kind == 1 ? paintSwell (g, b) : paintFilter (g, b);
            g.setFont (monoFont (13.f));
            g.setColour (kind == 2 ? Colour (0xff9fe3d2) : Colour (0xffffc777));
            g.drawText (roName + "  " + roValue, b.reduced (12, 7), juce::Justification::topLeft);
            g.setFont (uiFont (12.f, false, 0.03f));
            g.setColour (Colour (0xffe9c79a));
            g.drawText (says, b.reduced (12, 5), juce::Justification::bottomRight);
            g.endTransparencyLayer();
        }
        else if (roTicks > 0)
        {
            g.setFont (monoFont (11.5f));
            g.setColour (Colour (0xffe89a45).withAlpha (juce::jmin (1.f, (float) roTicks / 10.f)));
            g.drawText (roName + "  " + roValue, b.reduced (10, 5), juce::Justification::bottomLeft);
        }
        g.setFont (monoFont (11.5f));
        if (lastByp) { g.setColour (Colour (0xffe89a45)); g.drawText ("BYPASSED", b.reduced (10, 7), juce::Justification::topRight); }
        else if (hl > 0.f) { g.setColour (Colour (0xfffff1cf).withAlpha (0.4f + 0.6f * hl)); g.drawText ("HOLD", b.reduced (10, 7), juce::Justification::topRight); }

        // glass: a soft reflection and darker edges
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.13f), 0.f, 0.f, juce::Colours::transparentWhite, b.getWidth() * 0.5f, b.getHeight() * 0.9f, false));
        g.fillAll();
        const float e = 14.f;
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.6f), 0, 0, juce::Colours::transparentBlack, 0, e, false)); g.fillRect (0.f, 0.f, b.getWidth(), e);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.6f), 0, b.getHeight(), juce::Colours::transparentBlack, 0, b.getHeight() - e, false)); g.fillRect (0.f, b.getHeight() - e, b.getWidth(), e);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.6f), 0, 0, juce::Colours::transparentBlack, e, 0, false)); g.fillRect (0.f, 0.f, e, b.getHeight());
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.6f), b.getWidth(), 0, juce::Colours::transparentBlack, b.getWidth() - e, 0, false)); g.fillRect (b.getWidth() - e, 0.f, e, b.getHeight());
        if (lastByp) { g.setColour (juce::Colours::black.withAlpha (0.45f)); g.fillAll(); }
    }

    PolySwellProcessor& proc;
    std::array<std::array<float, kB>, kCols> thick {}, hot {};
    std::array<bool, kCols> pick {};
    std::array<float, kB> sm {};
    int head = 0, idle = 0, quiet = 0, roTicks = 0, linger = 0;
    uint32_t lastFrame = 0, lastSeq = 0;
    float lastHold = 0.f, viewA = 0.f;
    bool lastByp = false, hadReadout = false, focusHeld = false;
    String roName, roValue, focusId;
};
} // namespace

// ================================================================================================== board
struct PolySwellEditor::Board : juce::Component, Host, juce::Timer
{
    Board (PolySwellEditor& e, PolySwellProcessor& p) : editor (e), proc (p), window (p)
    {
        setLookAndFeel (&look);
        setSize (kBaseW, kBaseH);
        addAndMakeVisible (window);
        window.setBounds (juce::roundToInt (kPedalX + 28), juce::roundToInt (kPedalY + 30), 336, 152);

        buildBar();
        buildPedal();
        buildPanel();
        setPanelVisible (proc.uiPedalsOpen);
        startTimerHz (15);
    }
    ~Board() override { setLookAndFeel (nullptr); }

    // ---- Host
    void readout (const String& name, const String& value) override { window.setReadout (name, value); }
    void focus (const String& paramId, bool on) override { window.setFocus (paramId, on); }
    void showKnobView (const String& paramId, bool on)   // for tools/ui_snapshot
    {
        for (auto* k : knobs) if (k->id == paramId) { k->show(); window.setFocus (paramId, on); }
    }

    void setParam (const char* id, float plain)
    {
        if (auto* prm = proc.apvts.getParameter (id))
        { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (plain)); prm->endChangeGesture(); }
    }
    float getParam (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    // ---- building blocks
    struct Text { String s; Rect r; float size; Colour c; juce::Justification j; bool bold; float track; };
    void label (const String& s, Rect r, float size, Colour c, juce::Justification j = juce::Justification::centred, bool bold = true, float track = 0.14f)
    { texts.push_back ({ s, r, size, c, j, bold, track }); }

    Knob* knob (const char* id, const String& name, int kind, float cx, float cy, Colour ac, juce::Array<juce::Component*>& group)
    {
        auto* k = knobs.add (new Knob (proc, *this, id, name, kind, ac));
        const int sz = kind == 0 ? 124 : kind == 1 ? 78 : 64;
        k->setBounds (juce::roundToInt (cx) - sz / 2, juce::roundToInt (cy) - sz / 2, sz, sz);
        addAndMakeVisible (k); group.add (k);
        return k;
    }
    Knob* valueChip (const char* id, const String& name, int width, Colour ac, juce::Array<juce::Component*>& group)
    {
        auto* k = knobs.add (new Knob (proc, *this, id, name, 3, ac));
        juce::ignoreUnused (width);
        k->setSize (k->chipWidth(), 22);
        addAndMakeVisible (k); group.add (k);
        return k;
    }
    Chip* chip (Chip::Mode m, const char* id, const String& text, Colour ac, const String& tip, juce::Array<juce::Component*>& group)
    {
        auto* c = chips.add (new Chip (proc, m, id, text, ac));
        c->setTooltip (tip);
        c->setSize (c->idealWidth(), 22);
        addAndMakeVisible (c); group.add (c);
        return c;
    }
    static void flow (std::initializer_list<juce::Component*> row, float x, float y, float gap = 6.f)
    {
        for (auto* c : row) { c->setTopLeftPosition (juce::roundToInt (x), juce::roundToInt (y)); x += (float) c->getWidth() + gap; }
    }

    // ---- top bar
    void buildBar()
    {
        addAndMakeVisible (prevB); addAndMakeVisible (nextB); addAndMakeVisible (lcd);
        addAndMakeVisible (undoB); addAndMakeVisible (redoB); addAndMakeVisible (gearB); addAndMakeVisible (panelB);
        prevB.onClick = [this] { stepPreset (-1); };
        nextB.onClick = [this] { stepPreset (1); };
        lcd.onClick = [this] { presetMenu(); };
        undoB.onClick = [this] { proc.undo.undo(); };
        redoB.onClick = [this] { proc.undo.redo(); };
        gearB.onClick = [this] { settingsMenu(); };
        panelB.onClick = [this] { editor.setPanelOpen (! proc.uiPedalsOpen); };

        saveC = chip (Chip::custom, "", "SAVE", col::amber, "Save the current sound as your own preset.", barOnly);
        saveC->mode = Chip::toggle; saveC->litFn = [] { return false; };
        saveC->setSize (saveC->idealWidth(), 22);
        saveC->onPress = [this] { savePreset(); };
        for (int i = 0; i < 2; ++i)
        {
            auto* c = chip (Chip::custom, "", i == 0 ? "A" : "B", col::amber, "A / B: two versions of the sound to compare. Click the other one to switch.", barOnly);
            c->mode = Chip::toggle; c->setSize (26, 22);
            c->litFn = [this, i] { return abSlot == i; };
            c->onPress = [this, i] { switchAb (i); };
            abC[i] = c;
        }
    }

    void layoutBar (bool open)
    {
        const int y = 13;
        const int right = (open ? kBaseW : kPedalOnlyW) - 22;
        prevB.setBounds (22, y - 3, 28, 28);
        panelB.setBounds (right - 28, y - 3, 28, 28);
        gearB.setBounds (right - 60, y - 3, 28, 28);
        const int lcdW = open ? 250 : right - 60 - 8 - 28 - 4 - 54;
        lcd.setBounds (54, y - 3, lcdW, 28);
        nextB.setBounds (54 + lcdW + 4, y - 3, 28, 28);
        for (auto* c : barOnly) c->setVisible (open);
        undoB.setVisible (open); redoB.setVisible (open);
        if (open)
        {
            redoB.setBounds (right - 98, y - 3, 28, 28);
            undoB.setBounds (right - 130, y - 3, 28, 28);
            abC[1]->setTopLeftPosition (right - 168, y); abC[0]->setTopLeftPosition (right - 196, y);
            saveC->setTopLeftPosition (54 + lcdW + 44, y);
        }
    }

    // ---- pedal
    void buildPedal()
    {
        juce::Array<juce::Component*> g;   // the pedal's controls are always visible
        const float px = kPedalX, py = kPedalY;
        const float bx[3] = { 77.f, 196.f, 315.f };
        const char* bid[3] = { "attack", "sustain", "freq" }; const char* bn[3] = { "ATTACK", "SUSTAIN", "TONE" };
        for (int i = 0; i < 3; ++i)
        {
            knob (bid[i], bn[i], 0, px + bx[i], py + 283, col::amber, g);
            label (bn[i], { px + bx[i] - 60, py + 333, 120, 16 }, 13.5f, col::ink, juce::Justification::centred, true, 0.18f);
        }
        const float sx[5] = { 56.f, 126.f, 196.f, 266.f, 336.f };
        const char* sid[5] = { "touch", "follow", "feedback", "dry", "vol" }; const char* sn[5] = { "FEEL", "PACE", "FEEDBACK", "DRY", "LEVEL" };
        for (int i = 0; i < 5; ++i)
        {
            knob (sid[i], sn[i], 1, px + sx[i], py + 392, col::amber, g);
            label (sn[i], { px + sx[i] - 45, py + 425, 90, 14 }, 11.5f, col::ink2, juce::Justification::centred, true, 0.16f);
        }

        // the swell's own options, in one row under the window
        auto* shape = chip (Chip::menu, "swellShape", "", col::amber, "Swell shape: Classic (the pedal's curve) or Bowed (a slower, S-shaped start).", g);
        auto* even = valueChip ("even", "EVEN", 84, col::amber, g);
        auto* sync = chip (Chip::custom, "", "SYNC", col::amber, "Tempo sync: ATTACK becomes a note length of the host tempo.", g);
        sync->textFn = [this] { return getParam ("sync") > 0.5f ? "SYNC " + proc.apvts.getParameter ("syncDiv")->getCurrentValueAsText() : String ("SYNC OFF"); };
        sync->litFn = [this] { return getParam ("sync") > 0.5f; };
        sync->setSize (86, 22);
        sync->onPress = [this, sync]
        {
            juce::PopupMenu m;
            m.addItem (1, "Off", true, getParam ("sync") < 0.5f);
            const auto names = proc.apvts.getParameter ("syncDiv")->getAllValueStrings();
            for (int i = 0; i < names.size(); ++i) m.addItem (10 + i, names[i], true, getParam ("sync") > 0.5f && juce::roundToInt (getParam ("syncDiv")) == i);
            m.setLookAndFeel (&look);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sync), [this, sync] (int r)
            {
                if (r == 1) setParam ("sync", 0.f);
                else if (r >= 10) { setParam ("syncDiv", (float) (r - 10)); setParam ("sync", 1.f); }
                sync->repaint();
            });
        };
        const float total = (float) (shape->getWidth() + even->getWidth() + sync->getWidth()) + 16.f;
        flow ({ shape, even, sync }, px + (kPedalW - total) * 0.5f, py + 197, 8.f);

        addAndMakeVisible (onLed); addAndMakeVisible (holdLed);
        onLed.setBounds (juce::roundToInt (px + 108 - 14), juce::roundToInt (py + 533), 28, 22);
        holdLed.setBounds (juce::roundToInt (px + 284 - 14), juce::roundToInt (py + 533), 28, 22);
        addAndMakeVisible (onSw); addAndMakeVisible (holdSw);
        onSw.setBounds (juce::roundToInt (px + 108 - 44), juce::roundToInt (py + 550), 88, 80);
        holdSw.setBounds (juce::roundToInt (px + 284 - 44), juce::roundToInt (py + 550), 88, 80);
        onSw.setTooltip ("ON: switches the pedal in or out (bypass).");
        holdSw.setTooltip ("HOLD: freezes what is ringing into a pad you can play over.");
        onSw.onDown = [this] { setParam ("bypass", getParam ("bypass") > 0.5f ? 0.f : 1.f); };
        holdSw.onDown = [this] { setParam ("holdOn", getParam ("holdLatch") > 0.5f ? (getParam ("holdOn") > 0.5f ? 0.f : 1.f) : 1.f); };
        holdSw.onUp = [this] { if (getParam ("holdLatch") < 0.5f) setParam ("holdOn", 0.f); };
        label ("ON", { px + 108 - 40, py + 624, 80, 16 }, 13.5f, col::ink, juce::Justification::centred, true, 0.18f);
        label ("HOLD", { px + 284 - 40, py + 624, 80, 16 }, 13.5f, col::ink, juce::Justification::centred, true, 0.18f);
    }

    // ---- panel
    void buildPanel()
    {
        auto& g = panelOnly;
        auto knobRow = [&] (int mod, std::initializer_list<std::pair<const char*, const char*>> ks)
        {
            const auto r = moduleRect (mod);
            const float step = (r.getWidth() - 28.f) / (float) ks.size();
            int i = 0;
            for (auto& k : ks)
            {
                const float cx = r.getX() + 14.f + step * ((float) i++ + 0.5f);
                knob (k.first, k.second, 2, cx, r.getY() + 71, kMods[mod].ac, g);
                label (k.second, { cx - 45, r.getY() + 101, 90, 13 }, 11.f, col::chipText, juce::Justification::centred, true, 0.14f);
            }
        };
        auto rowY = [] (int mod, int row) { return moduleRect (mod).getY() + (row == 0 ? 132.f : 162.f); };
        auto rowX = [] (int mod) { return moduleRect (mod).getX() + 14.f; };

        for (int i = 0; i < 6; ++i)
        {
            const auto r = moduleRect (i);
            Led* l = leds.add (new Led (proc, kMods[i].onId != nullptr ? kMods[i].onId : "holdOn", false,
                                        String (kMods[i].title) + (i == 4 ? ": lit while a pad is held." : ": click to switch this module on or off.")));
            l->setBounds (juce::roundToInt (r.getX() + 8), juce::roundToInt (r.getY() + 8), 26, 24);
            addAndMakeVisible (l); g.add (l);
            label (kMods[i].title, { r.getX() + 38, r.getY() + 10, 120, 20 }, 16.f, kMods[i].ac.interpolatedWith (col::cream, 0.4f), juce::Justification::centredLeft, true, 0.2f);
            const float tw = juce::GlyphArrangement::getStringWidth (uiFont (16.f, true, 0.2f), kMods[i].title);
            label (kMods[i].sub, { r.getX() + 48 + tw, r.getY() + 11, r.getWidth() - 60 - tw, 20 }, 12.5f, col::dimText, juce::Justification::centredLeft, false, 0.03f);
        }

        // FILTER
        knobRow (0, { { "mod", "MOD" }, { "res", "RES" }, { "pickFilter", "DYN" } });
        {
            auto* slope = chip (Chip::menu, "slope", "", kMods[0].ac, "Slope: how steeply the filter cuts.", g);
            bodyC = chip (Chip::custom, "", "BODY", kMods[0].ac, "Body: the VG-8 bowed tone curve and violin body impulse responses.", g);
            bodyC->textFn = [this]
            {
                const int ir = juce::roundToInt (getParam ("bodyIr"));
                if (ir > 0) return proc.apvts.getParameter ("bodyIr")->getCurrentValueAsText().toUpperCase();
                return String (getParam ("bowedBody") > 0.5f ? "BODY VG-8" : "BODY OFF");
            };
            bodyC->litFn = [this] { return getParam ("bodyIr") > 0.5f || getParam ("bowedBody") > 0.5f; };
            bodyC->setSize (96, 22);
            bodyC->onPress = [this] { bodyMenu(); };
            auto* mix = valueChip ("bodyMix", "MIX", 76, kMods[0].ac, g);
            flow ({ slope, bodyC, mix }, rowX (0), rowY (0, 0));
        }
        // FEEDBACK
        knobRow (1, { { "fbRise", "RISE" }, { "fbTone", "TONE" }, { "sustainMax", "MAX" } });
        flow ({ chip (Chip::menu, "fbHarmonic", "", kMods[1].ac, "Which overtone the feedback blooms into.", g),
                chip (Chip::menu, "fbMode", "", kMods[1].ac, "Bloom: the overtone grows on held notes. Immediate: it sounds from the pick, like an octave pedal.", g) },
              rowX (1), rowY (1, 0));
        // MOTION
        knobRow (2, { { "vibDepth", "VIBRATO" }, { "tremolo", "TREMOLO" }, { "spread", "SPREAD" }, { "detune", "DETUNE" } });
        {
            // what the vibrato moves, and its feel, in one chip
            auto* vt = chip (Chip::custom, "", "VIB ON", kMods[2].ac, "What the vibrato moves (voice, guitar or both), and HUMAN: it starts shallow and slow, then settles.", g);
            vt->textFn = [this] { return shortChoice (proc.apvts.getParameter ("vibTarget")->getCurrentValueAsText()) + (getParam ("vibHuman") > 0.5f ? " HUMAN" : ""); };
            vt->setSize (112, 22);
            vt->onPress = [this, vt]
            {
                juce::PopupMenu m;
                m.addSectionHeader ("Vibrato on");
                const auto names = proc.apvts.getParameter ("vibTarget")->getAllValueStrings();
                for (int i = 0; i < names.size(); ++i) m.addItem (10 + i, names[i], true, juce::roundToInt (getParam ("vibTarget")) == i);
                m.addSeparator();
                m.addItem (1, "Human feel", true, getParam ("vibHuman") > 0.5f);
                m.setLookAndFeel (&look);
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (vt), [this, vt] (int r)
                {
                    if (r == 1) setParam ("vibHuman", getParam ("vibHuman") > 0.5f ? 0.f : 1.f);
                    else if (r >= 10) setParam ("vibTarget", (float) (r - 10));
                    vt->repaint();
                });
            };
            flow ({ valueChip ("vibRate", "VIB", 86, kMods[2].ac, g), valueChip ("vibDelay", "DLY", 88, kMods[2].ac, g), vt }, rowX (2), rowY (2, 0), 5.f);
        }
        flow ({ valueChip ("tremRate", "TREM", 92, kMods[2].ac, g),
                chip (Chip::menu, "tremShape", "", kMods[2].ac, "Tremolo shape.", g),
                chip (Chip::menu, "spreadMode", "", kMods[2].ac, "How SPREAD places the notes.", g) },
              rowX (2), rowY (2, 1), 5.f);
        // LAYER
        knobRow (3, { { "voiceMix", "MIX" }, { "voiceTone", "TONE" }, { "voiceGlide", "GLIDE" }, { "voiceCharacter", "CHARACTER" } });
        {
            auto* waves = segs.add (new Seg (proc, "voiceWave", { { "SAW", 1 }, { "BOW", 8 }, { "PAD", 7 }, { "GLASS", 5 } }, kMods[3].ac));
            waves->setTooltip ("The voice's sound: saw (strings), bow, bowed pad, glass.");
            waves->setSize (waves->idealWidth(), 22); addAndMakeVisible (waves); g.add (waves);
            auto* more = chip (Chip::menu, "voiceWave", "", kMods[3].ac, "All nine waves.", g);
            more->textFn = [] { return String ("MORE"); };
            more->setSize (60, 22);
            flow ({ waves, more }, rowX (3), rowY (3, 0));
            auto* oct = segs.add (new Seg (proc, "voiceOct", { { "-1", 0 }, { "0", 1 }, { "+1", 2 } }, kMods[3].ac));
            oct->setTooltip ("The voice's octave.");
            oct->setSize (oct->idealWidth(), 22); addAndMakeVisible (oct); g.add (oct);
            flow ({ oct, chip (Chip::toggle, "voiceMono", "MONO", kMods[3].ac, "Mono: only the newest note keeps its voice (for lead lines).", g),
                    valueChip ("voiceUnison", "UNISON", 104, kMods[3].ac, g) }, rowX (3), rowY (3, 1));
        }
        // HOLD
        knobRow (4, { { "holdFade", "FADE" }, { "holdLevel", "LEVEL" } });
        flow ({ chip (Chip::toggle, "holdLatch", "LATCH", kMods[4].ac, "Latch: the footswitch toggles HOLD. Off: it holds only while pressed.", g) }, rowX (4), rowY (4, 0));
        label ("Press HOLD on the pedal to freeze what is ringing, then play over it.",
               { rowX (4), rowY (4, 1) - 2, kModW - 28, 26 }, 12.f, col::dimText, juce::Justification::centredLeft, false, 0.03f);
        // SPACE
        knobRow (5, { { "smearSize", "SIZE" }, { "smearLevel", "LEVEL" }, { "smearDrive", "DRIVE" } });
        flow ({ chip (Chip::menu, "smearSend", "SEND", kMods[5].ac, "What feeds the reverb: always, following the swell, or an expression pedal.", g),
                chip (Chip::toggle, "smearDark", "DARK", kMods[5].ac, "A darker reverb.", g),
                chip (Chip::toggle, "smearGate", "GATE", kMods[5].ac, "Gate: the reverb is cut shortly after you stop playing.", g) }, rowX (5), rowY (5, 0));
        flow ({ valueChip ("smearHold", "HOLD", 86, kMods[5].ac, g), valueChip ("smearCut", "CUT", 78, kMods[5].ac, g),
                valueChip ("smearThresh", "THR", 86, kMods[5].ac, g) }, rowX (5), rowY (5, 1));
    }

    void setPanelVisible (bool open)
    {
        for (auto* c : panelOnly) c->setVisible (open);
        layoutBar (open);
        panelOpen = open;
        bg = {};
        repaint();
    }

    // ---- menus
    void stepPreset (int d)
    {
        const int n = proc.getNumPrograms();
        proc.setCurrentProgram ((proc.getCurrentProgram() + d + n) % n);
        setParam ("holdOn", 0.f);
    }

    void presetMenu()
    {
        juce::PopupMenu m;
        m.addSectionHeader ("Factory");
        for (int i = 0; i < proc.getNumPrograms(); ++i)
            m.addItem (100 + i, proc.getProgramName (i), true, ! proc.userPresetLoaded && i == proc.getCurrentProgram());
        const auto user = PolySwellProcessor::userPresets();
        if (! user.isEmpty()) m.addSectionHeader ("Yours");
        for (int i = 0; i < user.size(); ++i)
            m.addItem (1000 + i, user[i].getFileNameWithoutExtension(), true, proc.userPresetLoaded && proc.currentPresetName == user[i].getFileNameWithoutExtension());
        m.addSeparator();
        m.addItem (1, "Save as...");
        m.setLookAndFeel (&look);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&lcd).withMaximumNumColumns (2), [this, user] (int r)
        {
            if (r == 1) savePreset();
            else if (r >= 1000) { proc.loadUserPreset (user[r - 1000]); setParam ("holdOn", 0.f); }
            else if (r >= 100) { proc.setCurrentProgram (r - 100); setParam ("holdOn", 0.f); }
        });
    }

    void savePreset()
    {
        auto* aw = new juce::AlertWindow ("Save preset", "Name of your preset", juce::MessageBoxIconType::NoIcon, this);
        aw->setLookAndFeel (&look);
        aw->addTextEditor ("name", proc.userPresetLoaded ? proc.currentPresetName : String());
        aw->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, safe = juce::Component::SafePointer<Board> (this)] (int r)
        {
            if (safe != nullptr && r == 1) proc.saveUserPreset (aw->getTextEditorContents ("name"));
        }), true);
    }

    void switchAb (int slot)
    {
        if (slot == abSlot) return;
        abState[(size_t) abSlot] = proc.apvts.copyState();
        if (abState[(size_t) slot].isValid()) proc.apvts.replaceState (abState[(size_t) slot].createCopy());
        abSlot = slot;
        abC[0]->repaint(); abC[1]->repaint();
    }

    void bodyMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "VG-8 bowed tone curve", true, getParam ("bowedBody") > 0.5f);
        m.addSectionHeader ("Body impulse response");
        const auto names = proc.apvts.getParameter ("bodyIr")->getAllValueStrings();
        const int cur = juce::roundToInt (getParam ("bodyIr"));
        for (int i = 0; i < names.size(); ++i)
            if (i < 8 || proc.customIr.existsAsFile())
                m.addItem (10 + i, i == 8 ? "Your IR: " + proc.customIr.getFileNameWithoutExtension() : names[i], true, i == cur);
        m.addSeparator();
        m.addItem (2, "Load your own IR...");
        m.setLookAndFeel (&look);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (bodyC), [this] (int r)
        {
            if (r == 1) setParam ("bowedBody", getParam ("bowedBody") > 0.5f ? 0.f : 1.f);
            else if (r >= 10) setParam ("bodyIr", (float) (r - 10));
            else if (r == 2)
            {
                chooser = std::make_unique<juce::FileChooser> ("Load an impulse response", proc.customIr.existsAsFile() ? proc.customIr : juce::File(), "*.wav;*.aif;*.aiff;*.flac");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                      [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) proc.setCustomIr (fc.getResult()); bodyC->repaint(); });
            }
            bodyC->repaint();
        });
    }

    void settingsMenu()
    {
        juce::PopupMenu m, midi, size;
        m.addSectionHeader ("Swell");
        m.addItem (1, "Smooth re-pick (no jump when re-picking a ringing note)", true, getParam ("smoothRepick") > 0.5f);
        m.addSectionHeader ("Output");
        m.addItem (2, "Safety limiter", true, getParam ("limiter") > 0.5f);
        m.addItem (3, "Low latency (10 ms; low notes separate less cleanly)", true, getParam ("lowLatency") > 0.5f);
        const auto mn = proc.apvts.getParameter ("midiOut")->getAllValueStrings();
        for (int i = 0; i < mn.size(); ++i) midi.addItem (20 + i, mn[i], true, juce::roundToInt (getParam ("midiOut")) == i);
        m.addSubMenu ("MIDI out of the played notes", midi);
        m.addSectionHeader ("Window");
        const float sizes[] = { 0.75f, 1.f, 1.25f, 1.5f, 2.f };
        for (int i = 0; i < 5; ++i) size.addItem (40 + i, String (juce::roundToInt (sizes[i] * 100)) + " %", true, std::abs (proc.uiScale - sizes[i]) < 0.01f);
        m.addSubMenu ("Size", size);
        m.addItem (4, panelOpen ? "Hide the effects panel" : "Show the effects panel");
        m.setLookAndFeel (&look);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&gearB), [this, sizes] (int r)
        {
            auto flip = [this] (const char* id) { setParam (id, getParam (id) > 0.5f ? 0.f : 1.f); };
            if (r == 1) flip ("smoothRepick");
            else if (r == 2) flip ("limiter");
            else if (r == 3) flip ("lowLatency");
            else if (r == 4) editor.setPanelOpen (! proc.uiPedalsOpen);
            else if (r >= 40) editor.setUiScale (sizes[r - 40]);
            else if (r >= 20) setParam ("midiOut", (float) (r - 20));
        });
    }

    // ---- state polling: things that change without a control of ours being touched
    void timerCallback() override
    {
        if (! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown()) proc.undo.beginNewTransaction();
        const String name = proc.currentPresetName;
        if (name != lcd.text) { lcd.text = name; lcd.repaint(); }
        const String num = proc.userPresetLoaded ? String ("USER") : String (proc.getCurrentProgram() + 1).paddedLeft ('0', 2);
        if (num != lcd.right) { lcd.right = num; lcd.repaint(); }
        undoB.setEnabled (proc.undo.canUndo()); redoB.setEnabled (proc.undo.canRedo());
        int mask = 0;
        for (int i = 0; i < 6; ++i) if (kMods[i].onId != nullptr && getParam (kMods[i].onId) < 0.5f) mask |= 1 << i;
        if (getParam ("filterOn") < 0.5f) mask |= 64;
        if (getParam ("bypass") > 0.5f) mask |= 128;      // bypassed: the name goes dark
        if (mask != offMask) { offMask = mask; repaint(); }
        for (auto* c : chips) if (c->textFn || c->litFn) c->repaint();
    }

    // ---- static artwork, baked at the current size
    void bake (float ps)
    {
        const int w = juce::roundToInt ((float) getWidth() * ps), h = juce::roundToInt ((float) getHeight() * ps);
        bg = juce::Image (juce::Image::RGB, juce::jmax (1, w), juce::jmax (1, h), false);
        bakedScale = ps;
        Graphics g (bg);
        g.addTransform (juce::AffineTransform::scale (ps));
        g.setImageResamplingQuality (Graphics::highResamplingQuality);

        if (! grain.isValid())
        {
            grain = juce::Image (juce::Image::ARGB, 192, 192, true);
            juce::Random r (7);
            for (int y = 0; y < 192; ++y) for (int x = 0; x < 192; ++x)
            { const float v = r.nextFloat(); grain.setPixelAt (x, y, (v > 0.5f ? juce::Colours::white : juce::Colours::black).withAlpha (std::abs (v - 0.5f) * 0.22f)); }
        }

        // the board everything sits on
        g.setGradientFill (juce::ColourGradient (Colour (0xff2a211c), (float) getWidth() * 0.3f, 0.f, Colour (0xff120e0c), (float) getWidth() * 0.3f, (float) getHeight() * 1.1f, true));
        g.fillAll();

        // --- pedal enclosure
        const Rect pr (kPedalX, kPedalY, kPedalW, kPedalH);
        juce::Path pedal; pedal.addRoundedRectangle (pr, 24.f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.8f), 30, { 0, 16 }).drawForPath (g, pedal);
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 6, { 0, 3 }).drawForPath (g, pedal);
        g.setGradientFill (juce::ColourGradient (col::body1, pr.getX(), pr.getY(), col::body2, pr.getRight(), pr.getBottom(), false));
        g.fillPath (pedal);
        {
            Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (pedal);
            g.setTiledImageFill (grain, 0, 0, 1.f); g.fillRect (pr);      // powder coat
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.14f), pr.getX(), pr.getY(), juce::Colours::transparentWhite, pr.getX() + pr.getWidth() * 0.75f, pr.getY() + pr.getHeight() * 0.42f, false));
            g.fillRect (pr);
            g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, 0, pr.getBottom() - 40, juce::Colours::black.withAlpha (0.3f), 0, pr.getBottom(), false));
            g.fillRect (pr);
        }
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.3f), 0, pr.getY(), juce::Colours::black.withAlpha (0.35f), 0, pr.getBottom(), false));
        g.drawRoundedRectangle (pr.reduced (0.75f), 23.5f, 1.5f);

        const float sxy[4][2] = { { 15, 15 }, { kPedalW - 15, 15 }, { 15, kPedalH - 15 }, { kPedalW - 15, kPedalH - 15 } };
        for (int i = 0; i < 4; ++i)
        {
            const float cx = pr.getX() + sxy[i][0], cy = pr.getY() + sxy[i][1], a = 0.6f + (float) i * 1.1f;
            g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillEllipse (cx - 7.5f, cy - 6.5f, 15, 15);
            g.setGradientFill (juce::ColourGradient (Colour (0xfff2efe8), cx - 3, cy - 5, Colour (0xff6b6864), cx + 4, cy + 6, false));
            g.fillEllipse (cx - 6.5f, cy - 6.5f, 13, 13);
            g.setColour (Colour (0xff2b2826));
            g.drawLine (cx - std::cos (a) * 4.5f, cy - std::sin (a) * 4.5f, cx + std::cos (a) * 4.5f, cy + std::sin (a) * 4.5f, 2.f);
        }

        // window bezel
        const Rect wr (pr.getX() + 20, pr.getY() + 22, 352, 168);
        g.setColour (juce::Colours::white.withAlpha (0.2f)); g.fillRoundedRectangle (wr.translated (0, 1.2f), 10.f);
        g.setColour (juce::Colours::black.withAlpha (0.5f)); g.fillRoundedRectangle (wr.translated (0, -1.f), 10.f);
        g.setColour (Colour (0xff0a0705)); g.fillRoundedRectangle (wr, 10.f);
        g.setColour (Colour (0xff1d1714)); g.drawRoundedRectangle (wr.reduced (1.5f), 9.f, 2.f);
        g.setColour (juce::Colours::white.withAlpha (0.06f)); g.drawRoundedRectangle (wr.reduced (3.f), 8.f, 1.f);

        // name plate
        g.setColour (col::ink.withAlpha (0.35f));
        g.fillRect (pr.getX() + 34, pr.getY() + 447, pr.getWidth() - 68, 1.f);
        g.fillRect (pr.getX() + 34, pr.getY() + 529, pr.getWidth() - 68, 1.f);
        g.setColour (col::ink);
        g.setFont (uiFont (11.5f, true, 0.5f));
        g.drawText ("BENNO", Rect (pr.getX(), pr.getY() + 452, pr.getWidth(), 14).translated (3, 0), juce::Justification::centred);
        g.setColour (col::ink.withAlpha (0.5f));      // unlit: printed on the enclosure; the lit version is drawn over it
        drawWordmark (g);
        g.setColour (col::ink2);
        g.setFont (uiFont (10.5f, false, 0.3f));
        g.drawText ("POLYPHONIC SWELL AND SUSTAIN", Rect (pr.getX(), pr.getY() + 512, pr.getWidth(), 13).translated (2, 0), juce::Justification::centred);

        // --- panel plate and its six modules
        if (panelOpen)
        {
            const Rect pl (kPanelX, kPanelY, kPanelW, kPanelH);
            juce::Path plate; plate.addRoundedRectangle (pl, 14.f);
            juce::DropShadow (juce::Colours::black.withAlpha (0.7f), 26, { 0, 14 }).drawForPath (g, plate);
            g.setGradientFill (juce::ColourGradient (Colour (0xff2c2622), pl.getX(), pl.getY(), Colour (0xff1a1513), pl.getX() + 80, pl.getBottom(), false));
            g.fillPath (plate);
            {
                Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (plate);
                g.setTiledImageFill (grain, 0, 0, 0.5f); g.fillRect (pl);
            }
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.14f), 0, pl.getY(), juce::Colours::black.withAlpha (0.3f), 0, pl.getBottom(), false));
            g.drawRoundedRectangle (pl.reduced (0.5f), 14.f, 1.f);
            for (int i = 0; i < 6; ++i)
            {
                const auto r = moduleRect (i);
                const Colour ac = kMods[i].ac;
                g.setColour (juce::Colours::white.withAlpha (0.07f)); g.fillRoundedRectangle (r.translated (0, 1.f), 8.f);
                g.setGradientFill (juce::ColourGradient (Colour (0xff15110f).interpolatedWith (ac, 0.13f), 0, r.getY(), Colour (0xff191412).interpolatedWith (ac, 0.06f), 0, r.getBottom(), false));
                g.fillRoundedRectangle (r, 8.f);
                {
                    Graphics::ScopedSaveState ss (g);
                    juce::Path mp; mp.addRoundedRectangle (r, 8.f);
                    g.reduceClipRegion (mp);
                    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.6f), 0, r.getY(), juce::Colours::transparentBlack, 0, r.getY() + 9, false));
                    g.fillRect (r.withHeight (9));
                }
                g.setColour (ac.withAlpha (0.22f)); g.drawRoundedRectangle (r.reduced (0.5f), 8.f, 1.f);
            }
        }

        for (auto& t : texts)
        {
            if (! panelOpen && t.r.getX() >= kPanelX) continue;
            g.setFont (uiFont (t.size, t.bold, t.track));
            g.setColour (t.c);
            g.drawFittedText (t.s, t.r.toNearestInt(), t.j, 2, 0.9f);
        }
    }

    static void drawWordmark (Graphics& g)
    {
        const Rect pr (kPedalX, kPedalY, kPedalW, kPedalH);
        g.setFont (juce::Font (juce::FontOptions ("Georgia", 46.f, juce::Font::bold | juce::Font::italic)));
        g.drawText ("Polyswell", Rect (pr.getX(), pr.getY() + 462, pr.getWidth() - 46, 52), juce::Justification::centred);
        g.setFont (juce::Font (juce::FontOptions ("Georgia", 27.f, juce::Font::bold | juce::Font::italic)));
        g.drawText ("II", Rect (pr.getX() + 285, pr.getY() + 474, 50, 36), juce::Justification::centredLeft);
    }

    // the name lit from within: a wide and a tight amber halo under warm-white letters (shown while the pedal is on)
    static Rect glowRect() { return { kPedalX, kPedalY + 440, kPedalW, 96 }; }
    void bakeGlow (float ps)
    {
        const auto gr = glowRect();
        const int w = juce::jmax (1, juce::roundToInt (gr.getWidth() * ps)), h = juce::jmax (1, juce::roundToInt (gr.getHeight() * ps));
        auto text = [&] (Colour c)
        {
            juce::Image img (juce::Image::ARGB, w, h, true);
            Graphics g (img);
            g.addTransform (juce::AffineTransform::translation (-gr.getX(), -gr.getY()).scaled (ps));
            g.setColour (c);
            drawWordmark (g);
            return img;
        };
        auto blurred = [&] (const juce::Image& src, float radius)
        {
            const int r = juce::jmax (1, juce::roundToInt (radius * ps));
            juce::ImageConvolutionKernel k (2 * r + 1);
            k.createGaussianBlur ((float) r * 0.5f);
            juce::Image out (juce::Image::ARGB, w, h, true);
            k.applyToImage (out, src, out.getBounds());
            return out;
        };
        const auto halo = text (Colour (0xffff9a2e));
        glow = juce::Image (juce::Image::ARGB, w, h, true);
        Graphics g (glow);
        const auto wide = blurred (halo, 11.f), tight = blurred (halo, 4.f);
        g.setOpacity (0.85f); g.drawImageAt (wide, 0, 0); g.drawImageAt (wide, 0, 0);
        g.setOpacity (1.f); g.drawImageAt (tight, 0, 0); g.drawImageAt (tight, 0, 0);
        g.drawImageAt (text (Colour (0xfffff3d6)), 0, 0);
    }

    void paint (Graphics& g) override
    {
        const float ps = g.getInternalContext().getPhysicalPixelScaleFactor();
        if (! bg.isValid() || std::abs (ps - bakedScale) > 0.001f) { bake (ps); bakeGlow (ps); }
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        g.drawImage (bg, getLocalBounds().toFloat());
        if (! (offMask & 128)) g.drawImage (glow, glowRect());
    }

    void paintOverChildren (Graphics& g) override
    {
        // a module that is switched off: everything under its header goes dark
        if (panelOpen)
            for (int i = 0; i < 6; ++i)
                if (offMask & (1 << i))
                {
                    const auto r = moduleRect (i);
                    g.setColour (Colour (0xff120e0c).withAlpha (0.62f));
                    g.fillRoundedRectangle (r.withTrimmedTop (34).reduced (1.f), 7.f);
                }
        if (offMask & 64)   // FILTER off: TONE does nothing
        {
            g.setColour (col::body2.withAlpha (0.6f));
            g.fillEllipse (kPedalX + 315 - 46, kPedalY + 283 - 46, 92, 92);
            g.setColour (col::ink2);
            g.setFont (uiFont (10.5f, true, 0.12f));
            g.drawText ("FILTER IS OFF", Rect (kPedalX + 315 - 60, kPedalY + 283 - 8, 120, 16), juce::Justification::centred);
        }
    }

    PolySwellEditor& editor;
    PolySwellProcessor& proc;
    Look look;
    SignalWindow window;
    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<Chip> chips;
    juce::OwnedArray<Seg> segs;
    juce::OwnedArray<Led> leds;
    juce::Array<juce::Component*> panelOnly, barOnly;
    std::vector<Text> texts;
    IconButton prevB { IconButton::prev, "Previous preset" }, nextB { IconButton::next, "Next preset" },
               undoB { IconButton::undo, "Undo" }, redoB { IconButton::redo, "Redo" },
               gearB { IconButton::gear, "Settings: re-pick, limiter, low latency, MIDI out, window size" },
               panelB { IconButton::panel, "Show or hide the effects panel" };
    Lcd lcd;
    Chip* saveC = nullptr; Chip* abC[2] { nullptr, nullptr }; Chip* bodyC = nullptr;
    Led onLed { proc, "bypass", true, "Lit while the pedal is on." }, holdLed { proc, "holdOn", false, "Lit while a pad is held." };
    FootSwitch onSw, holdSw;
    std::unique_ptr<juce::FileChooser> chooser;
    std::array<juce::ValueTree, 2> abState;
    int abSlot = 0, offMask = 0;
    bool panelOpen = true;
    juce::Image bg, grain, glow;
    float bakedScale = 0.f;
};

// ================================================================================================== editor
PolySwellEditor::PolySwellEditor (PolySwellProcessor& p) : juce::AudioProcessorEditor (&p), proc (p)
{
    board = std::make_unique<Board> (*this, p);
    addAndMakeVisible (*board);
    tooltips.setLookAndFeel (&board->look);
    applySize();
}

PolySwellEditor::~PolySwellEditor() { tooltips.setLookAndFeel (nullptr); }

void PolySwellEditor::paint (juce::Graphics& g) { g.fillAll (juce::Colour (0xff120e0c)); }

void PolySwellEditor::applySize()
{
    const float s = juce::jlimit (0.75f, 2.f, proc.uiScale);
    const int w = proc.uiPedalsOpen ? kBaseW : kPedalOnlyW;
    board->setTransform ({});
    board->setBounds (0, 0, w, kBaseH);
    board->setTransform (juce::AffineTransform::scale (s));
    setSize (juce::roundToInt ((float) w * s), juce::roundToInt ((float) kBaseH * s));
}

void PolySwellEditor::resized() {}

void PolySwellEditor::setPanelOpen (bool open)
{
    proc.uiPedalsOpen = open;
    applySize();
    board->setPanelVisible (open);
}

void PolySwellEditor::showKnobView (const juce::String& paramId, bool on) { board->showKnobView (paramId, on); }

void PolySwellEditor::setUiScale (float scale)
{
    proc.uiScale = juce::jlimit (0.75f, 2.f, scale);
    applySize();
    board->bg = {};
    board->repaint();
}
