// PolySwell II editor. Layout follows the approved mockup: an oxblood pedal (window, 3 big + 4 small knobs, two
// footswitches) and a panel of six tinted modules. Static artwork (enclosure, plates, lettering) is baked once into an
// image at the current size; knob bodies are baked per size too. Only indicators, LEDs, chips and the window repaint.
#include "PluginEditor.h"
#include "BinaryData.h"

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

// Bahnschrift / Consolas ship with Windows; a Mac gets its closest built-in fonts (DIN Alternate, Menlo)
#if JUCE_MAC
static const char* const kUiFace = "DIN Alternate";
static const char* const kMonoFace = "Menlo";
#else
static const char* const kUiFace = "Bahnschrift";
static const char* const kMonoFace = "Consolas";
#endif

juce::Font uiFont (float h, bool bold = true, float track = 0.12f)
{
    return juce::Font (juce::FontOptions (kUiFace, h, bold ? juce::Font::bold : juce::Font::plain))
             .withHorizontalScale (0.88f).withExtraKerningFactor (track);
}
juce::Font monoFont (float h) { return juce::Font (juce::FontOptions (kMonoFace, h, juce::Font::plain)); }

// ------------------------------------------------------------------------------------------------ skins
// The pedal is printed on a worn plate (the owner's artwork, resources/skin), with the PolySwell logo; a skin sets the
// plate, the logo, the ink, the knobs' three ring colours (the logo's rings, outer = lightest), the LED and the window's
// glow. The effects panel keeps its own dark look.
struct Skin
{
    const char* name;
    const char* plate; int plateSize; const char* logo; int logoSize;
    Colour ink, body1, frame2, accent, onAccent, led;
    Colour rings[3];
    Colour glass1, glass2, glow, amber, pale, text, textHi;
    Colour faceHi { 0xfffffaf0 }, faceMid { 0xffece2cb }, faceLo { 0xffc9bc9d }, pointer { 0xff2a231b };   // the pedal knobs (ivory)
    juce::Image plateImg() const { return juce::ImageCache::getFromMemory (plate, plateSize); }
    juce::Image logoImg() const  { return juce::ImageCache::getFromMemory (logo, logoSize); }
};
constexpr int kNumSkins = 4;   // Sunburst, Lagoon, Midnight, Sunburst Night
const Skin& skinAt (int i)
{
    static const Skin s[kNumSkins] = {
        { "Sunburst", BinaryData::plate_sunburst_png, BinaryData::plate_sunburst_pngSize, BinaryData::logo_sunburst_png, BinaryData::logo_sunburst_pngSize,
          Colour (0xff2a231b), Colour (0xffefe5cc), Colour (0x592a231b), Colour (0xffd9732f), Colour (0xfffff4df), Colour (0xffffb030),
          { Colour (0xffe09a45), Colour (0xffd9732f), Colour (0xffbc4320) },
          Colour (0xff3a1a08), Colour (0xff0c0503), Colour (0xffff8a1f), Colour (0xffffb347), Colour (0xfffff1cf), Colour (0xffe89a45), Colour (0xffffc777) },
        { "Lagoon", BinaryData::plate_lagoon_png, BinaryData::plate_lagoon_pngSize, BinaryData::logo_lagoon_png, BinaryData::logo_lagoon_pngSize,
          Colour (0xff1f2433), Colour (0xffebe9de), Colour (0x541f2433), Colour (0xff5466ce), Colour (0xfff2f6ff), Colour (0xff8cb6ff),
          { Colour (0xff699bd6), Colour (0xff5466ce), Colour (0xff4435c0) },   // light blue -> blue -> indigo (soft, like Sunburst)
          Colour (0xff0d1838), Colour (0xff04070f), Colour (0xff4f6fe0), Colour (0xff8fb0f2), Colour (0xffe8eeff), Colour (0xff7f98dc), Colour (0xffb8c8ff) },
        { "Midnight", BinaryData::plate_midnight_png, BinaryData::plate_midnight_pngSize, BinaryData::logo_midnight_png, BinaryData::logo_midnight_pngSize,
          Colour (0xffefe5cb), Colour (0xff23263a), Colour (0x47efe5cb), Colour (0xff5466ce), Colour (0xfff2f6ff), Colour (0xff9fbcff),
          { Colour (0xff699bd6), Colour (0xff5466ce), Colour (0xff4435c0) },
          Colour (0xff161a3c), Colour (0xff05060d), Colour (0xff5a6ae6), Colour (0xff9fb4ff), Colour (0xffeef1ff), Colour (0xff8a9ae0), Colour (0xffc4cfff),
          Colour (0xff5d6488), Colour (0xff353a58), Colour (0xff171a2c), Colour (0xffefe5cb) },   // navy knobs, cream pointers
        { "Sunburst Night", BinaryData::plate_night_png, BinaryData::plate_night_pngSize, BinaryData::logo_night_png, BinaryData::logo_night_pngSize,
          Colour (0xffefe5cb), Colour (0xff1c1916), Colour (0x47efe5cb), Colour (0xffd9732f), Colour (0xfffff4df), Colour (0xffffb030),
          { Colour (0xffe09a45), Colour (0xffd9732f), Colour (0xffbc4320) },
          Colour (0xff3a1a08), Colour (0xff0c0503), Colour (0xffff8a1f), Colour (0xffffb347), Colour (0xfffff1cf), Colour (0xffe89a45), Colour (0xffffc777),
          Colour (0xff5a534c), Colour (0xff35302b), Colour (0xff17140f), Colour (0xffefe5cb) } };   // black knobs, cream pointers
    return s[juce::jlimit (0, kNumSkins - 1, i)];
}

// ------------------------------------------------------------------------------------------------ geometry
constexpr float kPedalX = 22, kPedalY = 56, kPedalW = 392, kPedalH = 646;
constexpr float kPanelX = 432, kPanelY = 56, kPanelW = 622, kPanelH = 646;
constexpr float kModW = 293, kModH = 199.3f;

// panel: FILTER | FEEDBACK, MOTION | SPACE, LAYER across the full width (index 4 unused)
Rect moduleRect (int i)
{
    const float x0 = kPanelX + 12, y0 = kPanelY + 12;
    if (i == 4) return {};
    // rows: FILTER | FEEDBACK (short: one row of settings), MOTION | SPACE, LAYER across the full width
    constexpr float h0 = 150.f, h1 = 232.f, h2 = 3.f * kModH - h0 - h1;
    const float y1 = y0 + h0 + 12.f, y2 = y1 + h1 + 12.f;
    if (i == 3) return { x0, y2, kPanelW - 24, h2 };
    const float col = (i == 1 || i == 5) ? 1.f : 0.f;
    return { x0 + col * (kModW + 12), i < 2 ? y0 : y1, kModW, i < 2 ? h0 : h1 };
}

struct ModDef { const char* title; const char* sub; const char* onId; Colour ac; };
const ModDef kMods[6] = {
    { "FILTER",   "Sweeps each note",        "filterOn",   col::filter },
    { "FEEDBACK", "Amp in the room",         "feedbackOn", col::feedback },
    { "MOTION",   "Movement per note",       "motionOn",   col::motion },
    { "LAYER",    "Synth under each note",   "voiceOn",    col::layer },
    { "",         "",                        nullptr,      col::hold },   // unused slot (HOLD was removed)
    { "SPACE",    "Follows the swell",       "smearOn",    col::space } };

// a small line icon per module, in its header
void drawModIcon (Graphics& g, int i, Rect r, Colour c)
{
    const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
    juce::Path p;
    switch (i)
    {
        case 0:   // low-pass response
            p.startNewSubPath (x, y + h * 0.4f); p.lineTo (x + w * 0.45f, y + h * 0.4f);
            p.quadraticTo (x + w * 0.62f, y + h * 0.18f, x + w * 0.7f, y + h * 0.5f); p.lineTo (x + w, y + h * 0.95f);
            break;
        case 1:   // a speaker with sound coming back
            p.addRectangle (x, y + h * 0.36f, w * 0.16f, h * 0.28f);
            p.startNewSubPath (x + w * 0.16f, y + h * 0.36f); p.lineTo (x + w * 0.4f, y + h * 0.14f); p.lineTo (x + w * 0.4f, y + h * 0.86f); p.lineTo (x + w * 0.16f, y + h * 0.64f);
            p.addCentredArc (x + w * 0.42f, y + h * 0.5f, w * 0.24f, h * 0.24f, 0.f, 0.7f, 2.45f, true);
            p.addCentredArc (x + w * 0.42f, y + h * 0.5f, w * 0.48f, h * 0.44f, 0.f, 0.75f, 2.4f, true);
            break;
        case 2:   // a wave
            for (int k = 0; k <= 24; ++k)
            {
                const float u = (float) k / 24.f, yy = y + h * (0.5f - 0.32f * std::sin (u * 2.f * kPi * 1.5f));
                if (k == 0) p.startNewSubPath (x + u * w, yy); else p.lineTo (x + u * w, yy);
            }
            break;
        case 3:   // stacked layers
            for (int k = 0; k < 3; ++k)
            {
                const float yy = y + h * (0.22f + 0.28f * (float) k);
                p.startNewSubPath (x + w * 0.1f, yy + h * 0.1f); p.lineTo (x + w * 0.5f, yy - h * 0.08f); p.lineTo (x + w * 0.9f, yy + h * 0.1f); p.lineTo (x + w * 0.5f, yy + h * 0.28f); p.closeSubPath();
            }
            break;
        case 4:   // a snowflake
            for (int k = 0; k < 3; ++k)
            {
                const float a = kPi * (float) k / 3.f, cx = x + w * 0.5f, cy = y + h * 0.5f, R = w * 0.46f;
                p.startNewSubPath (cx - std::cos (a) * R, cy - std::sin (a) * R); p.lineTo (cx + std::cos (a) * R, cy + std::sin (a) * R);
            }
            break;
        default:  // ripples spreading out
            p.addEllipse (x + w * 0.08f, y + h * 0.42f, w * 0.16f, h * 0.16f);
            for (int k = 1; k <= 3; ++k)
                p.addCentredArc (x + w * 0.16f, y + h * 0.5f, w * 0.26f * (float) k, h * 0.24f * (float) k, 0.f, 0.55f, 2.6f, true);
            break;
    }
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

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
        { "fbDistance", "DISTANCE: where you stand from the amp. Near: the note itself feeds back; further: its octave, the 12th, two octaves." },
        { "sustainMax", "MAX: the most that SUSTAIN and FEEDBACK may lift a fading note." },
        { "vibDepth", "VIBRATO: pitch movement on each note, in cents." },
        { "tremolo", "TREMOLO: each note pulses on its own, starting at its pick." },
        { "spread", "SPREAD: places each note or chord in the stereo field." },
        { "detuneMix", "DETUNE: two copies 10 cents up and down (left and right), as on a POG2. The knob sets how loud they are; at the top only the copies remain." },
        { "voiceMix", "MIX: guitar on the left, synth voice on the right." },
        { "voiceTone", "TONE: the voice's own low-pass filter." },
        { "voiceGlide", "GLIDE: single notes slide from the previous one." },
        { "voiceCharacter", "CHARACTER: makes the voice more alive (string colour, wander, scoop, breath)." },
        { "smearSize", "SIZE: the reverb's length." },
        { "smearLevel", "LEVEL: how much reverb is heard (level-matched)." },
        { "smearDrive", "DRIVE: soft clipping on the reverb only." },
        { "follow", "PACE: fast playing shortens the swell, slow playing keeps the full ATTACK." },
        { "even", "EVEN: levels soft and hard picks. Drag up or down." },
        { "vibRate", "Vibrato rate. Drag up or down." }, { "vibDelay", "Time before the vibrato starts. Drag up or down." },
        { "tremRate", "Tremolo rate. Drag up or down." },
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
            // silk-screened ticks around the big knobs
            if (kind == 0)
            {
            g.setColour (col::ink.withAlpha (0.85f));
            for (int i = 0; i <= 10; ++i)
            {
                const float a = -0.75f * kPi + 1.5f * kPi * (float) i / 10.f, s = std::sin (a), co = -std::cos (a);
                g.drawLine (c + s * R * 1.15f, c + co * R * 1.15f, c + s * R * 1.29f, c + co * R * 1.29f, juce::jmax (1.f, R * (i % 5 == 0 ? 0.045f : 0.03f)));
            }
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

    // kind 4: a horizontal fader (a groove, the filled part in the module's colour, a small cap, the value on it)
    void drawLinearSlider (Graphics& g, int x, int y, int w, int h, float sliderPos, float, float, juce::Slider::SliderStyle, juce::Slider& s) override
    {
        const auto b = Rect ((float) x, (float) y, (float) w, (float) h).reduced (1.f);
        const Colour ac = Colour ((juce::uint32) (juce::int64) s.getProperties().getWithDefault ("ac", (juce::int64) 0xffe9b24a));
        g.setColour (juce::Colours::black.withAlpha (0.4f)); g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
        g.setColour (ac.withAlpha (0.45f)); g.fillRoundedRectangle (b.withRight (juce::jmax (b.getX() + b.getHeight(), sliderPos)), b.getHeight() * 0.5f);
        g.setColour (ac.withAlpha (0.6f)); g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 1.f);
        const float cx = juce::jlimit (b.getX() + 6.f, b.getRight() - 6.f, sliderPos);
        g.setColour (Colour (0xfff2e6cf)); g.fillRoundedRectangle (cx - 4.f, b.getY() + 2.f, 8.f, b.getHeight() - 4.f, 3.f);
        g.setFont (uiFont (10.5f, true, 0.08f)); g.setColour (col::chipText);
        g.drawText (s.getTextFromValue (s.getValue()).toUpperCase(), b.reduced (10.f, 0.f), juce::Justification::centredRight);
    }

    void drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        const int kind = (int) s.getProperties().getWithDefault ("kind", 2);
        const Colour ac ((juce::uint32) (juce::int64) s.getProperties().getWithDefault ("ac", (juce::int64) col::amber.getARGB()));
        const Rect b ((float) x, (float) y, (float) w, (float) h);
        if (kind == 3) { drawValueChip (g, b, pos, s, ac); return; }

        if (kind < 2 && skin != nullptr)   // the pedal's knobs: ivory, the logo's three rings around it filling with the value
        {
            const float bw = (float) w, c = b.getCentreX(), cy = b.getCentreY();
            const float rw0 = bw > 100.f ? 3.4f : bw > 60.f ? 2.6f : 2.f, pad = 6.f + 3.f * (rw0 + 1.8f) + 4.f;   // face size as before
            const float R = bw * 0.5f - pad, a = a0 + pos * (a1 - a0);
            const float rw = bw > 100.f ? 6.f : bw > 60.f ? 4.f : 3.2f, rr = R + 3.f + rw * 0.5f;
            // one ring whose colour fades along the sweep, from the skin's lightest colour to its deepest
            auto ringCol = [&] (float t) { return t < 0.5f ? skin->rings[0].interpolatedWith (skin->rings[1], t * 2.f)
                                                           : skin->rings[1].interpolatedWith (skin->rings[2], t * 2.f - 1.f); };
            const int segs = 60;
            const juce::PathStrokeType seg (rw, juce::PathStrokeType::curved, juce::PathStrokeType::butt);
            for (int k = 0; k < segs; ++k)
            {
                const float t0 = (float) k / segs, t1 = (float) (k + 1) / segs, tm = (t0 + t1) * 0.5f;
                const float s0 = a0 + t0 * (a1 - a0), s1 = a0 + t1 * (a1 - a0);
                juce::Path arc; arc.addCentredArc (c, cy, rr, rr, 0.f, s0, juce::jmin (s1 + 0.004f, a1), true);
                const bool lit = s1 <= a + 0.0001f;
                g.setColour (ringCol (tm).withAlpha (lit ? 1.f : 0.2f)); g.strokePath (arc, seg);
                if (! lit && s0 < a)   // the segment under the pointer: lit up to it
                {
                    juce::Path part; part.addCentredArc (c, cy, rr, rr, 0.f, s0, a, true);
                    g.setColour (ringCol (tm)); g.strokePath (part, seg);
                }
            }
            juce::Path face; face.addEllipse (c - R, cy - R, 2 * R, 2 * R);
            juce::DropShadow (juce::Colours::black.withAlpha (0.45f), juce::jmax (2, juce::roundToInt (R * 0.18f)), { 0, juce::jmax (1, juce::roundToInt (R * 0.09f)) }).drawForPath (g, face);
            juce::ColourGradient fg (skin->faceHi, c - R * 0.24f, cy - R * 0.4f, skin->faceLo, c + R * 0.6f, cy + R * 0.9f, true);
            fg.addColour (0.55, skin->faceMid);
            g.setGradientFill (fg); g.fillPath (face);
            g.setColour (juce::Colours::black.withAlpha (0.28f)); g.strokePath (face, juce::PathStrokeType (1.f));
            g.setColour (Colour (0x1f3c2814)); g.drawEllipse (c - R * 0.8f, cy - R * 0.8f, R * 1.6f, R * 1.6f, 1.2f);
            const float sn = std::sin (a), cs = -std::cos (a);
            g.setColour (skin->pointer);
            g.drawLine (juce::Line<float> (c + sn * R * 0.25f, cy + cs * R * 0.25f, c + sn * R * 0.86f, cy + cs * R * 0.86f), R > 25.f ? 4.5f : R > 16.f ? 3.5f : 2.6f);
            return;
        }
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
    const Skin* skin = nullptr;   // the pedal's knobs follow the skin
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
    virtual void midiMenu (const String& paramId, juce::Component& target) = 0;   // right-click: MIDI learn
};

struct Knob : juce::Slider
{
    Knob (PolySwellProcessor& p, Host& h, const String& paramId, const String& shownName, int kind, Colour ac)
        : host (h), name (shownName), id (paramId)
    {
        setSliderStyle (kind == 4 ? juce::Slider::LinearHorizontal : kind == 3 ? juce::Slider::RotaryVerticalDrag : juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        setRotaryParameters (1.25f * kPi, 2.75f * kPi, true);
        setMouseDragSensitivity (kind == 3 ? 160 : 220);
        setPopupDisplayEnabled (false, false, nullptr);
        getProperties().set ("kind", kind);
        getProperties().set ("ac", (juce::int64) ac.getARGB());
        getProperties().set ("label", shownName);
        setTooltip (String (tipFor (paramId)) + (kind < 3 ? "  Double-click: reset. Right-click: MIDI learn." : ""));
        att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, paramId, *this);
        if (auto* prm = p.apvts.getParameter (paramId)) setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
        const String unit = p.apvts.getParameter (paramId)->getLabel();
        textFromValueFunction = [base = textFromValueFunction, unit] (double v) { return tidyValue (base ? base (v) : String (v), unit); };
        onValueChange = [this] { if (isMouseOverOrDragging()) show(); repaint(); };
        onDragStart = [this] { show(); host.focus (id, true); };
        onDragEnd = [this] { if (! isMouseOver()) host.focus (id, false); };
    }
    void mouseEnter (const juce::MouseEvent& e) override { juce::Slider::mouseEnter (e); show(); host.focus (id, true); repaint(); }
    void mouseExit (const juce::MouseEvent& e) override { juce::Slider::mouseExit (e); if (! e.mods.isAnyMouseButtonDown()) host.focus (id, false); repaint(); }
    void show() { host.readout (name, getTextFromValue (getValue())); }
    void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) { host.midiMenu (id, *this); return; } juce::Slider::mouseDown (e); }
    void mouseDrag (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::Slider::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::Slider::mouseUp (e); }
    bool hitTest (int x, int y) override   // round knobs: only the knob itself, so neighbours never steal the mouse
    {
        if ((int) getProperties().getWithDefault ("kind", 2) >= 3) return juce::Component::hitTest (x, y);
        return juce::Point<float> ((float) x, (float) y).getDistanceFrom (getLocalBounds().toFloat().getCentre()) < (float) getWidth() * 0.42f;
    }
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
        if (pedalSkin != nullptr && *pedalSkin != nullptr)   // on the pedal: a printed pill
        {
            const Skin& sk = **pedalSkin;
            const auto r = b.reduced (0.75f);
            if (on) { g.setColour (sk.accent); g.fillRoundedRectangle (r, r.getHeight() * 0.5f); }
            else { g.setColour (sk.ink.withAlpha (over ? 1.f : 0.85f)); g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.5f); }
            g.setFont (uiFont (11.f, true, 0.14f));
            g.setColour (on ? sk.onAccent : sk.ink);
            g.drawText (shown(), b.withTrimmedLeft (10).withTrimmedRight (mode == toggle ? 8.f : 18.f), juce::Justification::centredLeft);
            if (mode != toggle)
            {
                juce::Path t; const float x = b.getRight() - 13, y = b.getCentreY() - 1.5f;
                t.addTriangle (x - 3.5f, y, x + 3.5f, y, x, y + 4.5f);
                g.fillPath (t);
            }
            return;
        }
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

    void mouseEnter (const juce::MouseEvent& e) override { juce::Button::mouseEnter (e); if (onHover) onHover (true); }
    void mouseExit (const juce::MouseEvent& e) override { juce::Button::mouseExit (e); if (onHover) onHover (false); }

    Mode mode;
    String label;
    Colour ac;
    std::function<void (bool)> onHover;
    const Skin* const* pedalSkin = nullptr;   // set for the chips printed on the pedal
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

static void drawLed (Graphics& g, Rect b, bool on, Colour tint = Colour (0xffffb030))
{
    const float c = b.getCentreX(), cy = b.getCentreY(), r = 5.f;
    if (on)
    {
        g.setGradientFill (juce::ColourGradient (tint.withAlpha (0.6f), c, cy, tint.withAlpha (0.f), c + r * 3.2f, cy, true));
        g.fillEllipse (c - r * 3.2f, cy - r * 3.2f, r * 6.4f, r * 6.4f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.5f)); g.fillEllipse (c - r - 2, cy - r - 2, 2 * r + 4, 2 * r + 4);
    juce::ColourGradient gr (on ? Colour (0xfffff7dc) : tint.withMultipliedBrightness (0.45f).withMultipliedSaturation (0.7f), c - r * 0.3f, cy - r * 0.4f,
                             on ? tint.withMultipliedBrightness (0.72f) : tint.withMultipliedBrightness (0.16f), c + r, cy + r, true);
    if (on) gr.addColour (0.4, tint);
    g.setGradientFill (gr);
    g.fillEllipse (c - r, cy - r, 2 * r, 2 * r);
}

struct Led : juce::Button
{
    Led (PolySwellProcessor& p, const String& paramId, bool inverted, const String& tip)
        : juce::Button (paramId), invert (inverted), bound (p, paramId, [this] { repaint(); })
    { setTooltip (tip); setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    bool on() const { return (bound.value > 0.5f) != invert; }
    void paintButton (Graphics& g, bool, bool) override
    { drawLed (g, getLocalBounds().toFloat(), on(), (pedalSkin != nullptr && *pedalSkin != nullptr) ? (*pedalSkin)->led : Colour (0xffffb030)); }
    const Skin* const* pedalSkin = nullptr;
    void clicked() override { bound.set (bound.value > 0.5f ? 0.f : 1.f); }
    bool invert;
    Bound bound;
};

struct FootSwitch : juce::Component, juce::SettableTooltipClient
{
    FootSwitch() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (Graphics& g) override   // the owner's footswitch photo (resources/skin/footswitch.png)
    {
        if (! img.isValid()) img = juce::ImageCache::getFromMemory (BinaryData::footswitch_png, BinaryData::footswitch_pngSize);
        const auto b = getLocalBounds().toFloat();
        const float c = b.getCentreX(), cy = b.getCentreY(), R = down ? 31.f : 32.f, oy = down ? 1.f : 0.f;
        juce::Path disc; disc.addEllipse (c - R, cy - R + oy, 2 * R, 2 * R);
        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), down ? 4 : 8, { 0, down ? 2 : 4 }).drawForPath (g, disc);
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        g.setOpacity (1.f);   // drawImage uses the current colour's alpha (the shadow's would make it see-through)
        g.drawImage (img, Rect (c - R, cy - R + oy, 2 * R, 2 * R));
    }
    void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) { if (onMenu) onMenu(); return; } down = true; repaint(); if (onDown) onDown(); }
    void mouseUp (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) return; down = false; repaint(); if (onUp) onUp(); }
    bool hitTest (int x, int y) override { return juce::Point<float> ((float) x, (float) y).getDistanceFrom (getLocalBounds().toFloat().getCentre()) < 32.f; }
    std::function<void()> onDown, onUp, onMenu;
    bool down = false;
    juce::Image img;
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
// The window. Live: one ribbon per tracked note (pitch up, the past to the left of "now"), the filter's cutoff on the
// same frequency axis (what is above it is being turned down, shaded), the harmonic each held note feeds back into
// (rust), the HOLD pad (violet), the SPACE tail (sage) and the LAYER voice (blue core); underneath, the output level
// against the guitar's own level, with the latest note's swell projected past "now". While a knob or chip is under
// the mouse, that control's picture instead.
struct SignalWindow : juce::Component, juce::Timer
{
    static constexpr int kCols = 105, kB = PolySwellProcessor::kSpecBands, kMaxN = swello::SwelloEngine::kVoicePub;
    static constexpr float kNowFrac = 0.68f, kFLo = 72.f, kFHi = 1500.f, kColSec = 1.f / 30.f;
    struct Col { int n = 0; float midi[kMaxN] {}, lvl[kMaxN] {}, hot[kMaxN] {}, fb[kMaxN] {}; float cut = 0.f, outDb = -120.f, inDb = -120.f; bool pick = false; };

    explicit SignalWindow (PolySwellProcessor& p) : proc (p) { setInterceptsMouseClicks (true, false); startTimerHz (30); }

    // only the NOTES switch in the top-left corner takes clicks; the rest of the glass lets them through
    Rect notesRect() const { return { 8.f, 4.f, 46.f, 12.f }; }
    bool hitTest (int x, int y) override { return notesRect().expanded (3.f).contains ((float) x, (float) y); }
    void mouseDown (const juce::MouseEvent&) override { proc.uiWindowNotes = ! proc.uiWindowNotes; repaint(); }

    // the glow of the window follows the skin (the effects' own colours - filter, feedback, layer ... - stay)
    void setSkin (const Skin& s)
    {
        cAmber = s.amber; cPale = s.pale; cGlow = s.glow; cGlass1 = s.glass1; cGlass2 = s.glass2; cText = s.text; cTextHi = s.textHi;
        cCaption = s.text.interpolatedWith (s.pale, 0.55f);
        repaint();
    }
    Colour cAmber { 0xffffb347 }, cPale { 0xfffff1cf }, cGlow { 0xffff8a1f }, cGlass1 { 0xff3a1a08 }, cGlass2 { 0xff0c0503 },
           cText { 0xffe89a45 }, cTextHi { 0xffffc777 }, cCaption { 0xffe9c79a };

    void setReadout (const String& n, const String& v) { roName = n.toUpperCase(); roValue = v; roTicks = 60; }

    // a control is under the mouse or being turned: the window explains it until the mouse leaves
    void setFocus (const String& id, bool on)
    {
        if (on) { focusId = id; focusHeld = true; }
        else if (id == focusId) { focusHeld = false; linger = 26; }
    }

    static int viewKind (const String& id)
    {
        static const std::map<String, int> k {
            { "attack", 1 }, { "sustain", 1 }, { "touch", 1 }, { "follow", 1 }, { "even", 1 }, { "feedback", 1 }, { "dry", 1 }, { "vol", 1 },
            { "fbRise", 1 }, { "sustainMax", 1 }, { "swellShape", 1 }, { "levelMatch", 1 }, { "fbTone", 1 }, { "smoothRepick", 1 },
            { "freq", 2 }, { "mod", 2 }, { "res", 2 }, { "pickFilter", 2 }, { "slope", 2 },
            { "vibDepth", 3 }, { "vibRate", 3 }, { "vibDelay", 3 }, { "vibHuman", 3 }, { "vibTarget", 3 },
            { "tremolo", 4 }, { "tremRate", 4 }, { "tremShape", 4 },
            { "spread", 5 }, { "spreadMode", 5 },
            { "detune", 6 }, { "detuneMix", 6 },
            { "voiceMix", 7 }, { "voiceTone", 7 }, { "voiceWave", 7 }, { "voiceOct", 7 }, { "voiceUnison", 7 }, { "voiceMono", 7 },
            { "voiceGlide", 7 }, { "voiceCharacter", 7 },
            { "smearSize", 9 }, { "smearLevel", 9 }, { "smearDrive", 9 }, { "smearHold", 9 }, { "smearCut", 9 }, { "smearThresh", 9 },
            { "smearSend", 9 }, { "smearDark", 9 }, { "smearGate", 9 },
            { "fbDistance", 10 }, { "fbOneWins", 10 }, { "fbAmp", 10 }, { "fbHarmonic", 10 }, { "fbMode", 10 } };
        const auto it = k.find (id);
        return it != k.end() ? it->second : 0;
    }

    float P (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }
    bool on (const char* id) const { return P (id) > 0.5f; }

    // the harmonic(s) the amp model feeds back into, along the DISTANCE ladder (as the engine, minus its slow drift)
    void fbLadder (float& rA, float& rB, float& wA, float& wB) const
    {
        const float pos = juce::jlimit (0.f, 1.f, P ("fbDistance")) * 4.f;
        const int ia = juce::jmin (3, (int) pos);
        const float x = juce::jlimit (0.f, 1.f, (pos - (float) ia - 0.3f) / 0.4f), w = x * x * (3.f - 2.f * x);
        rA = (float) (ia + 1); rB = (float) (ia + 2); wA = 1.f - w; wB = w;
    }
    float classicRatio() const { const int h = juce::roundToInt (P ("fbHarmonic")); return h == 1 ? 3.f : h == 2 ? 1.f : h == 3 ? 0.5f : 2.f; }
    bool feedbackOn() const { return P ("feedback") > 0.f && on ("feedbackOn"); }

    static float hzOf (float midi) { return 440.f * std::pow (2.f, (midi - 69.f) / 12.f); }
    static String noteName (float midi)
    {
        static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int m = juce::roundToInt (midi);
        return String (n[((m % 12) + 12) % 12]) + String (m / 12 - 1);
    }

    // the filter frequency at a moment of the latest note (age < 0: no note, the filter at rest); 0 = filter off
    float cutoffAt (float age) const
    {
        if (! on ("filterOn")) return 0.f;
        const float fc = P ("freq"), mod = on ("modOn") ? P ("mod") : 0.f, oct = std::abs (mod) < 0.03f ? 0.f : std::abs (mod) * 6.f;
        const float A = noteAttack > 0.f ? noteAttack : P ("attack") * 0.001f;
        const float ph = age >= 0.f ? juce::jlimit (0.f, 1.f, A < 0.002f ? 1.f : age / A) : 1.f;
        return juce::jmax (40.f, fc / std::pow (2.f, oct * (mod > 0.f ? 1.f - ph : ph)));
    }

    const Col& col (int age) const { return cols[(size_t) ((head - 1 - age + 2 * kCols) % kCols)]; }

    void timerCallback() override
    {
        float in[kB], out[kB];
        const uint32_t frame = proc.readSpectrum (in, out);
        idle = frame == lastFrame ? idle + 1 : 0;
        lastFrame = frame;
        const bool live = idle <= 4;

        Col& c = cols[(size_t) head];
        c = Col();
        double ei = 0.0, eo = 0.0;
        for (int b = 0; b < kB; ++b) { ei += (double) in[b] * in[b]; eo += (double) out[b] * out[b]; }
        c.inDb  = live ? (float) (10.0 * std::log10 (ei + 1e-12)) : -120.f;
        c.outDb = live ? (float) (10.0 * std::log10 (eo + 1e-12)) : -120.f;

        // the tracked notes
        swello::SwelloEngine::VoiceNote v[kMaxN];
        const int n = live ? juce::jmin (kMaxN, proc.readVoiceNotes (v)) : 0;
        float loudest = 0.f;
        for (int j = 0; j < n; ++j) loudest = juce::jmax (loudest, v[j].level);
        liveN = 0;   // each sounding note: pitch, level, time since its pick (for the dots on the swell)
        for (int j = 0; j < n; ++j)
            if (v[j].hz >= 30.f) { liveMidi[liveN] = 69.f + 12.f * std::log2 (v[j].hz / 440.f); liveLvl[liveN] = v[j].level; liveT[liveN] = v[j].timeSec; ++liveN; }
        lvlRef = juce::jmax (lvlRef * 0.995f, loudest, 1.0e-6f);
        const bool fbOn = feedbackOn(), immediate = P ("fbMode") > 0.5f;
        const float A = P ("attack") * 0.001f, rise = juce::jmax (0.05f, P ("fbRise"));
        for (int j = 0; j < n; ++j)
        {
            if (v[j].hz < 30.f) continue;
            const int k = c.n++;
            c.midi[k] = 69.f + 12.f * std::log2 (v[j].hz / 440.f);
            c.lvl[k] = juce::jlimit (0.08f, 1.f, 1.f + 20.f * std::log10 (v[j].level / lvlRef + 1.0e-9f) / 36.f);
            const int band = juce::jlimit (0, kB - 1, (int) (std::log (v[j].hz / 40.f) / std::log (16000.f / 40.f) * (float) kB));
            c.hot[k] = juce::jlimit (0.f, 1.f, (20.f * std::log10 ((out[band] + 1.0e-9f) / (in[band] + 1.0e-9f)) - 3.f) / 12.f);
            if (fbOn)
            {
                const float x = juce::jlimit (0.f, 1.f, (v[j].timeSec - A - 0.25f * rise) / (0.75f * rise));
                c.fb[k] = immediate ? P ("feedback") : P ("feedback") * x * x * (3.f - 2.f * x);
            }
        }
        c.cut = cutoffAt (noteAge);

        const auto note = proc.latestNote();
        c.pick = live && note.seq != lastSeq && note.seq != 0;
        lastSeq = note.seq;
        noteAge = (note.seq != 0 && live) ? note.ageSec : -1.f;
        noteAttack = note.attackSec;
        if (live && note.seq != 0)
        {
            const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
            const float knobA = juce::jmax (0.01f, P ("attack") * 0.001f);
            const Pick pk { note.seq, now - note.ageSec, note.attackSec / knobA, note.strength, note.pickStrength };
            if (picks.empty() || picks.back().seq != note.seq) { picks.push_back (pk); if (picks.size() > 4) picks.erase (picks.begin()); }
            else if (note.ageSec < 0.2f) picks.back() = pk;   // the engine refines a pick while its strum window is open
        }
        {
            // a pick is ringing while a tracked note's time since its pick matches; once none does, its dot stops there
            const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
            for (size_t k = 0; k < picks.size(); ++k)
            {
                auto& pk = picks[k];
                if (pk.endAge >= 0.f) continue;
                const float age = (float) (now - pk.t0);
                if (k + 1 < picks.size() && age - (float) (now - picks[k + 1].t0) < 0.02f) continue;
                if (age < 0.35f) continue;
                bool ringing = live;
                if (ringing)
                {
                    ringing = false;
                    for (int j = 0; j < liveN; ++j) if (std::abs (liveT[j] - age) < 0.2f) { ringing = true; break; }
                }
                if (! ringing) pk.endAge = age;
            }
        }
        // the guitar's level at the pick (loudest of the first 4 frames): the projected swell starts from it
        const float inLin = std::sqrt ((float) ei);
        if (c.pick) { inPeakNote = inLin; pickCols = 0; }
        else if (++pickCols < 4) inPeakNote = juce::jmax (inPeakNote, inLin);


        // input meter: per-block peak of the guitar, falling 1.5 dB a frame, with a 1 s peak hold
        const float key = proc.smearKeyDb.load (std::memory_order_relaxed);
        meterDb = juce::jmax (key, meterDb - 1.5f);
        if (key >= peakDb) { peakDb = key; peakHoldT = 30; } else if (--peakHoldT < 0) peakDb -= 1.f;

        head = (head + 1) % kCols;
        if (focusHeld) roTicks = 30; else if (roTicks > 0) --roTicks;
        if (! focusHeld && linger > 0) --linger;
        const float target = ((focusHeld || linger > 0) && viewKind (focusId) != 0) ? 1.f : 0.f;
        const float before = viewA;
        viewA += (target - viewA) * 0.35f;
        if (std::abs (target - viewA) < 0.02f) viewA = target;
        const bool byp = on ("bypass");
        // nothing moving: stop repainting once the last note has scrolled off
        quiet = (n > 0 || c.outDb > -90.f || meterDb > -60.f) ? 0 : quiet + 1;
        if (quiet < kCols + 2 || roTicks > 0 || hadReadout || byp != lastByp || viewA > 0.f || before > 0.f || paramsChanged())
            repaint();
        hadReadout = roTicks > 0; lastByp = byp;
    }

    // the live view follows these even when nothing is playing
    bool paramsChanged()
    {
        float sum = 0.f; int i = 1;
        for (auto* id : { "attack", "sustain", "sustainMax", "swellShape", "levelMatch", "filterOn", "freq", "mod", "res", "slope",
                          "feedback", "feedbackOn", "fbDistance", "voiceOn", "voiceMix", "smearOn", "smearSize", "bypass" })
            sum += P (id) * (float) (i++);
        const bool changed = sum != paramSum;
        paramSum = sum;
        return changed;
    }

    // ---------------------------------------------------------------- the swell picture
    static constexpr float kT = 3.5f;                 // seconds across the picture
    static float natural (float t) { return t < 0.f ? 0.f : std::pow (10.f, -8.f * t / 20.f); }   // a plucked note: about 8 dB/s
    static float swellGain (float ph, int shape)   // 0 classic, 1 bowed, 2 POG2 (as the engine)
    {
        ph = juce::jlimit (0.f, 1.f, ph);
        if (shape == 2) return (1.f - std::exp (-3.f * ph)) / (1.f - std::exp (-3.f));
        if (shape == 0) return ph * ph;
        return std::pow ((1.f - std::exp (-3.1f * ph)) / (1.f - std::exp (-3.1f)), 2.45f);
    }
    // level of one note after the pedal: its swell over `attack`, and SUSTAIN slowing the decay (as the engine:
    // the note may fall no faster than 0.5 + 8 (1 - SUSTAIN)^1.3 dB/s, lifted by at most MAX x SUSTAIN)
    float swelled (float t, float attack) const
    {
        if (t < 0.f) return 0.f;
        const float s = P ("sustain");
        const float rate = s > 0.f ? juce::jmin (8.f, 0.5f + 8.f * std::pow (1.f - s, 1.3f)) : 8.f;
        float lift = juce::jmin (P ("sustainMax") * s, (8.f - rate) * t);
        if (P ("levelMatch") > 0.5f && attack > 0.002f)     // MATCH: the string's decay during the swell is made up
        {
            auto during = [&] (float u)
            {
                const float k = juce::jmin (8.f, 1.f / natural (u));
                const float l = juce::jmax (juce::jmin (P ("sustainMax") * s, (8.f - rate) * u), 20.f * std::log10 (1.f + (k - 1.f) * juce::jmin (1.f, u / attack)));
                return std::pow (10.f, (-8.f * u + l) / 20.f) * swellGain (u / attack, juce::roundToInt (P ("swellShape")));
            };
            if (t <= attack) return during (t);
            // after the swell: from the matched level, at SUSTAIN's rate (or the string's own, without SUSTAIN)
            return during (attack) * std::pow (10.f, -rate * (t - attack) / 20.f);
        }
        return std::pow (10.f, (-8.f * t + lift) / 20.f) * swellGain (attack < 0.002f ? 1.f : t / attack, juce::roundToInt (P ("swellShape")));
    }
    // PACE, as the engine: the swell fast playing allows (0.4 x the time since the last note), blended in by PACE
    float pacedAttack (float A, float gap) const
    {
        const float f = P ("follow");
        if (f <= 0.f || gap < 0.06f || gap >= 2.f) return A;
        return A + (juce::jmin (A, 0.4f * gap) - A) * f;
    }

    // a time plot: T seconds across, levels x gain (so a picture can zoom in on what the control changes)
    struct Plot
    {
        float x0, w, base, h, T = kT, gain = 1.f;
        float x (float t) const { return x0 + w * t / T; }
        float y (float a) const { return base - h * juce::jlimit (0.f, 1.06f, a * gain); }
    };

    template <typename Fn>
    static juce::Path curve (const Plot& pl, float from, Fn&& level)
    {
        juce::Path p;
        const int n = 420;
        bool started = false;
        for (int i = 0; i <= n; ++i)
        {
            const float t = pl.T * (float) i / (float) n;
            if (t < from - pl.T / (float) n) continue;
            const float tt = juce::jmax (t, from);
            if (! started) { p.startNewSubPath (pl.x (tt), pl.y (level (tt))); started = true; }
            else p.lineTo (pl.x (tt), pl.y (level (tt)));
        }
        return p;
    }
    static void dashed (Graphics& g, const juce::Path& p, Colour c, float thickness = 1.4f)
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
            g.setGradientFill (juce::ColourGradient (c.withAlpha (0.34f), 0, fillTo->base - fillTo->h, c.withAlpha (0.04f), 0, fillTo->base, false));
            g.fillPath (f);
        }
        g.setColour (c.withAlpha (0.13f)); g.strokePath (p, juce::PathStrokeType (10.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c.withAlpha (0.24f)); g.strokePath (p, juce::PathStrokeType (5.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c); g.strokePath (p, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    static void tag (Graphics& g, const String& s, float x, float y, Colour c, juce::Justification j = juce::Justification::centredLeft)
    {
        g.setFont (monoFont (10.5f)); g.setColour (c);
        g.drawText (s, Rect (j == juce::Justification::centredLeft ? x : x - 120.f, y - 7.f, 120.f, 14.f), j);
    }
    static String ms (float sec) { return sec < 1.f ? String (juce::roundToInt (sec * 1000.f)) + " ms" : String (sec, 2) + " s"; }

    // the frame for time pictures: a baseline and gridlines at a sensible step for the span shown
    Plot timeGrid (Graphics& g, Rect b, float T = kT, float gain = 1.f) const
    {
        const Plot pl { 14.f, b.getWidth() - 28.f, b.getHeight() - 24.f, b.getHeight() - 50.f, T, gain };
        g.setColour (cGlow.withAlpha (0x33 / 255.f)); g.fillRect (pl.x0, pl.base, pl.w, 1.f);
        const float step = T <= 0.6f ? 0.1f : T <= 1.2f ? 0.25f : T <= 2.4f ? 0.5f : 1.f;
        for (int k = 1; (float) k * step < T - 0.02f; ++k)
        {
            const float t = (float) k * step;
            g.setColour (cGlow.withAlpha (0x22 / 255.f)); g.fillRect (pl.x (t), pl.base - pl.h, 1.f, pl.h + 4.f);
            tag (g, step < 1.f ? String (juce::roundToInt (t * 1000.f)) + " ms" : String (juce::roundToInt (t)) + " s",
                 pl.x (t) + 3.f, pl.base - pl.h + 4.f, cText.withAlpha (0x88 / 255.f));
        }
        return pl;
    }
    static void markAt (Graphics& g, const Plot& pl, float t, Colour c, const String& text = {}, float textY = -1.f)
    {
        if (t > pl.T) return;
        for (float y = pl.base - pl.h; y < pl.base; y += 5.f) { g.setColour (c.withAlpha (0.55f)); g.fillRect (pl.x (t), y, 1.f, 2.5f); }
        juce::Path tri; tri.addTriangle (pl.x (t) - 4.5f, pl.base + 7.f, pl.x (t) + 4.5f, pl.base + 7.f, pl.x (t), pl.base);
        g.setColour (c); g.fillPath (tri);
        if (text.isNotEmpty()) tag (g, text, pl.x (t) + 4.f, textY >= 0.f ? textY : pl.base - pl.h + 16.f, c);
    }

    String paintSwell (Graphics& g, Rect b)
    {
        const Colour amberC (cAmber), guitarC (0xffd8c3a2), paleC (cPale);
        const float A = P ("attack") * 0.001f;
        const String id = focusId;
        auto peakOf = [] (auto&& fn, float T) { float m = 1.0e-4f; for (int i = 0; i <= 240; ++i) m = juce::jmax (m, fn (T * (float) i / 240.f)); return m; };

        if (id == "touch")   // FEEL: zoomed in on the swell, hard and soft pick side by side, both scaled up
        {
            const float tch = P ("touch");
            const float hard = A * std::pow (2.f, -tch * 2.f), soft = A * std::pow (2.f, tch * 2.f);
            const float T = juce::jlimit (0.3f, kT, 2.2f * juce::jmax (juce::jmax (hard, soft), A, 0.05f));
            const float pk = peakOf ([&] (float t) { return juce::jmax (swelled (t, hard), swelled (t, soft)); }, T);
            const Plot pl = timeGrid (g, b, T, 0.95f / pk);
            if (std::abs (tch) < 0.02f)
            {
                glowLine (g, curve (pl, 0.f, [&] (float t) { return swelled (t, A); }), amberC, &pl);
                markAt (g, pl, A, amberC, ms (A));
                return "at noon every pick swells in over the same time";
            }
            glowLine (g, curve (pl, 0.f, [&] (float t) { return swelled (t, soft); }), guitarC);
            glowLine (g, curve (pl, 0.f, [&] (float t) { return swelled (t, hard); }), amberC, &pl);
            markAt (g, pl, hard, amberC, "HARD " + ms (hard), pl.base - pl.h + 16.f);
            markAt (g, pl, soft, guitarC, "SOFT " + ms (soft), pl.base - pl.h + 30.f);
            return tch > 0.f ? "hard picks swell faster, soft picks slower" : "soft picks swell faster, hard picks slower";
        }
        if (id == "follow")   // PACE: two slow notes, then four fast ones; scaled up
        {
            const float f = P ("follow");
            const float on[6] = { 0.f, 1.15f, 2.1f, 2.4f, 2.7f, 3.0f };
            float a[6];
            for (int i = 0; i < 6; ++i) a[i] = i == 0 ? A : pacedAttack (A, on[i] - on[i - 1]);
            const float pk = peakOf ([&] (float t) { float m = 0.f; for (int i = 0; i < 6; ++i) m = juce::jmax (m, swelled (t - on[i], A)); return m; }, kT);
            const Plot pl = timeGrid (g, b, kT, 0.95f / pk);
            for (int i = 0; i < 6; ++i)
            {
                const float o = on[i];
                dashed (g, curve (pl, o, [&] (float t) { return swelled (t - o, A); }), amberC.withAlpha (0.4f));
                glowLine (g, curve (pl, o, [&] (float t) { return swelled (t - o, a[i]); }), amberC, i == 0 ? &pl : nullptr);
            }
            tag (g, "SLOW NOTES", pl.x (0.05f), pl.base - pl.h + 16.f, cText.withAlpha (0xcc / 255.f));
            tag (g, "FAST NOTES", pl.x (2.1f), pl.base - pl.h + 16.f, cText.withAlpha (0xcc / 255.f));
            if (f <= 0.005f) return "off: every note swells over the full ATTACK";
            return "fast notes swell over " + ms (a[5]) + " instead of " + ms (A) + " (dashed)";
        }
        if (id == "even")
        {
            const float e = P ("even");
            const float amp[2] = { 0.42f, 1.f }, on[2] = { 0.f, 1.7f };
            const float pkE = peakOf ([&] (float t) { float m = 0.f; for (int i = 0; i < 2; ++i) m = juce::jmax (m, juce::jmax (amp[i] * natural (t - on[i]), (amp[i] + (0.72f - amp[i]) * e) * swelled (t - on[i], A))); return m; }, kT);
            const Plot pl = timeGrid (g, b, kT, 0.95f / juce::jmax (1.f, pkE));
            for (int i = 0; i < 2; ++i)
            {
                const float o = on[i], av = amp[i], lev = av + (0.72f - av) * e;
                dashed (g, curve (pl, o, [=] (float t) { return av * natural (t - o); }), guitarC.withAlpha (0.55f));
                glowLine (g, curve (pl, o, [&] (float t) { return lev * swelled (t - o, A); }), amberC, &pl);
                tag (g, i == 0 ? "SOFT PICK" : "HARD PICK", pl.x (o) + 22.f, pl.base - pl.h + 16.f, cText.withAlpha (0xcc / 255.f));
            }
            return e > 0.01f ? "soft and hard picks come out closer in level" : "off: each note keeps its own level";
        }

        // the plain swell against the guitar, plus what the knob adds; scaled so the tallest curve fits (the POG2 curve's
        // own make-up gain lifts a long swell above the guitar's pick, as the pedal measured)
        const float volG = id == "vol" ? juce::jmax (1.f, juce::Decibels::decibelsToGain (P ("vol"))) : 1.f;
        const float pkAll = peakOf ([&] (float t) { return juce::jmax (natural (t), volG * swelled (t, A)); }, kT);
        const Plot pl = timeGrid (g, b, kT, 0.95f / juce::jmax (1.f, pkAll));
        dashed (g, curve (pl, 0.f, [] (float t) { return natural (t); }), guitarC.withAlpha (0.6f));
        tag (g, "GUITAR", pl.x (0.f) + 18.f, pl.y (0.9f), guitarC.withAlpha (0.85f));
        const float vol = id == "vol" ? juce::Decibels::decibelsToGain (P ("vol")) : 1.f;
        if (id == "vol") dashed (g, curve (pl, 0.f, [&] (float t) { return swelled (t, A); }), amberC.withAlpha (0.45f));
        if (id == "levelMatch" && P ("levelMatch") > 0.5f)      // dashed: the same swell without MATCH
            dashed (g, curve (pl, 0.f, [&] (float t) { return natural (t) * swellGain (A < 0.002f ? 1.f : t / A, juce::roundToInt (P ("swellShape"))); }), amberC.withAlpha (0.5f));
        if (id == "sustain" || id == "sustainMax")              // dashed: the same note without SUSTAIN
            dashed (g, curve (pl, 0.f, [&] (float t) { return natural (t) * swellGain (A < 0.002f ? 1.f : t / A, juce::roundToInt (P ("swellShape"))); }), amberC.withAlpha (0.45f));
        glowLine (g, curve (pl, 0.f, [&] (float t) { return vol * swelled (t, A); }), amberC, &pl);
        markAt (g, pl, A, amberC);
        if (id == "dry")
        {
            const float d = P ("dry");
            if (d > 0.005f) glowLine (g, curve (pl, 0.f, [=] (float t) { return d * natural (t); }), paleC);
            return d > 0.005f ? "the unswelled guitar is mixed in: the pick is heard" : "off: only the swelled sound";
        }
        if (id == "feedback" || id == "fbRise" || id == "fbTone")
        {
            const float fb = P ("feedback"), rise = P ("fbRise");
            if (fb > 0.005f)
            {
                auto ov = [=] (float t)
                {
                    const float x = juce::jlimit (0.f, 1.f, (t - A - 0.25f * rise) / (0.75f * rise));
                    return 0.8f * fb * x * x * (3.f - 2.f * x) * std::pow (10.f, -0.6f * t / 20.f) * 0.75f;
                };
                glowLine (g, curve (pl, A, ov), Colour (0xffff8a6a));
                tag (g, "FEEDBACK", pl.x (juce::jmin (A + rise, 2.5f)) + 4.f, pl.y (ov (juce::jmin (A + rise, 2.5f))) - 11.f, Colour (0xffff8a6a));
            }
            if (id == "fbTone") return "how bright the fed-back harmonic is";
            return fb > 0.005f ? "a held note blooms into feedback after " + ms (A + 0.25f * rise) : "off: no feedback";
        }
        if (id == "sustain" || id == "sustainMax") return P ("sustain") > 0.005f ? "the note fades more slowly than the guitar (dashed: without)" : "off: the note fades like the guitar";
        if (id == "vol") return "output level (dashed: 0 dB)";
        if (id == "levelMatch") return P ("levelMatch") > 0.5f ? "the swell ends as loud as the pick was (dashed: without)" : "off: a long swell ends quieter than the pick";
        if (id == "swellShape")
        {
            const int sh = juce::roundToInt (P ("swellShape"));
            return sh == 2 ? "POG2: a fast start that eases in, with the pedal's own gain" : sh == 1 ? "bowed: a slower, S-shaped start" : "classic: slow start, then faster";
        }
        if (id == "smoothRepick") return "re-picking a ringing note: it dips a little instead of starting from silence";
        return A < 0.002f ? String ("no swell: the note starts at once") : "each note fades in over " + ms (A);
    }

    // ---------------------------------------------------------------- the filter picture
    String paintFilter (Graphics& g, Rect b)
    {
        const Colour teal (0xff7fd1bd), guitarC (0xffd8c3a2);
        const float x0 = 14.f, w = b.getWidth() - 28.f, top = 26.f, bot = b.getHeight() - 22.f;
        const float fLo = 60.f, fHi = 14000.f;
        auto X = [&] (float f) { return x0 + w * std::log (f / fLo) / std::log (fHi / fLo); };
        auto Y = [&] (float db) { return juce::jlimit (top - 4.f, bot, top + (bot - top) * (18.f - db) / 54.f); };
        g.setColour (cGlow.withAlpha (0x33 / 255.f)); g.fillRect (x0, Y (0.f), w, 1.f);
        const std::pair<float, const char*> marks[] = { { 100.f, "100" }, { 1000.f, "1 k" }, { 10000.f, "10 k" } };
        for (auto& m : marks)
        {
            g.setColour (cGlow.withAlpha (0x22 / 255.f)); g.fillRect (X (m.first), top, 1.f, bot - top + 4.f);
            tag (g, m.second, X (m.first) + 3.f, bot - 7.f, cText.withAlpha (0x88 / 255.f));
        }
        const bool fon = P ("filterOn") > 0.5f;
        const float fc = P ("freq"), q = 0.5f * std::pow (20.f, juce::jlimit (0.f, 1.f, P ("res")));
        const int slope = juce::roundToInt (P ("slope"));
        auto response = [&] (float cut)
        {
            juce::Path p;
            for (int i = 0; i <= 160; ++i)
            {
                const float f = fLo * std::pow (fHi / fLo, (float) i / 160.f), r = f / cut, r2 = r * r;
                float h2 = slope == 0 ? 1.f / (1.f + r2) : 1.f / ((1.f - r2) * (1.f - r2) + r2 / (q * q));
                if (slope == 4) h2 /= (1.f + r2);
                else if (slope >= 2) h2 /= (1.f + r2 * r2);
                if (slope == 3) h2 /= (1.f + r2 * r2) * (1.f + r2 * r2);
                const float y = Y (10.f * std::log10 (h2 + 1.0e-9f));
                if (i == 0) p.startNewSubPath (X (f), y); else p.lineTo (X (f), y);
            }
            return p;
        };
        const Plot pl { x0, w, bot, bot - top };
        if (! fon)
        {
            juce::Path flat; flat.startNewSubPath (x0, Y (0.f)); flat.lineTo (x0 + w, Y (0.f));
            glowLine (g, flat, guitarC);
            dashed (g, response (fc), teal.withAlpha (0.45f));
            return "FILTER is off: nothing is cut. Switch it on in the panel";
        }
        const float mod = P ("mod"), oct = std::abs (mod) < 0.03f ? 0.f : std::abs (mod) * 6.f;
        const float other = fc / std::pow (2.f, oct);      // the far end of the sweep (always below TONE)
        const float dyn = P ("pickFilter"), dynOct = dyn * 2.f;
        if (focusId == "pickFilter" && std::abs (dyn) > 0.02f)
        {
            dashed (g, response (juce::jlimit (40.f, 16000.f, fc * std::pow (2.f, dynOct))), guitarC.withAlpha (0.65f));
            dashed (g, response (juce::jlimit (40.f, 16000.f, fc * std::pow (2.f, -dynOct))), guitarC.withAlpha (0.4f));
            tag (g, "HARD PICK", X (juce::jlimit (80.f, 5000.f, fc * std::pow (2.f, dynOct))) + 4.f, top + 20.f, guitarC.withAlpha (0.9f));
        }
        if (oct > 0.f)
        {
            // the cutoff travels during the swell: from the dashed curve to the solid one
            const float from = mod > 0.f ? other : fc, to = mod > 0.f ? fc : other;
            dashed (g, response (juce::jmax (40.f, from)), teal.withAlpha (0.65f), 1.6f);
            glowLine (g, response (juce::jmax (40.f, to)), teal, &pl);
            const float ya = top + 4.f, xa = X (juce::jmax (fLo, from)), xb = X (juce::jmax (fLo, to));
            g.setColour (teal); g.fillRect (juce::jmin (xa, xb), ya, std::abs (xb - xa), 2.f);
            juce::Path head; const float d = xb > xa ? -1.f : 1.f;
            head.addTriangle (xb, ya + 1.f, xb + d * 8.f, ya - 4.f, xb + d * 8.f, ya + 6.f);
            g.fillPath (head);
            tag (g, "START", xa + (xb > xa ? 0.f : -34.f), ya + 12.f, teal.withAlpha (0.85f));
            if (focusId == "mod") return mod > 0.f ? "the filter opens up to TONE during the swell" : "the filter closes down from TONE during the swell";
        }
        else glowLine (g, response (fc), teal, &pl);
        if (focusId == "res") return "a peak at the filter frequency";
        if (focusId == "slope") return "how steeply it cuts above the frequency";
        if (focusId == "pickFilter") return std::abs (dyn) > 0.02f ? (dyn > 0.f ? "harder picks open the filter further" : "harder picks close the filter") : "off: picks do not move the filter";
        if (focusId == "mod") return "noon: the filter stays where TONE puts it";
        return "everything above " + tidyValue (String (fc), "Hz") + " is turned down";
    }

    // ---------------------------------------------------------------- MOTION
    String paintVibrato (Graphics& g, Rect b)
    {
        const Colour gold (0xfff2c96a);
        const Plot pl = timeGrid (g, b);
        const float mid = pl.base - pl.h * 0.5f, half = pl.h * 0.5f;
        g.setColour (cGlow.withAlpha (0x33 / 255.f)); g.fillRect (pl.x0, mid, pl.w, 1.f);
        const float depth = on ("vibDepthOn") ? P ("vibDepth") : 0.f, rate = P ("vibRate"), delay = P ("vibDelay");
        const bool human = on ("vibHuman");
        juce::Path p; float ph = 0.f; const int n = 420;
        for (int i = 0; i <= n; ++i)
        {
            const float t = kT * (float) i / (float) n, u = t - delay;
            float env = 0.f;
            if (u > 0.f)
            {
                float r = rate;
                if (human) { const float x = juce::jmin (1.f, u / 0.9f); env = x * x * (3.f - 2.f * x); r = rate * (0.85f + 0.15f * juce::jmin (1.f, u / 1.2f)); }
                else env = juce::jmin (1.f, u / 0.35f);
                ph += 2.f * kPi * r * kT / (float) n;
            }
            const float y = mid - depth * env * std::sin (ph) / 50.f * half;
            if (i == 0) p.startNewSubPath (pl.x (t), y); else p.lineTo (pl.x (t), y);
        }
        glowLine (g, p, gold);
        tag (g, "+50 C", pl.x0 + 2.f, pl.base - pl.h + 16.f, cText.withAlpha (0x88 / 255.f));
        tag (g, "-50 C", pl.x0 + 2.f, pl.base - 8.f, cText.withAlpha (0x88 / 255.f));
        if (delay > 0.02f && depth > 0.1f) markAt (g, pl, delay, gold);
        static const char* target[3] = { "the voice", "guitar and voice", "the guitar" };
        if (depth < 0.1f) return "off: the pitch stays put";
        return "pitch +-" + String (juce::roundToInt (depth)) + " cents, " + String (rate, 1) + " times a second, on "
             + target[juce::jlimit (0, 2, juce::roundToInt (P ("vibTarget")))] + (human ? ", settling in" : "");
    }

    String paintTremolo (Graphics& g, Rect b)
    {
        const Plot pl = timeGrid (g, b);
        const float A = P ("attack") * 0.001f, depth = on ("tremoloOn") ? P ("tremolo") : 0.f, rate = P ("tremRate");
        const int shape = juce::roundToInt (P ("tremShape"));
        auto lfo = [&] (float t)
        {
            const float x = t * rate - std::floor (t * rate);
            if (shape == 1) return x < 0.5f ? 2.f * x : 2.f - 2.f * x;
            if (shape == 2) return 0.5f - 0.5f * std::tanh (5.f * std::cos (2.f * kPi * x)) / std::tanh (5.f);
            return 0.5f - 0.5f * std::cos (2.f * kPi * x);
        };
        dashed (g, curve (pl, 0.f, [&] (float t) { return swelled (t, A); }), cAmber.withAlpha (0.45f));
        glowLine (g, curve (pl, 0.f, [&] (float t) { return swelled (t, A) * (1.f - depth * lfo (t)); }), Colour (0xfff2c96a), &pl);
        if (depth < 0.005f) return "off: each note keeps a steady level";
        return on ("tremSyncOn") ? String ("each note pulses on its own, in time with the host tempo")
                                 : "each note pulses " + String (rate, 1) + " times a second, from its own pick";
    }

    String paintSpread (Graphics& g, Rect b)
    {
        const Colour gold (0xfff2c96a);
        const float sp = on ("spreadOn") ? P ("spread") : 0.f;
        const int mode = juce::roundToInt (P ("spreadMode"));
        const float x0 = 34.f, x1 = b.getWidth() - 34.f, top = 34.f, bot = b.getHeight() - 30.f, cx = 0.5f * (x0 + x1);
        for (float y = top - 6.f; y < bot; y += 5.f) { g.setColour (cGlow.withAlpha (0x44 / 255.f)); g.fillRect (cx, y, 1.f, 2.5f); }
        g.setColour (cGlow.withAlpha (0x33 / 255.f)); g.fillRect (x0, bot + 4.f, x1 - x0, 1.f);
        tag (g, "L", x0 - 20.f, 0.5f * (top + bot), cText.withAlpha (0x99 / 255.f));
        tag (g, "R", x1 + 20.f, 0.5f * (top + bot), cText.withAlpha (0x99 / 255.f), juce::Justification::centredRight);
        static const char* names[6] = { "E2", "A2", "D3", "G3", "B3", "E4" };
        const float strength[6] = { -0.5f, 0.7f, -0.9f, 0.2f, 0.9f, -0.2f };
        uint32_t seed = 12345u;
        for (int i = 0; i < 6; ++i)
        {
            const float alt = (i & 1) ? 1.f : -1.f;
            seed = seed * 1664525u + 1013904223u;
            float pan = alt;
            if (mode == 1) pan = (float) (seed >> 8) / 8388608.f - 1.f;
            else if (mode == 2) pan = -1.f + 2.f * (float) i / 5.f;
            else if (mode == 3) pan = strength[i];
            else if (mode == 4) pan = alt * (1.f - 0.5f * (strength[i] + 1.f));
            else if (mode == 5) pan = alt * 0.5f * (strength[i] + 1.f);
            const float x = cx + pan * sp * (x1 - cx), y = top + (bot - top) * ((float) i + 0.5f) / 6.f, r = 2.5f + 1.6f * (strength[i] + 1.f);
            g.setColour (gold.withAlpha (0.35f)); g.fillRect (juce::jmin (cx, x), y - 0.5f, std::abs (x - cx), 1.f);
            g.setColour (gold.withAlpha (0.25f)); g.fillEllipse (x - r - 3.f, y - r - 3.f, 2.f * r + 6.f, 2.f * r + 6.f);
            g.setColour (gold); g.fillEllipse (x - r, y - r, 2.f * r, 2.f * r);
            tag (g, names[i], x + r + 4.f, y, gold.withAlpha (0.85f));
        }
        static const char* how[6] = { "notes alternate left and right", "each note lands somewhere random",
                                      "low notes left, high notes right", "soft picks left, hard picks right",
                                      "hard picks in the middle, soft ones at the sides", "soft picks in the middle, hard ones at the sides" };
        if (sp < 0.005f) return "off: every note in the middle";
        return String (how[juce::jlimit (0, 5, mode)]) + " (dot size = pick strength)";
    }

    String paintDetune (Graphics& g, Rect b)
    {
        const Plot pl = timeGrid (g, b);
        const float mid = pl.base - pl.h * 0.5f, half = pl.h * 0.5f;
        g.setColour (cGlow.withAlpha (0x33 / 255.f)); g.fillRect (pl.x0, mid, pl.w, 1.f);
        const float d = P ("detune"), wob = 0.f;
        const float a = on ("detuneOn") ? PolySwellProcessor::detuneCopyLevel (P ("detuneMix")) * juce::jmin (1.f, d / 5.f) : 0.f;
        // the note itself, fading as the copies take over
        juce::Path nt; nt.startNewSubPath (pl.x0, mid); nt.lineTo (pl.x0 + pl.w, mid);
        if (a < 0.995f) glowLine (g, nt, cAmber.withAlpha (0.25f + 0.75f * (1.f - a)));
        juce::Path l, r;
        for (int i = 0; i <= 200; ++i)
        {
            const float t = kT * (float) i / 200.f;
            const float yl = mid - d * (1.f + 0.4f * wob * std::sin (2.f * kPi * 0.4f * t)) / 25.f * half;
            const float yr = mid + d * (1.f + 0.4f * wob * std::sin (2.f * kPi * 0.4f * t + 1.7f)) / 25.f * half;
            if (i == 0) { l.startNewSubPath (pl.x (t), yl); r.startNewSubPath (pl.x (t), yr); }
            else { l.lineTo (pl.x (t), yl); r.lineTo (pl.x (t), yr); }
        }
        if (a > 0.0005f) { glowLine (g, l, Colour (0xfff2c96a).withAlpha (0.25f + 0.75f * a)); glowLine (g, r, Colour (0xffd8c3a2).withAlpha (0.25f + 0.75f * a)); }
        tag (g, "LEFT +" + String (d, 1) + " C", pl.x0 + 2.f, juce::jmax (pl.base - pl.h + 14.f, mid - d / 25.f * half - 10.f), Colour (0xfff2c96a));
        tag (g, "RIGHT -" + String (d, 1) + " C", pl.x0 + 2.f, juce::jmin (pl.base - 6.f, mid + d / 25.f * half + 10.f), Colour (0xffd8c3a2));
        if (a <= 0.0005f) return "off: just the note";
        if (a >= 0.995f) return "only the two copies, the note itself gone (a POG2 at the top)";
        const float db = 20.f * std::log10 (a / (2.f * (1.f - a)));
        return "copies " + String (d, 1) + " c up and down, " + String (juce::roundToInt (-db)) + " dB under the note" + (wob > 0.01f ? ", wobbling" : "");
    }

    // ---------------------------------------------------------------- LAYER
    static float waveWeight (int wave, int h)
    {
        const float fh = (float) h;
        switch (wave)
        {
            case 2: return std::abs (std::sin (kPi * fh * 0.3f)) / fh;                       // PWM
            case 3: return (h & 1) ? 1.f / fh : 0.02f;                                         // square
            case 4: return (h == 1 || h == 2 || h == 3 || h == 4 || h == 6 || h == 8) ? 0.8f / std::sqrt (fh) : 0.f;   // organ
            case 5: return (h % 3 == 0 ? 2.f : 1.f) / std::pow (fh, 1.6f);                   // glass
            case 6: return ((h & 1) ? 1.f : 0.5f) / std::pow (fh, 0.8f);                     // reed
            case 7: return 1.f / std::pow (fh, 0.92f);                                         // VG pad
            case 8: return std::abs (std::sin (kPi * fh / 7.3f)) / fh;                       // bow
            default: return 1.f / fh;                                                          // saw, supersaw
        }
    }

    String paintLayer (Graphics& g, Rect b)
    {
        const Colour blue (0xff8fb8e8), amberC (cAmber);
        const float mix = on ("voiceMixOn") ? P ("voiceMix") : 0.f;
        if (focusId == "voiceGlide" || focusId == "voiceCharacter")
        {
            const Plot pl = timeGrid (g, b);
            const float yLo = pl.base - pl.h * 0.25f, yHi = pl.base - pl.h * 0.8f;
            juce::Path p;
            if (focusId == "voiceGlide")
            {
                const float gl = P ("voiceGlide");
                juce::Path step; step.startNewSubPath (pl.x (0.f), yLo); step.lineTo (pl.x (1.2f), yLo); step.lineTo (pl.x (1.2f), yHi); step.lineTo (pl.x (kT), yHi);
                dashed (g, step, amberC.withAlpha (0.55f));
                for (int i = 0; i <= 300; ++i)
                {
                    const float gt = gl / (1.f + 5.f / 7.f);   // E3 -> A3, 5 semitones: slides faster than GLIDE (as the engine)
                    const float t = kT * (float) i / 300.f, left = gl < 0.003f ? (t >= 1.2f ? 0.f : 1.f) : 1.f - juce::jlimit (0.f, 1.f, (t - 1.2f) / gt);
                    const float y = yHi + (yLo - yHi) * (t < 1.2f ? 1.f : std::pow (left, 2.2f));
                    if (i == 0) p.startNewSubPath (pl.x (t), y); else p.lineTo (pl.x (t), y);
                }
                glowLine (g, p, blue);
                tag (g, "E3", pl.x (0.05f), yLo - 10.f, blue); tag (g, "A3", pl.x (2.6f), yHi - 10.f, blue);
                return gl < 0.003f ? String ("off: the voice jumps to the new note") : "single notes slide into the next; longer jumps faster (here " + ms (gl / (1.f + 5.f / 7.f)) + ")";
            }
            const float ch = P ("voiceCharacter");
            for (int k = 0; k < 3; ++k)
            {
                juce::Path w;
                const float y0 = pl.base - pl.h * (0.22f + 0.28f * (float) k);
                for (int i = 0; i <= 300; ++i)
                {
                    const float t = kT * (float) i / 300.f;
                    const float y = y0 - ch * pl.h * 0.09f * (std::sin (2.f * kPi * (0.31f + 0.07f * (float) k) * t + (float) k) + 0.6f * std::sin (2.f * kPi * (0.73f + 0.11f * (float) k) * t + 2.f * (float) k));
                    if (i == 0) w.startNewSubPath (pl.x (t), y); else w.lineTo (pl.x (t), y);
                }
                glowLine (g, w, blue.withAlpha (0.9f));
            }
            return ch < 0.01f ? String ("off: every voice note holds steady") : "each voice note wanders on its own: pitch, brightness, level";
        }

        const int wave = juce::roundToInt (P ("voiceWave")), oct = juce::roundToInt (P ("voiceOct"));
        const float tone = P ("voiceTone"), q = 0.707f + 7.f * P ("voiceRes") * P ("voiceRes"), f0 = 220.f * std::pow (2.f, (float) (oct - 1));
        const float x0 = 20.f, w = b.getWidth() - 40.f, top = 46.f, base = b.getHeight() - 34.f, h = base - top, slot = w / 16.f;
        auto respAt = [&] (float f) { const float r = f / tone, r2 = r * r; return juce::jmin (1.6f, 1.f / std::sqrt ((1.f - r2) * (1.f - r2) + r2 / (q * q))); };
        float vmax = 0.f;
        for (int k = 1; k <= 16; ++k) vmax = juce::jmax (vmax, waveWeight (wave, k));
        for (int k = 1; k <= 16; ++k)
        {
            const float gx = x0 + slot * (float) (k - 1), gh = 1.f / std::pow ((float) k, 1.1f), vh = waveWeight (wave, k) / vmax * respAt (f0 * (float) k) / 1.6f;
            g.setColour (amberC.withAlpha (0.2f + 0.7f * (1.f - mix))); g.fillRect (gx + slot * 0.12f, base - gh * h, slot * 0.3f, gh * h);
            g.setColour (blue.withAlpha (0.2f + 0.75f * mix)); g.fillRect (gx + slot * 0.48f, base - vh * h, slot * 0.34f, vh * h);
        }
        juce::Path fc;
        for (int i = 0; i <= 120; ++i)
        {
            const float hx = 1.f + 15.f * (float) i / 120.f, y = base - respAt (f0 * hx) / 1.6f * h;
            if (i == 0) fc.startNewSubPath (x0 + slot * (hx - 0.5f), y); else fc.lineTo (x0 + slot * (hx - 0.5f), y);
        }
        dashed (g, fc, blue.withAlpha (0.7f), 1.2f);
        g.setColour (cGlow.withAlpha (0x33 / 255.f)); g.fillRect (x0, base, w, 1.f);
        tag (g, "GUITAR", b.getWidth() - 110.f, 14.f, amberC); tag (g, "VOICE", b.getWidth() - 56.f, 14.f, blue);
        for (int k : { 1, 4, 8, 16 }) tag (g, String (k), x0 + slot * ((float) k - 0.6f), base + 9.f, cText.withAlpha (0x88 / 255.f));
        static const char* waves[9] = { "supersaw", "saw", "PWM", "square", "organ", "glass", "reed", "bowed pad", "bow" };
        if (focusId == "voiceTone") return "the voice's own filter: harmonics above " + tidyValue (String (tone), "Hz") + " are turned down";
        if (focusId == "voiceWave") return String ("the voice's harmonics: ") + waves[juce::jlimit (0, 8, wave)];
        if (focusId == "voiceOct") return oct == 0 ? "the voice plays an octave below the note" : oct == 2 ? "the voice plays an octave above the note" : "the voice plays the note itself";
        if (focusId == "voiceUnison") return "extra detuned copies of the voice, for a wider sound";
        if (focusId == "voiceMono") return on ("voiceMono") ? "mono: only the newest note keeps its voice" : "every note gets its own voice";
        return mix < 0.005f ? String ("off: only the guitar") : "guitar and synth voice: " + String (juce::roundToInt (mix * 100.f)) + " % voice";
    }

    // ---------------------------------------------------------------- SPACE
    String paintSpace (Graphics& g, Rect b)
    {
        const Plot pl = timeGrid (g, b);
        const Colour sage (0xffb5d38c), amberC (cAmber);
        const float size = P ("smearSize"), L = P ("smearLevel"), T60 = 0.3f + 5.7f * size;
        const bool gate = on ("smearGate"), follows = juce::roundToInt (P ("smearSend")) == 1;
        const float holdS = P ("smearHold") * 0.001f, cutS = juce::jmax (0.002f, P ("smearCut") * 0.001f), tEnd = 0.9f;
        const float A = follows ? juce::jmax (0.25f, P ("attack") * 0.001f) : 0.05f;
        auto noteAt = [&] (float t) { return t < tEnd ? swelled (t, A) : swelled (tEnd, A) * std::exp (-(t - tEnd) / 0.03f); };
        // the reverb's energy: fed by the note, decaying 60 dB in T60
        std::array<float, 421> rev {};
        double E = 0.0; const float dt = kT / 420.f;
        for (int i = 0; i <= 420; ++i)
        {
            const float t = (float) i * dt, x = noteAt (t);
            E += (double) dt * (13.8 / T60) * ((double) x * x - E);
            float r = (float) std::sqrt (juce::jmax (0.0, E)) * L * 0.9f;
            if (gate && t > tEnd + holdS) { const float u = juce::jlimit (0.f, 1.f, (t - tEnd - holdS) / cutS); r *= 1.f - u * u * (3.f - 2.f * u); }
            rev[(size_t) i] = r;
        }
        // in dB (0 .. -42): the tail's length is what SIZE changes, and that only shows on a level scale
        auto dbv = [] (float a) { return juce::jlimit (0.f, 1.f, (20.f * std::log10 (a + 1.0e-6f) + 42.f) / 42.f); };
        glowLine (g, curve (pl, 0.f, [&] (float t) { return dbv (rev[(size_t) juce::jlimit (0, 420, juce::roundToInt (t / dt))]); }), sage, &pl);
        glowLine (g, curve (pl, 0.f, [&] (float t) { return dbv (noteAt (t)); }), amberC);
        tag (g, "NOTE", pl.x (0.1f), pl.base - pl.h + 16.f, amberC); tag (g, "REVERB", pl.x (1.2f), pl.base - pl.h + 16.f, sage);
        const String pre = on ("smearOn") ? String() : String ("SPACE is off: ");
        if (focusId == "smearLevel") return pre + String (juce::roundToInt (L * 100.f)) + " % reverb, level-matched";
        if (focusId == "smearDrive") return pre + "soft clipping on the reverb only";
        if (focusId == "smearThresh") return pre + (P ("smearThresh") <= -79.f ? String ("off: all playing feeds the reverb") : "only playing above " + String (juce::roundToInt (P ("smearThresh"))) + " dB feeds the reverb");
        if (focusId == "smearSend") return pre + (follows ? "the reverb builds with each note's swell" : juce::roundToInt (P ("smearSend")) == 2 ? "an expression pedal sets how much feeds it" : "everything feeds the reverb");
        if (focusId == "smearDark") return pre + (on ("smearDark") ? "a darker reverb" : "a bright reverb");
        if (focusId == "smearGate" || focusId == "smearHold" || focusId == "smearCut")
            return pre + (gate ? "cut " + String (juce::roundToInt (holdS * 1000.f)) + " ms after you stop, over " + String (juce::roundToInt (cutS * 1000.f)) + " ms" : String ("gate off: the tail rings out"));
        return pre + "the reverb rings about " + tidyValue (String (T60), "s");
    }

    // ---------------------------------------------------------------- FEEDBACK: which harmonic the amp picks up
    String paintLadder (Graphics& g, Rect b)
    {
        const Colour rust (0xffff8a6a);
        float rA, rB, wA, wB; fbLadder (rA, rB, wA, wB);
        const bool amp = on ("fbAmp") && ! (P ("fbMode") > 0.5f);
        const float x0 = 40.f, x1 = b.getWidth() - 40.f, base = b.getHeight() - 42.f, top = 36.f;
        static const char* names[5] = { "NOTE", "OCTAVE", "12TH", "2 OCT", "2 OCT+3RD" };
        g.setColour (cGlow.withAlpha (0x33 / 255.f)); g.fillRect (x0 - 14.f, base, x1 - x0 + 28.f, 1.f);
        for (int i = 0; i < 5; ++i)
        {
            const float x = x0 + (x1 - x0) * (float) i / 4.f, r = (float) (i + 1);
            const float w = amp ? (r == rA ? wA : 0.f) + (r == rB ? wB : 0.f) : (classicRatio() == r ? 1.f : 0.f);
            g.setColour (rust.withAlpha (0.12f)); g.fillRoundedRectangle (x - 11.f, top, 22.f, base - top, 3.f);
            if (w > 0.01f)
            {
                g.setColour (rust.withAlpha (0.25f)); g.fillRoundedRectangle (x - 14.f, base - w * (base - top) - 3.f, 28.f, w * (base - top) + 3.f, 4.f);
                g.setColour (rust); g.fillRoundedRectangle (x - 10.f, base - w * (base - top), 20.f, w * (base - top), 3.f);
            }
            g.setFont (monoFont (10.f)); g.setColour (cText.withAlpha (0xcc / 255.f));
            g.drawText (names[i], Rect (x - 40.f, base + 4.f, 80.f, 12.f), juce::Justification::centred);
        }
        if (amp)
        {
            const float pos = juce::jlimit (0.f, 1.f, P ("fbDistance")), xm = x0 + (x1 - x0) * pos, drift = 0.06f * (x1 - x0);
            g.setColour (rust.withAlpha (0.18f)); g.fillRect (xm - drift, base + 17.f, 2.f * drift, 3.f);
            juce::Path tri; tri.addTriangle (xm - 5.f, base + 25.f, xm + 5.f, base + 25.f, xm, base + 17.f);
            g.setColour (rust); g.fillPath (tri);
        }
        if (! amp) return P ("fbMode") > 0.5f ? String ("immediate: an overtone from the pick, like an octave pedal") : "AMP model off: a fixed overtone";
        if (focusId == "fbOneWins") return on ("fbOneWins") ? "in a chord the loudest note takes the feedback" : "every held note feeds back";
        return "near: the note; further: octave, 12th, 2 octaves";
    }

    // ---------------------------------------------------------------- live
    float yOfHz (float f, Rect lane) const
    {
        const float u = std::log (juce::jlimit (kFLo, kFHi, f) / kFLo) / std::log (kFHi / kFLo);
        return lane.getBottom() - u * lane.getHeight();
    }

    void paintLive (Graphics& g, Rect b)
    {
        // one plot over the whole glass: the swell (output against the guitar) in front, the notes faint behind it
        const Rect lane (8.f, 13.f, b.getWidth() - 16.f, b.getHeight() - 32.f), env = lane;
        const float nowX = std::round (b.getWidth() * kNowFrac), dx = (nowX - lane.getX()) / (float) (kCols - 1);
        auto X = [&] (int age) { return nowX - (float) age * dx; };
        const int fut = (int) ((lane.getRight() - nowX) / dx);
        const Colour amberC (cAmber), pale (cPale), teal (0xff7fd1bd), rust (0xffff8a6a), blue (0xff8fb8e8), violet (0xffc4a6ec),
                     sage (0xffb5d38c), cream (0xffd8c3a2);

        const bool showNotes = proc.uiWindowNotes;
        const bool filterOn = on ("filterOn");
        const bool fbOn = feedbackOn(), amp = on ("fbAmp") && P ("fbMode") < 0.5f;
        const bool layerOn = on ("voiceOn") && on ("voiceMixOn") && P ("voiceMix") > 0.f, spaceOn = on ("smearOn");
        if (showNotes)
        {
        g.beginTransparencyLayer (0.35f);   // ---- NOTES (optional, behind): the tracked notes over time, the filter veil
        // the open E strings as a faint grid
        g.setFont (monoFont (9.5f));
        for (int m : { 40, 52, 64, 76 })
        {
            const float y = yOfHz (hzOf ((float) m), lane);
            g.setColour (cGlow.withAlpha (0x14 / 255.f)); g.fillRect (lane.getX(), y, lane.getWidth(), 1.f);
            g.setColour (cText.withAlpha (0x66 / 255.f)); g.drawText (noteName ((float) m), Rect (lane.getRight() - 24.f, y - 10.f, 22.f, 10.f), juce::Justification::centredRight);
        }

        // FILTER: a soft veil over everything above the cutoff (no line); the notes under it dim (below)
        auto filterAtt = [] (float hz, float cut) { if (cut <= 0.f) return 1.f; const float r = 2.f * hz / cut; return 1.f / std::sqrt (1.f + r * r * r * r); };
        if (filterOn)
        {
            auto veil = [&] (float x, float w, float cut, float alpha)
            {
                const float y0 = yOfHz (cut, lane), y1 = yOfHz (cut * 1.8f, lane);
                if (y0 > lane.getY() && y0 > y1)
                {
                    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.f), 0.f, y0, juce::Colours::black.withAlpha (alpha), 0.f, y1, false));
                    g.fillRect (x, y1, w, y0 - y1);
                }
                if (y1 > lane.getY()) { g.setColour (juce::Colours::black.withAlpha (alpha)); g.fillRect (x, lane.getY(), w, y1 - lane.getY()); }
            };
            for (int age = kCols - 1; age >= 0; --age) if (col (age).cut > 0.f) veil (X (age) - dx * 0.5f, dx + 0.6f, col (age).cut, 0.5f);
            for (int k = 1; k <= fut; ++k) veil (nowX + ((float) k - 0.5f) * dx, dx + 0.6f, cutoffAt (noteAge >= 0.f ? noteAge + (float) k * kColSec : -1.f), 0.32f);
        }

        // the notes
        float rA, rB, wA, wB; fbLadder (rA, rB, wA, wB);
        const float take = (fbOn && amp) ? 0.65f * ((rA == 1.f ? 0.f : wA) + wB) : 0.f;
        const float tailCols = (0.15f + 2.2f * P ("smearSize")) / kColSec;   // a longer SIZE, a longer tail
        juce::Path body[12], glow, core, over;
        auto quad = [] (juce::Path& p, float xa, float ya, float ta, float xb, float yb, float tb)
        { p.startNewSubPath (xa, ya - ta); p.lineTo (xb, yb - tb); p.lineTo (xb, yb + tb); p.lineTo (xa, ya + ta); p.closeSubPath(); };
        struct Tail { float x, y, len, th; };
        std::vector<Tail> tails;
        for (int age = kCols - 1; age >= 1; --age)
        {
            const Col& a = col (age); const Col& nx = col (age - 1);
            for (int j = 0; j < a.n; ++j)
            {
                int k = -1; float best = 0.7f;
                for (int q = 0; q < nx.n; ++q) if (std::abs (nx.midi[q] - a.midi[j]) < best) { best = std::abs (nx.midi[q] - a.midi[j]); k = q; }
                const float ya = yOfHz (hzOf (a.midi[j]), lane), ta = 0.55f + 2.1f * a.lvl[j] * (1.f - take * a.fb[j]);
                if (k < 0)
                {
                    if (spaceOn) tails.push_back ({ X (age), ya, juce::jmin (tailCols, (float) age) * dx, ta });
                    continue;
                }
                const float yb = yOfHz (hzOf (nx.midi[k]), lane), tb = 0.55f + 2.1f * nx.lvl[k] * (1.f - take * nx.fb[k]);
                const int hotB = juce::jlimit (0, 3, (int) (0.5f * (a.hot[j] + nx.hot[k]) * 4.f));
                const float att = filterOn ? filterAtt (hzOf (a.midi[j]), a.cut) : 1.f;   // how much the filter dulls this note
                const int bucket = hotB * 3 + (att > 0.8f ? 0 : att > 0.4f ? 1 : 2);
                quad (body[bucket], X (age), ya, ta, X (age - 1), yb, tb);
                quad (glow, X (age), ya, ta + 1.8f, X (age - 1), yb, tb + 1.8f);
                if (layerOn) quad (core, X (age), ya, 0.45f, X (age - 1), yb, 0.45f);
                if (fbOn && (a.fb[j] > 0.02f || nx.fb[k] > 0.02f))
                    for (int pass = 0; pass < 2; ++pass)
                    {
                        const float r = amp ? (pass == 0 ? rA : rB) : classicRatio(), w = amp ? (pass == 0 ? wA : wB) : (pass == 0 ? 1.f : 0.f);
                        if (w < 0.02f || r == 1.f) continue;
                        const float dm = 12.f * std::log2 (r);
                        quad (over, X (age), yOfHz (hzOf (a.midi[j] + dm), lane), 0.3f + 2.2f * a.fb[j] * w * a.lvl[j],
                              X (age - 1), yOfHz (hzOf (nx.midi[k] + dm), lane), 0.3f + 2.2f * nx.fb[k] * w * nx.lvl[k]);
                    }
            }
        }
        for (auto& t : tails)   // SPACE: the reverb carrying on after a note stops
        {
            g.setGradientFill (juce::ColourGradient (sage.withAlpha (0.4f), t.x, t.y, sage.withAlpha (0.f), t.x + t.len, t.y, false));
            g.fillRect (t.x, t.y - t.th - 1.5f, t.len, 2.f * t.th + 3.f);
        }
        g.setColour ((spaceOn ? sage : cGlow).withAlpha (0.16f)); g.fillPath (glow);
        for (int k = 0; k < 12; ++k)
        {
            const float dim = (float) (k % 3) / 2.f;
            g.setColour (amberC.interpolatedWith (pale, (float) (k / 3) / 3.f).interpolatedWith (Colour (0xff6a3410), 0.7f * dim));
            g.fillPath (body[k]);
        }
        if (! over.isEmpty())
        {
            g.setColour (rust.withAlpha (0.25f)); g.strokePath (over, juce::PathStrokeType (3.f));
            g.setColour (rust); g.fillPath (over);
        }
        if (layerOn) { g.setColour (blue.withAlpha (0.4f + 0.6f * juce::jmin (1.f, P ("voiceMix") * 1.5f))); g.fillPath (core); }

        // the notes sounding now, named
        {
            const Col& now = col (0);
            std::vector<float> ys;
            g.setFont (monoFont (10.5f));
            for (int j = 0; j < now.n; ++j) ys.push_back (now.midi[j]);
            std::sort (ys.begin(), ys.end());
            float lastY = 1.0e9f;
            for (float m : ys)
            {
                const float y = yOfHz (hzOf (m), lane);
                if (std::abs (y - lastY) < 9.f) continue;
                lastY = y;
                g.setColour (pale.withAlpha (0.9f)); g.drawText (noteName (m), Rect (nowX + 6.f, y - 6.f, 40.f, 12.f), juce::Justification::centredLeft);
            }
        }

        // "now" on the notes' own time axis
        for (float y = lane.getY() - 4.f; y < lane.getBottom(); y += 5.f) { g.setColour (cTextHi.withAlpha (0x40 / 255.f)); g.fillRect (nowX, y, 1.f, 2.5f); }
        g.endTransparencyLayer();
        }
        juce::ignoreUnused (env, fut);

        // ---- LEVEL (left): each pick's own swell - its attack after FEEL and PACE, its level after EVEN - with a dot
        // travelling along the latest; the picks before it stay as faint ghosts. TONE (right): the filter on the latest
        // note right now - MOD sweeping it, DYN moving it by the pick, RES as the peak.
        const double nowS = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const float A0 = juce::jmax (0.f, P ("attack") * 0.001f);
        const float Tw = juce::jlimit (1.2f, 9.f, A0 * 1.4f + 1.f);
        const float split = filterOn ? std::round (b.getWidth() * 0.63f) : b.getWidth();   // no filter: LEVEL takes the glass
        const Rect pa (14.f, 24.f, split - 20.f, b.getHeight() - 46.f);                       // LEVEL
        const Rect fr (split + 8.f, 24.f, b.getWidth() - split - 16.f, b.getHeight() - 46.f);  // TONE
        if (filterOn) { g.setColour (cGlow.withAlpha (0.12f)); g.fillRect (split, pa.getY() - 8.f, 1.f, pa.getHeight() + 12.f); }
        auto tx = [&] (float t) { return pa.getX() + pa.getWidth() * juce::jlimit (0.f, 1.f, t / Tw); };
        const float even = P ("even"), touch = P ("touch");
        auto ampOf = [&] (float strength) { return std::pow (10.f, strength * 12.f * (1.f - even) / 40.f); };   // half the dB, so soft picks stay readable
        float pkRef = 1.0e-6f;
        for (int k = 0; k <= 120; ++k) pkRef = juce::jmax (pkRef, swelled (Tw * (float) k / 120.f, A0));
        const float full = pkRef * ampOf (1.f) / 0.95f;   // the hardest pick peaks just under the top (nothing is squashed)
        auto ay = [&] (float a) { return pa.getBottom() - pa.getHeight() * juce::jlimit (0.f, 1.f, a / full); };
        auto curveOf = [&] (float amp, float att, float upTo)
        {
            juce::Path c;
            const int n = 120;
            for (int k = 0; k <= n; ++k)
            {
                const float t = upTo * (float) k / (float) n;
                if (k == 0) c.startNewSubPath (tx (t), ay (amp * swelled (t, att))); else c.lineTo (tx (t), ay (amp * swelled (t, att)));
            }
            return c;
        };
        g.setFont (monoFont (9.f));
        for (float sec = 1.f; sec < Tw - 0.25f; sec += (Tw > 5.f ? 2.f : 1.f))
        {
            g.setColour (cGlow.withAlpha (0.09f)); g.fillRect (tx (sec), pa.getY(), 1.f, pa.getHeight());
            g.setColour (cText.withAlpha (0.4f)); g.drawText (String ((int) sec) + " S", Rect (tx (sec) + 2.f, pa.getBottom() + 1.f, 30.f, 9.f), juce::Justification::centredLeft);
        }
        g.setColour (cGlow.withAlpha (0.2f)); g.fillRect (pa.getX(), pa.getBottom(), pa.getWidth(), 1.f);
        const bool havePick = ! picks.empty() && nowS - picks.back().t0 < 30.0;
        String caption;
        float latestAge = -1.f, latestAtt = A0, latestPickStr = 0.f;
        if (havePick)
        {
            for (size_t k = 0; k + 1 < picks.size(); ++k)   // ghosts of the picks before
            {
                const auto& pk = picks[k];
                const float age0 = (float) (nowS - pk.t0), age = pk.endAge >= 0.f ? pk.endAge : age0;
                if (age0 > Tw + 4.f) continue;
                const float amp = ampOf (pk.strength), att = A0 * pk.mul;
                g.setColour (amberC.withAlpha (0.22f)); g.strokePath (curveOf (amp, att, Tw), juce::PathStrokeType (1.f));
                const float tc = juce::jmin (age, Tw);
                g.setColour (amberC.withAlpha (0.45f)); g.fillEllipse (tx (tc) - 2.f, ay (amp * swelled (tc, att)) - 2.f, 4.f, 4.f);
            }
            const auto& pk = picks.back();
            const bool ended = pk.endAge >= 0.f;
            const float age = ended ? pk.endAge : (float) (nowS - pk.t0), amp = ampOf (pk.strength), att = A0 * pk.mul;
            latestAge = age; latestAtt = att; latestPickStr = pk.pickStrength;
            juce::Path dryP;   // the guitar alone, at this pick's level
            for (int k = 0; k <= 120; ++k) { const float t = Tw * (float) k / 120.f, y = ay (amp * natural (t) * (even > 0.f ? 1.f : 1.f)); if (k == 0) dryP.startNewSubPath (tx (t), y); else dryP.lineTo (tx (t), y); }
            dashed (g, dryP, cream.withAlpha (0.4f), 1.f);
            const auto whole = curveOf (amp, att, Tw);
            juce::Path fillP (whole); fillP.lineTo (pa.getRight(), pa.getBottom()); fillP.lineTo (pa.getX(), pa.getBottom()); fillP.closeSubPath();
            g.setGradientFill (juce::ColourGradient (amberC.withAlpha (0.3f), 0.f, pa.getY(), amberC.withAlpha (0.02f), 0.f, pa.getBottom(), false));
            g.fillPath (fillP);
            g.setColour (amberC.withAlpha (0.45f)); g.strokePath (whole, juce::PathStrokeType (1.2f));
            const float tc = juce::jmin (age, Tw);
            if (tc > 0.01f)   // the part this note has already played, lit
            {
                const auto played = curveOf (amp, att, tc);
                g.setColour (cGlow.withAlpha (0.35f)); g.strokePath (played, juce::PathStrokeType (4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (pale); g.strokePath (played, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            const float dx2 = tx (tc), dy2 = ay (amp * swelled (tc, att)), dotA = ended ? 0.4f : age <= Tw ? 1.f : 0.55f;
            g.setGradientFill (juce::ColourGradient (pale.withAlpha (0.6f * dotA), dx2, dy2, cGlow.withAlpha (0.f), dx2 + 12.f, dy2, true));
            g.fillEllipse (dx2 - 12.f, dy2 - 12.f, 24.f, 24.f);
            g.setColour (pale.withAlpha (dotA)); g.fillEllipse (dx2 - 3.f, dy2 - 3.f, 6.f, 6.f);
            if (att > 0.002f && att < Tw) { juce::Path at; at.startNewSubPath (tx (att), pa.getY()); at.lineTo (tx (att), pa.getBottom()); dashed (g, at, cTextHi.withAlpha (0.25f), 1.f); }
            // what this pick did, in words: how hard, and what set its attack
            const float feelMul = std::pow (2.f, -touch * pk.strength * 2.f);
            const String how = pk.strength > 0.3f ? "HARD" : pk.strength < -0.3f ? "SOFT" : "PICK";
            String why;
            const bool byFeel = std::abs (feelMul - 1.f) > 0.06f, byPace = std::abs (pk.mul - feelMul) > 0.06f * feelMul;
            why = byFeel && byPace ? " FEEL+PACE" : byPace ? " PACE" : byFeel ? " FEEL" : "";
            caption = how + " " + ms (att).toUpperCase() + why;
        }
        else
        {
            // nothing played yet: the swell an average pick would get, dashed
            dashed (g, curveOf (1.f, A0, Tw), amberC.withAlpha (0.7f), 1.6f);
            caption = "PLAY A NOTE";
        }
        tag (g, caption, notesRect().getRight() + 8.f, notesRect().getCentreY() + 0.5f, cTextHi.withAlpha (havePick ? 0.9f : 0.5f));

        // TONE: the filter's response on the latest note at this moment (dashed: where its sweep starts and ends)
        if (filterOn)
        {
            const float fLo = 60.f, fHi = 14000.f;
            auto FX = [&] (float f) { return fr.getX() + fr.getWidth() * std::log (f / fLo) / std::log (fHi / fLo); };
            auto FY = [&] (float db) { return juce::jlimit (fr.getY() - 4.f, fr.getBottom(), fr.getY() + fr.getHeight() * (12.f - db) / 42.f); };
            tag (g, "TONE", fr.getX(), notesRect().getCentreY() + 0.5f, (filterOn ? teal : cText).withAlpha (filterOn ? 0.9f : 0.5f));
            g.setColour (cGlow.withAlpha (0.2f)); g.fillRect (fr.getX(), FY (0.f), fr.getWidth(), 1.f);
            for (float f : { 100.f, 1000.f, 10000.f }) { g.setColour (cGlow.withAlpha (0.09f)); g.fillRect (FX (f), fr.getY(), 1.f, fr.getHeight()); }
            g.setFont (monoFont (9.f)); g.setColour (cText.withAlpha (0.4f));
            g.drawText ("1K", Rect (FX (1000.f) + 2.f, fr.getBottom() + 1.f, 20.f, 9.f), juce::Justification::centredLeft);
            const int slope = juce::roundToInt (P ("slope"));
            const float q0 = slope == 4 ? 1.f : 0.5f, q = q0 * std::pow (10.f / q0, juce::jlimit (0.f, 1.f, P ("res")));
            auto resp = [&] (float cut)
            {
                juce::Path rp;
                for (int k = 0; k <= 90; ++k)
                {
                    const float f = fLo * std::pow (fHi / fLo, (float) k / 90.f), r = f / cut, r2 = r * r;
                    float h2 = slope == 0 ? 1.f / (1.f + r2) : 1.f / ((1.f - r2) * (1.f - r2) + r2 / (q * q));
                    if (slope == 4) h2 /= (1.f + r2);
                    else if (slope >= 2) h2 /= (1.f + r2 * r2);
                    if (slope == 3) h2 /= (1.f + r2 * r2) * (1.f + r2 * r2);
                    const float y = FY (10.f * std::log10 (h2 + 1.0e-9f));
                    if (k == 0) rp.startNewSubPath (FX (f), y); else rp.lineTo (FX (f), y);
                }
                return rp;
            };
            if (! filterOn)
            {
                juce::Path flat; flat.startNewSubPath (fr.getX(), FY (0.f)); flat.lineTo (fr.getRight(), FY (0.f));
                g.setColour (cText.withAlpha (0.5f)); g.strokePath (flat, juce::PathStrokeType (1.4f));
                tag (g, "OPEN", fr.getRight(), FY (0.f) + 9.f, cText.withAlpha (0.5f), juce::Justification::centredRight);
            }
            else
            {
                const float fc = P ("freq"), mod = on ("modOn") ? P ("mod") : 0.f, oct = std::abs (mod) < 0.03f ? 0.f : std::abs (mod) * 6.f;
                const float dynOct = P ("pickFilter") * latestPickStr * 4.f;   // DYN: this pick moves the cutoff
                auto cutAtPh = [&] (float ph) { return juce::jlimit (30.f, 18000.f, fc * std::pow (2.f, -oct * (mod > 0.f ? 1.f - ph : ph) + dynOct)); };
                const float ph = latestAge < 0.f ? 1.f : (latestAtt < 0.002f ? 1.f : juce::jlimit (0.f, 1.f, latestAge / latestAtt));
                if (oct > 0.f)   // the sweep's two ends, dashed
                {
                    dashed (g, resp (cutAtPh (0.f)), teal.withAlpha (0.35f), 1.f);
                    dashed (g, resp (cutAtPh (1.f)), teal.withAlpha (0.35f), 1.f);
                }
                const float cNow = cutAtPh (ph);
                const auto now = resp (cNow);
                g.setColour (teal.withAlpha (0.3f)); g.strokePath (now, juce::PathStrokeType (4.f));
                g.setColour (teal); g.strokePath (now, juce::PathStrokeType (1.6f));
                const float cx2 = FX (juce::jlimit (fLo, fHi, cNow));
                g.setColour (teal.withAlpha (0.9f)); g.fillEllipse (cx2 - 2.5f, FY (0.f) - 2.5f, 5.f, 5.f);
                const String hz = cNow >= 1000.f ? String (cNow / 1000.f, cNow >= 10000.f ? 0 : 1) + " KHZ" : String (juce::roundToInt (cNow)) + " HZ";
                tag (g, hz, fr.getRight(), notesRect().getCentreY() + 0.5f, teal.withAlpha (0.85f), juce::Justification::centredRight);
            }
        }

        // the NOTES switch: the tracked notes over time, faint underneath
        {
            const auto r = notesRect();
            g.setColour (cText.withAlpha (showNotes ? 0.9f : 0.4f)); g.drawRoundedRectangle (r, 5.f, 1.f);
            if (showNotes) { g.setColour (cGlow.withAlpha (0.25f)); g.fillRoundedRectangle (r, 5.f); }
            g.setFont (uiFont (8.5f, true, 0.12f)); g.setColour (cTextHi.withAlpha (showNotes ? 1.f : 0.55f));
            g.drawText ("NOTES", r, juce::Justification::centred);
        }

        // what is shaping the sound: lit when in use
        struct Item { const char* s; Colour c; bool lit; };
        const Item items[] = { { "SWELL", amberC, ! lastByp }, { "FILTER", teal, filterOn }, { "FEEDBACK", rust, fbOn },
                               { "LAYER", blue, layerOn }, { "SPACE", sage, spaceOn } };
        const auto lf = uiFont (9.f, true, 0.06f);
        g.setFont (lf);
        float tw = 0.f;
        for (auto& it : items) tw += juce::GlyphArrangement::getStringWidth (lf, it.s) + 8.f;
        const float gapL = juce::jmax (4.f, (b.getWidth() - 20.f - tw) / 4.f);   // spread across the glass
        float lx = 10.f; const float ly = b.getHeight() - 11.f;
        for (auto& it : items)
        {
            const float a = it.lit ? 1.f : 0.28f;
            g.setColour (it.c.withAlpha (a)); g.fillEllipse (lx, ly - 2.5f, 5.f, 5.f);
            const float w = juce::GlyphArrangement::getStringWidth (lf, it.s);
            g.drawText (it.s, Rect (lx + 8.f, ly - 6.f, w + 4.f, 12.f), juce::Justification::centredLeft);
            lx += w + 8.f + gapL;
        }

        // input level: a thin bar on the glass's left edge, the zone where the tracking works best marked
        {
            const float mx = 5.f, y0 = 24.f, y1 = b.getHeight() - 22.f;
            auto mY = [&] (float db) { return y1 - juce::jlimit (0.f, 1.f, (db + 60.f) / 60.f) * (y1 - y0); };
            g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillRoundedRectangle (mx - 1.f, y0 - 1.f, 4.f, y1 - y0 + 2.f, 1.5f);
            g.setColour (pale.withAlpha (0.16f)); g.fillRect (mx, mY (-6.f), 2.f, mY (-24.f) - mY (-6.f));
            g.setColour (meterDb > -3.f ? rust : meterDb < -30.f ? amberC.withAlpha (0.55f) : amberC);
            if (meterDb > -60.f) g.fillRect (mx, mY (meterDb), 2.f, y1 - mY (meterDb));
            if (peakDb > -60.f) { g.setColour (peakDb > -3.f ? rust : pale); g.fillRect (mx - 1.f, mY (peakDb) - 0.5f, 4.f, 1.5f); }
        }
    }

    void paint (Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        juce::Path clip; clip.addRoundedRectangle (b, 5.f);
        g.reduceClipRegion (clip);
        g.setGradientFill (juce::ColourGradient (cGlass1, b.getCentreX(), b.getHeight() * 0.55f, cGlass2, 0.f, 0.f, true));
        g.fillAll();

        if (viewA < 0.99f)
        {
            g.beginTransparencyLayer (1.f - viewA);
            paintLive (g, b);
            g.endTransparencyLayer();
        }

        const int kind = viewKind (focusId);
        if (viewA > 0.01f && kind != 0)
        {
            g.beginTransparencyLayer (viewA);
            String says; Colour tc (cTextHi);
            switch (kind)
            {
                case 1:  says = paintSwell (g, b); break;
                case 2:  says = paintFilter (g, b); tc = Colour (0xff9fe3d2); break;
                case 3:  says = paintVibrato (g, b); tc = Colour (0xfff2c96a); break;
                case 4:  says = paintTremolo (g, b); tc = Colour (0xfff2c96a); break;
                case 5:  says = paintSpread (g, b); tc = Colour (0xfff2c96a); break;
                case 6:  says = paintDetune (g, b); tc = Colour (0xfff2c96a); break;
                case 7:  says = paintLayer (g, b); tc = Colour (0xffa9c8ee); break;
                case 9:  says = paintSpace (g, b); tc = Colour (0xffc5dda4); break;
                default: says = paintLadder (g, b); tc = Colour (0xffffa58a); break;
            }
            g.setFont (monoFont (13.f));
            g.setColour (tc);
            g.drawText (roName + "  " + roValue, b.reduced (12, 7), juce::Justification::topLeft);
            g.setFont (uiFont (12.f, false, 0.03f));
            g.setColour (cCaption);
            g.drawText (says, b.reduced (12, 5), juce::Justification::bottomRight);
            g.endTransparencyLayer();
        }
        else if (roTicks > 0 && roName.isNotEmpty())
        {
            const String s = roName + "  " + roValue;
            g.setFont (monoFont (12.f));
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), s) + 12.f;
            g.setColour (juce::Colours::black.withAlpha (0.6f * juce::jmin (1.f, (float) roTicks / 10.f))); g.fillRoundedRectangle (8.f, 5.f, w, 15.f, 3.f);
            g.setColour (cTextHi.withAlpha (juce::jmin (1.f, (float) roTicks / 10.f)));
            g.drawText (s, Rect (14.f, 5.f, w, 15.f), juce::Justification::centredLeft);
        }

        // glass: a soft reflection and darker edges
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.12f), 0.f, 0.f, juce::Colours::transparentWhite, b.getWidth() * 0.5f, b.getHeight() * 0.9f, false));
        g.fillAll();
        const float e = 12.f;
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), 0, 0, juce::Colours::transparentBlack, 0, e, false)); g.fillRect (0.f, 0.f, b.getWidth(), e);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), 0, b.getHeight(), juce::Colours::transparentBlack, 0, b.getHeight() - e, false)); g.fillRect (0.f, b.getHeight() - e, b.getWidth(), e);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), 0, 0, juce::Colours::transparentBlack, e, 0, false)); g.fillRect (0.f, 0.f, e, b.getHeight());
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), b.getWidth(), 0, juce::Colours::transparentBlack, b.getWidth() - e, 0, false)); g.fillRect (b.getWidth() - e, 0.f, e, b.getHeight());
        if (lastByp)
        {
            g.setColour (juce::Colours::black.withAlpha (0.5f)); g.fillAll();
            g.setFont (monoFont (13.f)); g.setColour (cText);
            g.drawText ("BYPASSED", b, juce::Justification::centred);
        }
    }

    PolySwellProcessor& proc;
    std::array<Col, kCols> cols {};
    int head = 0, idle = 0, quiet = 0, roTicks = 0, linger = 0, peakHoldT = 0, pickCols = 99;
    uint32_t lastFrame = 0, lastSeq = 0;
    struct Pick { uint32_t seq; double t0; float mul, strength, pickStrength, endAge = -1.f; };   // mul = its attack / ATTACK; endAge: when it stopped ringing
    std::vector<Pick> picks;
    int liveN = 0;
    float liveMidi[kMaxN] {}, liveLvl[kMaxN] {}, liveT[kMaxN] {};
    float viewA = 0.f, noteAge = -1.f, noteAttack = 0.f, paramSum = 0.f, lvlRef = 1.0e-6f,
          meterDb = -100.f, peakDb = -100.f, inPeakNote = 0.f;
    bool lastByp = false, hadReadout = false, focusHeld = false;
    String roName, roValue, focusId;
};
// ------------------------------------------------------------------------------------------------ LAYER's waves
// harmonic recipes for drawing the nine voice waves (pictures, not the synth itself)
static float waveHarm (int wave, int h)
{
    const float fh = (float) h;
    switch (wave)
    {
        case 0: case 1: return 1.f / fh;                                                          // supersaw, saw
        case 2:  return std::sin (kPi * fh * 0.3f) / fh;                                           // PWM (30 %)
        case 3:  return (h % 2) ? 1.f / fh : 0.f;                                                  // square
        case 4:  { static const float o[9] = { 0.f, 1.f, 0.6f, 0.45f, 0.25f, 0.f, 0.15f, 0.f, 0.1f }; return h < 9 ? o[h] : 0.f; }   // organ drawbars
        case 5:  return h == 1 ? 1.f : h == 4 ? 0.5f : h == 7 ? 0.3f : h == 11 ? 0.2f : 0.f;      // glass
        case 6:  return (h % 2) ? std::pow (fh, -0.8f) : 0.3f / fh;                                // reed
        case 7:  return std::pow (fh, -1.6f);                                                      // bowed pad
        default: return std::abs (std::sin (kPi * fh / 7.3f)) / fh;                               // bow
    }
}
static float waveAt (int wave, float ph, float toneHarm = 1.0e9f)   // ph in cycles; toneHarm: harmonic where TONE rolls off
{
    float v = 0.f;
    for (int h = 1; h <= 24; ++h)
    {
        const float w = waveHarm (wave, h);
        if (w == 0.f) continue;
        const float r = (float) h / toneHarm;
        v += w / std::sqrt (1.f + r * r * r * r) * std::sin (2.f * kPi * (float) h * ph);
    }
    return v;
}

// the voice's wave as it comes out: TONE rounds it, OCTAVE changes how many cycles fit, UNISON adds copies that drift
struct WaveScope : juce::Component, juce::Timer, juce::SettableTooltipClient
{
    WaveScope (PolySwellProcessor& p, Colour a) : proc (p), ac (a)
    { setInterceptsMouseClicks (false, false); startTimerHz (30); }
    float P (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }
    void timerCallback() override
    {
        const float u = P ("voiceUnison");
        if (u > 0.001f && isShowing()) { phase += 0.016f; repaint(); }
        else if (u != lastU || P ("voiceWave") != lastW || P ("voiceTone") != lastT || P ("voiceOct") != lastO) repaint();
        lastU = u; lastW = P ("voiceWave"); lastT = P ("voiceTone"); lastO = P ("voiceOct");
    }
    void paint (Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.35f)); g.fillRoundedRectangle (b, 6.f);
        g.setColour (ac.withAlpha (0.15f)); g.fillRect (b.getX() + 4.f, b.getCentreY(), b.getWidth() - 8.f, 1.f);
        const int wave = juce::roundToInt (P ("voiceWave")), oct = juce::roundToInt (P ("voiceOct"));
        const float cycles = oct == 0 ? 1.f : oct == 2 ? 4.f : 2.f, toneH = juce::jmax (1.f, P ("voiceTone") / (110.f * cycles));
        const float u = P ("voiceUnison");
        float peak = 1.0e-3f;
        for (int i = 0; i < 200; ++i) peak = juce::jmax (peak, std::abs (waveAt (wave, (float) i / 200.f, toneH)));
        auto path = [&] (float stretch, float off)
        {
            juce::Path pth;
            const int n = juce::jmax (60, (int) b.getWidth());
            for (int i = 0; i <= n; ++i)
            {
                const float x = (float) i / (float) n, ph = x * cycles * stretch + off;
                const float y = b.getCentreY() - waveAt (wave, ph, toneH) / peak * (b.getHeight() * 0.38f);
                if (i == 0) pth.startNewSubPath (b.getX() + 4.f + x * (b.getWidth() - 8.f), y); else pth.lineTo (b.getX() + 4.f + x * (b.getWidth() - 8.f), y);
            }
            return pth;
        };
        const int copies = u > 0.001f ? 2 + juce::roundToInt (u * 4.f) : 0;
        for (int k = 0; k < copies; ++k)   // UNISON: detuned copies, drifting
        {
            const float d = ((float) k / (float) juce::jmax (1, copies - 1) - 0.5f) * 2.f;
            g.setColour (ac.withAlpha (0.28f));
            g.strokePath (path (1.f + 0.035f * u * d, phase * d * u * 2.f), juce::PathStrokeType (1.f));
        }
        const auto main = path (1.f, 0.f);
        g.setColour (ac.withAlpha (0.3f)); g.strokePath (main, juce::PathStrokeType (3.5f));
        g.setColour (ac.interpolatedWith (juce::Colours::white, 0.35f)); g.strokePath (main, juce::PathStrokeType (1.5f));
    }
    PolySwellProcessor& proc;
    Colour ac;
    float phase = 0.f, lastU = -1.f, lastW = -1.f, lastT = -1.f, lastO = -1.f;
};

// a small bat-handle toggle switch, like a pedal's mini switch: lever up = on (MATCH on the pedal)
struct MiniToggle : juce::Component, juce::SettableTooltipClient
{
    MiniToggle (PolySwellProcessor& p, const String& id) : bound (p, id, [this] { repaint(); })
    { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const float c = b.getCentreX(), cy = b.getCentreY();
        const bool on = bound.value > 0.5f;
        // the hex nut and the dark bushing
        juce::Path nut;
        for (int k = 0; k < 6; ++k) { const float a = kPi / 6.f + (float) k * kPi / 3.f, x = c + 9.5f * std::cos (a), y = cy + 9.5f * std::sin (a); if (k == 0) nut.startNewSubPath (x, y); else nut.lineTo (x, y); }
        nut.closeSubPath();
        juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 4, { 0, 2 }).drawForPath (g, nut);
        g.setGradientFill (juce::ColourGradient (Colour (0xffe8e6e0), c - 6.f, cy - 8.f, Colour (0xff6c6a66), c + 6.f, cy + 9.f, false));
        g.fillPath (nut);
        g.setColour (juce::Colours::black.withAlpha (0.35f)); g.strokePath (nut, juce::PathStrokeType (0.8f));
        g.setColour (Colour (0xff1a1816)); g.fillEllipse (c - 4.f, cy - 4.f, 8.f, 8.f);
        // the lever: a tapered chrome bat, up (on) or down (off), drawn in perspective
        const float dir = on ? -1.f : 1.f, len = 15.f, tipY = cy + dir * len;
        juce::Path lever;
        lever.startNewSubPath (c - 2.6f, cy); lever.lineTo (c - 3.6f, tipY); lever.lineTo (c + 3.6f, tipY); lever.lineTo (c + 2.6f, cy); lever.closeSubPath();
        g.setGradientFill (juce::ColourGradient (Colour (0xfff7f6f2), c - 3.f, cy, Colour (0xff8a8884), c + 3.f, cy, false));
        g.fillPath (lever);
        g.setColour (juce::Colours::black.withAlpha (0.3f)); g.strokePath (lever, juce::PathStrokeType (0.6f));
        g.setGradientFill (juce::ColourGradient (Colour (0xffffffff), c - 2.f, tipY - 2.f, Colour (0xff9a9894), c + 3.f, tipY + 3.f, true));
        g.fillEllipse (c - 4.2f, tipY - 4.2f, 8.4f, 8.4f);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onMenu) onMenu(); return; }
        bound.set (bound.value > 0.5f ? 0.f : 1.f);
    }
    std::function<void()> onMenu;
    Bound bound;
};

// a choice of LFO shapes as pictures (sine, triangle, square)
struct ShapeSeg : juce::Component, juce::SettableTooltipClient
{
    ShapeSeg (PolySwellProcessor& p, const String& id, Colour a) : ac (a), bound (p, id, [this] { repaint(); })
    { setMouseCursor (juce::MouseCursor::PointingHandCursor); setTooltip ("Tremolo shape: sine, triangle or square."); }
    void paint (Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (0.5f);
        const int cur = juce::roundToInt (bound.value);
        const float w = b.getWidth() / 3.f;
        for (int i = 0; i < 3; ++i)
        {
            const Rect c (b.getX() + w * (float) i, b.getY(), w, b.getHeight());
            if (i == cur) { g.setColour (ac.withAlpha (0.85f)); g.fillRoundedRectangle (c.reduced (1.f), 4.f); }
            juce::Path p;
            const Rect ic = c.reduced (5.f, 6.f);
            for (int k = 0; k <= 24; ++k)
            {
                const float x = (float) k / 24.f, ph = x * 2.f * kPi;
                float y = i == 0 ? std::sin (ph) : i == 1 ? (2.f / kPi) * std::asin (std::sin (ph)) : (std::sin (ph) >= 0.f ? 1.f : -1.f);
                const float px = ic.getX() + x * ic.getWidth(), py = ic.getCentreY() - y * ic.getHeight() * 0.5f;
                if (k == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
            }
            g.setColour (i == cur ? Colour (0xff141018) : ac.interpolatedWith (juce::Colours::white, 0.3f));
            g.strokePath (p, juce::PathStrokeType (1.4f));
        }
        g.setColour (ac.withAlpha (0.6f)); g.drawRoundedRectangle (b, 5.f, 1.f);
    }
    void mouseDown (const juce::MouseEvent& e) override { bound.set ((float) juce::jlimit (0, 2, (int) (e.position.x / ((float) getWidth() / 3.f)))); }
    Colour ac;
    Bound bound;
};

// the nine waves as buttons, each with a small picture of its shape
struct WaveGrid : juce::Component, juce::SettableTooltipClient
{
    WaveGrid (PolySwellProcessor& p, Colour a) : proc (p), ac (a), bound (p, "voiceWave", [this] { repaint(); })
    { setMouseCursor (juce::MouseCursor::PointingHandCursor); setTooltip ("The voice's wave."); }
    static constexpr const char* kNames[9] = { "SUPER", "SAW", "PWM", "SQUARE", "ORGAN", "GLASS", "REED", "PAD", "BOW" };
    Rect cell (int i) const
    {
        const float w = (float) getWidth() / 3.f, h = (float) getHeight() / 3.f;
        return Rect (w * (float) (i % 3), h * (float) (i / 3), w, h).reduced (2.f);
    }
    void paint (Graphics& g) override
    {
        const int cur = juce::roundToInt (bound.value);
        for (int i = 0; i < 9; ++i)
        {
            const auto c = cell (i);
            const bool on = i == cur, over = i == hover;
            g.setColour (on ? ac.withAlpha (0.85f) : juce::Colours::black.withAlpha (over ? 0.35f : 0.22f));
            g.fillRoundedRectangle (c, 4.f);
            g.setColour (ac.withAlpha (on ? 1.f : 0.35f)); g.drawRoundedRectangle (c, 4.f, 1.f);
            const Rect ic (c.getX() + 5.f, c.getY() + 3.f, 22.f, c.getHeight() - 6.f);   // the wave's picture
            float pk = 1.0e-3f;
            for (int k = 0; k < 40; ++k) pk = juce::jmax (pk, std::abs (waveAt (i, (float) k / 40.f)));
            juce::Path w;
            for (int k = 0; k <= 30; ++k)
            {
                const float x = (float) k / 30.f, y = ic.getCentreY() - waveAt (i, x * 2.f) / pk * ic.getHeight() * 0.42f;
                if (k == 0) w.startNewSubPath (ic.getX() + x * ic.getWidth(), y); else w.lineTo (ic.getX() + x * ic.getWidth(), y);
            }
            g.setColour (on ? Colour (0xff141018) : ac.interpolatedWith (juce::Colours::white, 0.3f));
            g.strokePath (w, juce::PathStrokeType (1.3f));
            g.setFont (uiFont (10.f, true, 0.06f));
            g.drawText (kNames[i], c.withTrimmedLeft (30.f), juce::Justification::centredLeft);
        }
    }
    int at (juce::Point<float> p) const { for (int i = 0; i < 9; ++i) if (cell (i).contains (p)) return i; return -1; }
    void mouseMove (const juce::MouseEvent& e) override { const int h = at (e.position); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { if (const int i = at (e.position); i >= 0) bound.set ((float) i); }
    PolySwellProcessor& proc;
    Colour ac;
    Bound bound;
    int hover = -1;
};

// the pull-out tab on the pedal's right edge: shows or hides the effects, with a dot per module in use
struct DrawerTab : juce::Component, juce::SettableTooltipClient
{
    DrawerTab() { setMouseCursor (juce::MouseCursor::PointingHandCursor); setTooltip ("Show or hide the effects"); }
    void paint (Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        juce::Path p; p.addRoundedRectangle (b.getX() - 8.f, b.getY(), b.getWidth() + 7.f, b.getHeight(), 6.f);
        g.setGradientFill (juce::ColourGradient (Colour (over ? 0xff3a312b : 0xff2c2622), 0, b.getY(), Colour (0xff1a1513), 0, b.getBottom(), false));
        g.fillPath (p);
        g.setColour (col::ink.withAlpha (over ? 0.45f : 0.22f)); g.strokePath (p, juce::PathStrokeType (1.f));
        const float cx = b.getCentreX() - 1.f;
        juce::Path ch; const float d = open ? 1.f : -1.f;
        ch.startNewSubPath (cx + 2.5f * d, 10.f); ch.lineTo (cx - 2.5f * d, 15.f); ch.lineTo (cx + 2.5f * d, 20.f);
        g.setColour (col::ink); g.strokePath (ch, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        {
            Graphics::ScopedSaveState ss (g);
            g.addTransform (juce::AffineTransform::rotation (-kPi * 0.5f, cx, b.getHeight() * 0.42f));
            g.setFont (uiFont (11.f, true, 0.3f)); g.setColour (col::ink);
            g.drawText ("EFFECTS", Rect (cx - 40.f, b.getHeight() * 0.42f - 7.f, 80.f, 14.f), juce::Justification::centred);
        }
        for (int i = 0; i < 6; ++i)
        {
            const bool lit = (mask >> i) & 1;
            g.setColour (kMods[i].ac.withAlpha (lit ? 1.f : 0.22f));
            g.fillEllipse (cx - 2.5f, b.getHeight() - 62.f + (float) i * 9.f, 5.f, 5.f);
        }
    }
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { over = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { over = false; repaint(); }
    std::function<void()> onClick;
    bool open = true, over = false;
    int mask = 0;
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
        addAndMakeVisible (drawer);
        drawer.setBounds (juce::roundToInt (kPedalX + kPedalW), juce::roundToInt (kPedalY + 236), 16, 180);
        drawer.onClick = [this] { editor.setPanelOpen (! proc.uiPedalsOpen); };
        window.setBounds (juce::roundToInt (kPedalX + 43), juce::roundToInt (kPedalY + 35), 309, 167);   // in the plate's window glass

        buildBar();
        buildPedal();
        buildPanel();
        setPanelVisible (proc.uiPedalsOpen);
        applySkin (proc.uiSkin);
        startTimerHz (15);
    }
    ~Board() override { setLookAndFeel (nullptr); }

    // ---- Host
    void readout (const String& name, const String& value) override { window.setReadout (name, value); }
    void focus (const String& paramId, bool on) override { window.setFocus (paramId, on); }

    // right-click on a knob or footswitch: MIDI learn, forget, reset
    void midiMenu (const String& id, juce::Component& target) override
    {
        auto* prm = proc.apvts.getParameter (id);
        if (prm == nullptr) return;
        const int cc = proc.ccFor (id);
        juce::PopupMenu m;
        m.addSectionHeader (prm->getName (40));
        if (proc.isLearning (id)) m.addItem (2, "Stop waiting for MIDI");
        else m.addItem (1, cc >= 0 ? "MIDI learn again (now CC " + String (cc) + ")" : String ("MIDI learn: then move a pedal or knob on your controller"));
        if (cc >= 0) m.addItem (3, "Forget CC " + String (cc));
        if (dynamic_cast<juce::AudioParameterBool*> (prm) == nullptr) m.addItem (4, "Reset to default");
        m.setLookAndFeel (&look);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [this, id] (int r)
        {
            auto* p = proc.apvts.getParameter (id);
            if (r == 1) { proc.learnCc (id); learnId = id; }
            else if (r == 2) { proc.stopLearning(); learnId = {}; }
            else if (r == 3) proc.forgetCc (id);
            else if (r == 4 && p != nullptr) { p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture(); }
        });
    }
    void showKnobView (const String& paramId, bool on)   // for tools/ui_snapshot
    {
        for (auto* k : knobs) if (k->id == paramId) k->show();
        window.setFocus (paramId, on);
    }

    void setParam (const char* id, float plain)
    {
        if (auto* prm = proc.apvts.getParameter (id))
        { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (plain)); prm->endChangeGesture(); }
    }
    float getParam (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    // ---- building blocks
    struct Text { String s; Rect r; float size; Colour c; juce::Justification j; bool bold; float track; bool onPedal; };
    void label (const String& s, Rect r, float size, Colour c, juce::Justification j = juce::Justification::centred, bool bold = true, float track = 0.14f)
    { texts.push_back ({ s, r, size, c, j, bold, track, r.getX() < kPedalX + kPedalW }); }   // on the pedal: printed in the skin's ink

    void applySkin (int index)
    {
        proc.uiSkin = juce::jlimit (0, kNumSkins - 1, index);
        currentSkin = &skinAt (proc.uiSkin);
        look.skin = currentSkin;
        window.setSkin (*currentSkin);
        bg = {};
        repaint();
        for (auto* c : getChildren()) c->repaint();
    }

    Knob* knob (const char* id, const String& name, int kind, float cx, float cy, Colour ac, juce::Array<juce::Component*>& group, int size = 0)
    {
        auto* k = knobs.add (new Knob (proc, *this, id, name, kind, ac));
        const int sz = size > 0 ? size : kind == 0 ? 124 : kind == 1 ? 78 : 64;
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
        if (String (id).isNotEmpty())   // the window explains the chip while the mouse is over it
            c->onHover = [this, c, pid = String (id)] (bool isOn)
            {
                if (isOn) window.setReadout (c->bound->param->getName (32), c->mode == Chip::toggle ? String (c->lit() ? "ON" : "OFF") : shortChoice (c->bound->text()));
                window.setFocus (pid, isOn);
            };
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
        skinC = chip (Chip::custom, "", "SKIN", col::amber, "Skin: the pedal's plate and colours (Sunburst, Lagoon, Midnight).", barOnly);
        skinC->textFn = [this] { return "SKIN " + String (skinAt (proc.uiSkin).name).toUpperCase(); };
        skinC->setSize (176, 22);
        skinC->onPress = [this] { skinMenu (skinC); };
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
            skinC->setTopLeftPosition (right - 196 - 10 - skinC->getWidth(), y);
            saveC->setTopLeftPosition (54 + lcdW + 44, y);
        }
    }

    // ---- pedal
    void buildPedal()
    {
        juce::Array<juce::Component*> g;   // the pedal's controls are always visible
        const float px = kPedalX, py = kPedalY;
        // laid out inside the plate's printed frame (x 20..373, y 14..625), as in the approved mockup
        // row 1: how the notes swell in, how long they ring, the tone
        const float bx[3] = { 84.f, 196.f, 308.f };
        const int bsz[3] = { 119, 84, 119 };   // ATTACK and TONE are the main knobs
        const char* bid[3] = { "attack", "sustain", "freq" }; const char* bn[3] = { "ATTACK", "SUSTAIN", "TONE" };
        for (int i = 0; i < 3; ++i)
        {
            knob (bid[i], bn[i], 0, px + bx[i], py + 276, col::amber, g, bsz[i]);
            label (bn[i], { px + bx[i] - 60, py + (i == 1 ? 306.f : 327.f), 120, 16 }, i == 1 ? 10.5f : 13.f, col::ink, juce::Justification::centred, true, 0.18f);
        }
        // row 2 in two printed groups: how the swell follows your playing | what comes out
        struct Grp { const char* title; float x, w; std::vector<std::pair<const char*, const char*>> ks; std::vector<float> xs; };
        const Grp groups[2] = { { "PLAYING", 30.f, 168.f, { { "touch", "FEEL" }, { "follow", "PACE" }, { "even", "EVEN" } }, { 60.f, 114.f, 168.f } },
                                { "OUTPUT", 226.f, 136.f, { { "dry", "DRY" }, { "vol", "LEVEL" } }, { 266.f, 324.f } } };
        for (auto& gr : groups)
        {
            pedalGroups.push_back ({ gr.title, Rect (px + gr.x, py + 360.f, gr.w, 76.f) });
            for (size_t i = 0; i < gr.ks.size(); ++i)
            {
                const float cx = px + gr.xs[i];
                knob (gr.ks[i].first, gr.ks[i].second, 1, cx, py + 390.f, col::amber, g, 78);
                label (gr.ks[i].second, { cx - 40.f, py + 415.f, 80.f, 13.f }, 10.f, col::ink, juce::Justification::centred, true, 0.16f);
            }
        }

        // the ON footswitch, centred, its LED to the left
        addAndMakeVisible (onLed);
        onLed.setBounds (juce::roundToInt (px + 128 - 18), juce::roundToInt (py + 574 - 18), 36, 36);   // room for the glow
        onLed.pedalSkin = &currentSkin;
        addAndMakeVisible (onSw);
        onSw.setBounds (juce::roundToInt (px + 196 - 36), juce::roundToInt (py + 584 - 36), 72, 72);
        onSw.setTooltip ("ON: switches the pedal in or out (bypass). Right-click: MIDI learn (a footswitch on your controller).");
        onSw.onMenu = [this] { midiMenu ("bypass", onSw); };
        onSw.onDown = [this] { setParam ("bypass", getParam ("bypass") > 0.5f ? 0.f : 1.f); };
        label ("ON", { px + 128 - 20, py + 592, 40, 14 }, 12.f, col::ink, juce::Justification::centred, true, 0.18f);
        // MATCH: a mini toggle mirroring the LED on the other side of the footswitch
        matchSw = std::make_unique<MiniToggle> (proc, "levelMatch");
        matchSw->setTooltip ("MATCH: each note's swell ends as loud as its pick (up = on, the default). Off: the note keeps the string's own fade, so looser playing stays quieter. "
                             "The POG2 has no such switch: its built-in gain always brings plucks back to about their pick level, which is MATCH on. Right-click: MIDI learn.");
        matchSw->onMenu = [this] { midiMenu ("levelMatch", *matchSw); };
        matchSw->setBounds (juce::roundToInt (px + 264 - 16), juce::roundToInt (py + 572 - 20), 32, 40);
        addAndMakeVisible (*matchSw);
        label ("MATCH", { px + 264 - 30, py + 592, 60, 14 }, 12.f, col::ink, juce::Justification::centred, true, 0.18f);
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
        auto rowY = [] (int mod, int row) { return moduleRect (mod).getY() + (mod < 2 ? 120.f : row == 0 ? 132.f : 162.f); };
        // a setting with its name above it (used in the boxed modules)
        auto named = [&] (juce::Component* c, const String& name, float cx, float y)
        {
            label (name, { cx - 40.f, y, 80.f, 11.f }, 9.f, col::dimText, juce::Justification::centred, true, 0.16f);
            c->setTopLeftPosition (juce::roundToInt (cx - (float) c->getWidth() * 0.5f), juce::roundToInt (y + 12.f));
        };
        auto rowX = [] (int mod) { return moduleRect (mod).getX() + 14.f; };

        for (int i = 0; i < 6; ++i)
        {
            if (i == 4) continue;   // unused slot (was HOLD)
            const auto r = moduleRect (i);
            Led* l = leds.add (new Led (proc, kMods[i].onId, false, String (kMods[i].title) + ": click to switch this module on or off."));
            l->setBounds (juce::roundToInt (r.getX() + 8), juce::roundToInt (r.getY() + 8), 26, 24);
            addAndMakeVisible (l); g.add (l);
            label (kMods[i].title, { r.getX() + 62, r.getY() + 10, 120, 20 }, 16.f, kMods[i].ac.interpolatedWith (col::cream, 0.4f), juce::Justification::centredLeft, true, 0.2f);
            const float tw = juce::GlyphArrangement::getStringWidth (uiFont (16.f, true, 0.2f), kMods[i].title);
            label (kMods[i].sub, { r.getX() + 72 + tw, r.getY() + 11, r.getWidth() - 84 - tw, 20 }, 12.5f, col::dimText, juce::Justification::centredLeft, false, 0.03f);
        }

        // FILTER
        knobRow (0, { { "mod", "MOD" }, { "res", "RES" }, { "pickFilter", "DYN" } });
        {
            bodyC = chip (Chip::custom, "", "BODY", kMods[0].ac, "BODY: an instrument's resonance on the sound - the VG-8 bowed tone curve, or a violin body. Part of FILTER (off with it).", g);
            bodyC->textFn = [this]
            {
                const int ir = juce::roundToInt (getParam ("bodyIr"));
                if (ir > 0) return proc.apvts.getParameter ("bodyIr")->getCurrentValueAsText().toUpperCase();
                return String (getParam ("bowedBody") > 0.5f ? "VG-8 TONE" : "OFF");
            };
            bodyC->litFn = [this] { return getParam ("bodyIr") > 0.5f || getParam ("bowedBody") > 0.5f; };
            bodyC->setSize (96, 22);
            bodyC->onPress = [this] { bodyMenu(); };
            bodyMixC = valueChip ("bodyMix", "MIX", 76, kMods[0].ac, g);   // only for a body IR (faded otherwise)
            label ("BODY", { rowX (0), rowY (0, 0), 40.f, 22.f }, 10.5f, col::chipText, juce::Justification::centredLeft, true, 0.2f);
            flow ({ bodyC, bodyMixC }, rowX (0) + 44.f, rowY (0, 0));
        }
        // FEEDBACK
        knobRow (1, { { "feedback", "FEEDBACK" }, { "fbDistance", "DISTANCE" }, { "fbRise", "RISE" }, { "fbTone", "TONE" } });
        flow ({ chip (Chip::toggle, "fbAmp", "AMP", kMods[1].ac, "AMP: the amp-in-the-room model. DISTANCE picks the harmonic, it takes over from the note, muting the string stops it.", g),
                chip (Chip::toggle, "fbOneWins", "ONE WINS", kMods[1].ac, "ONE WINS: in a chord the loudest note takes most of the feedback, as in a real amp.", g),
                chip (Chip::menu, "fbMode", "", kMods[1].ac, "Bloom: the feedback grows on held notes. Immediate: an overtone from the pick, like an octave pedal.", g) },
              rowX (1), rowY (1, 0));
        // MOTION: VIBRATO | TREMOLO on top (each knob with its settings beside it), SPREAD | DETUNE below
        {
            const auto r = moduleRect (2);
            const Colour ac = kMods[2].ac;
            const float bw = (r.getWidth() - 24.f) * 0.5f, x0 = r.getX() + 10.f, x1 = x0 + bw + 4.f;
            const float yA = r.getY() + 38.f, hA = 112.f, yB = yA + hA + 4.f, hB = r.getBottom() - 8.f - yB;
            const Rect boxes[4] = { { x0, yA, bw, hA }, { x1, yA, bw, hA }, { x0, yB, bw, hB }, { x1, yB, bw, hB } };
            const char* ids[4] = { "vibDepth", "tremolo", "spread", "detuneMix" }; const char* names[4] = { "VIBRATO", "TREMOLO", "SPREAD", "DETUNE" };
            for (int c = 0; c < 4; ++c)
            {
                const auto& bx = boxes[c];
                subBoxes.push_back ({ bx, ac });
                const float kcx = c == 3 ? bx.getCentreX() : bx.getX() + 32.f, kcy = bx.getY() + (c < 2 ? 40.f : 28.f);
                knob (ids[c], names[c], 2, kcx, kcy, ac, g, 50);
                label (names[c], { kcx - 40.f, kcy + 26.f, 80.f, 12.f }, 10.f, col::chipText, juce::Justification::centred, true, 0.12f);
            }
            auto valueOnly = [&] (const char* id) { return valueChip (id, "", 0, ac, g); };
            auto* vt = chip (Chip::custom, "", "VOICE", ac, "What the vibrato moves (voice, guitar or both). Lit: HUMAN, it starts shallow and slow, then settles.", g);
            vt->textFn = [this] { return shortChoice (proc.apvts.getParameter ("vibTarget")->getCurrentValueAsText()); };
            vt->litFn = [this] { return getParam ("vibHuman") > 0.5f; };
            vt->setSize (64, 22);
            vt->onHover = [this] (bool isOn) { window.setFocus ("vibDepth", isOn); };
            vt->onPress = [this, vt]
            {
                juce::PopupMenu m;
                m.addSectionHeader ("Vibrato on");
                const auto vn = proc.apvts.getParameter ("vibTarget")->getAllValueStrings();
                for (int i = 0; i < vn.size(); ++i) m.addItem (10 + i, vn[i], true, juce::roundToInt (getParam ("vibTarget")) == i);
                m.addSeparator();
                m.addItem (1, "Human feel", true, getParam ("vibHuman") > 0.5f);
                m.setLookAndFeel (&look);
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (vt), [this, vt] (int res)
                {
                    if (res == 1) setParam ("vibHuman", getParam ("vibHuman") > 0.5f ? 0.f : 1.f);
                    else if (res >= 10) setParam ("vibTarget", (float) (res - 10));
                    vt->repaint();
                });
            };
            const float sx0 = boxes[0].getRight() - 38.f, sx1 = boxes[1].getRight() - 38.f;
            named (valueOnly ("vibRate"), "RATE", sx0, yA + 3.f);
            named (valueOnly ("vibDelay"), "DELAY", sx0, yA + 37.f);
            named (vt, "ON", sx0, yA + 71.f);
            named (valueOnly ("tremRate"), "RATE", sx1, yA + 3.f);
            auto* shape = shapeSegs.add (new ShapeSeg (proc, "tremShape", ac));
            shape->setSize (66, 22); addAndMakeVisible (shape); g.add (shape);
            named (shape, "SHAPE", sx1, yA + 37.f);
            auto* mode = chip (Chip::menu, "spreadMode", "", ac, "How SPREAD places the notes.", g); mode->setSize (64, 22);
            named (mode, "PLACE", boxes[2].getRight() - 38.f, yB + 6.f);
        }
        // LAYER, across the full width: knobs, octave / MONO / UNISON (a fader) on the left; on the right a scope of the
        // voice's wave (TONE rounds it, UNISON spreads copies that drift in and out of phase) over the nine wave buttons
        {
            const auto r = moduleRect (3);
            const Colour ac = kMods[3].ac;
            const char* ids[4] = { "voiceMix", "voiceTone", "voiceGlide", "voiceCharacter" }; const char* names[4] = { "MIX", "TONE", "GLIDE", "CHARACTER" };
            for (int i = 0; i < 4; ++i)
            {
                const float cx = r.getX() + 14.f + 37.f + 76.f * (float) i;
                knob (ids[i], names[i], 2, cx, r.getY() + 74, ac, g);
                label (names[i], { cx - 45, r.getY() + 104, 90, 13 }, 11.f, col::chipText, juce::Justification::centred, true, 0.14f);
            }
            auto* oct = segs.add (new Seg (proc, "voiceOct", { { "-1", 0 }, { "0", 1 }, { "+1", 2 } }, ac));
            oct->setTooltip ("The voice's octave.");
            oct->setSize (oct->idealWidth(), 22); addAndMakeVisible (oct); g.add (oct);
            auto* mono = chip (Chip::toggle, "voiceMono", "MONO", ac, "Mono: only the newest note keeps its voice (for lead lines).", g);
            const float ry = r.getY() + 136.f;
            named (oct, "OCTAVE", r.getX() + 14.f + (float) oct->getWidth() * 0.5f, ry);
            named (mono, "PLAY", r.getX() + 14.f + (float) oct->getWidth() + 12.f + (float) mono->getWidth() * 0.5f, ry);
            auto* uni = knobs.add (new Knob (proc, *this, "voiceUnison", "UNISON", 4, ac));
            uni->setSize (150, 22); addAndMakeVisible (uni); g.add (uni);
            named (uni, "UNISON", r.getX() + 318.f - 75.f, ry);
            // the wave box
            const float wx = r.getX() + 336.f, ww = r.getRight() - 14.f - wx;
            subBoxes.push_back ({ Rect (wx, r.getY() + 38.f, ww, r.getHeight() - 46.f), ac });
            waveScope = std::make_unique<WaveScope> (proc, ac);
            waveScope->setBounds (juce::roundToInt (wx + 8.f), juce::roundToInt (r.getY() + 44.f), juce::roundToInt (ww - 16.f), 52);
            addAndMakeVisible (*waveScope); g.add (waveScope.get());
            waveGrid = std::make_unique<WaveGrid> (proc, ac);
            waveGrid->setBounds (juce::roundToInt (wx + 8.f), juce::roundToInt (r.getY() + 102.f), juce::roundToInt (ww - 16.f), juce::roundToInt (r.getHeight() - 116.f));
            addAndMakeVisible (*waveGrid); g.add (waveGrid.get());
        }
        // SPACE: three knobs; below them what feeds the reverb (IN) and how it stops (GATE), each in a box
        {
            const auto r = moduleRect (5);
            const Colour ac = kMods[5].ac;
            knobRow (5, { { "smearSize", "SIZE" }, { "smearLevel", "LEVEL" }, { "smearDrive", "DRIVE" } });
            const float bw = (r.getWidth() - 24.f) * 0.5f, by = r.getY() + 120.f, bh = r.getHeight() - 128.f;
            const float lx = r.getX() + 10.f, rx = lx + bw + 4.f;
            subBoxes.push_back ({ Rect (lx, by, bw, bh), ac });
            subBoxes.push_back ({ Rect (rx, by, bw, bh), ac });
            label ("IN", { lx + 8.f, by + 4.f, 40.f, 12.f }, 10.f, col::chipText, juce::Justification::centredLeft, true, 0.2f);
            label ("GATE", { rx + 8.f, by + 4.f, 60.f, 12.f }, 10.f, col::chipText, juce::Justification::centredLeft, true, 0.2f);
            auto* send = chip (Chip::menu, "smearSend", "", ac, "What feeds the reverb: always, following the swell, or an expression pedal.", g);
            send->setSize (juce::jmin (send->getWidth(), (int) bw - 16), 22);
            auto* thr = valueChip ("smearThresh", "", 0, ac, g);
            auto* dark = chip (Chip::toggle, "smearDark", "DARK", ac, "A darker reverb.", g);
            named (send, "SEND", lx + bw * 0.5f, by + 27.f);
            named (thr, "THRESHOLD", lx + bw * 0.5f, by + 61.f);
            dark->setTopLeftPosition (juce::roundToInt (lx + bw - 8.f - (float) dark->getWidth()), juce::roundToInt (by + 2.f));
            auto* gate = chip (Chip::toggle, "smearGate", "ON", ac, "Gate: the reverb is cut shortly after you stop playing.", g);
            gateHold = valueChip ("smearHold", "", 0, ac, g);
            gateCut  = valueChip ("smearCut", "", 0, ac, g);
            gate->setTopLeftPosition (juce::roundToInt (rx + bw - 8.f - (float) gate->getWidth()), juce::roundToInt (by + 2.f));
            named (gateHold, "HOLD", rx + bw * 0.5f, by + 27.f);
            named (gateCut, "CUT", rx + bw * 0.5f, by + 61.f);
        }
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
    }

    void presetMenu()
    {
        juce::PopupMenu m;
        for (auto* tag : { "Default", "Lead", "Pad", "Feedback", "Effect" })
        {
            m.addSectionHeader (tag);
            for (int i = 0; i < proc.getNumPrograms(); ++i)
                if (PolySwellProcessor::programTag (i) == tag)
                    m.addItem (100 + i, proc.getProgramName (i), true, ! proc.userPresetLoaded && i == proc.getCurrentProgram());
        }
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
            else if (r >= 1000) { proc.loadUserPreset (user[r - 1000]); }
            else if (r >= 100) { proc.setCurrentProgram (r - 100); }
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
        const bool vg = getParam ("bowedBody") > 0.5f, anyIr = getParam ("bodyIr") > 0.5f;
        m.addItem (3, "Off", true, ! vg && ! anyIr);
        m.addItem (1, "VG-8 bowed tone (an EQ curve)", true, vg && ! anyIr);
        m.addSectionHeader ("Violin body (an impulse response)");
        const auto names = proc.apvts.getParameter ("bodyIr")->getAllValueStrings();
        const int cur = juce::roundToInt (getParam ("bodyIr"));
        for (int i = 1; i < names.size(); ++i)
            if (i < 8 || proc.customIr.existsAsFile())
                m.addItem (10 + i, i == 8 ? "Your IR: " + proc.customIr.getFileNameWithoutExtension() : names[i], true, i == cur);
        m.addSeparator();
        m.addItem (2, "Load your own IR...");
        m.setLookAndFeel (&look);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (bodyC), [this] (int r)
        {
            if (r == 3) { setParam ("bowedBody", 0.f); setParam ("bodyIr", 0.f); }
            else if (r == 1) { setParam ("bowedBody", 1.f); setParam ("bodyIr", 0.f); }
            else if (r >= 10) { setParam ("bodyIr", (float) (r - 10)); setParam ("bowedBody", 0.f); }
            else if (r == 2)
            {
                chooser = std::make_unique<juce::FileChooser> ("Load an impulse response", proc.customIr.existsAsFile() ? proc.customIr : juce::File(), "*.wav;*.aif;*.aiff;*.flac");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                      [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) proc.setCustomIr (fc.getResult()); bodyC->repaint(); });
            }
            bodyC->repaint();
        });
    }

    void skinMenu (juce::Component* target)
    {
        juce::PopupMenu m;
        for (int i = 0; i < kNumSkins; ++i) m.addItem (1 + i, skinAt (i).name, true, proc.uiSkin == i);
        m.setLookAndFeel (&look);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [this] (int r) { if (r > 0) applySkin (r - 1); });
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
        juce::PopupMenu ccs;
        int mapped = 0;
        for (int i = 0; i < proc.getParameters().size(); ++i)
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (proc.getParameters()[i]))
                if (const int cc = proc.ccFor (rp->getParameterID()); cc >= 0)
                { ccs.addItem (200 + i, "CC " + String (cc) + ": " + rp->getName (30) + "  (click to forget)"); ++mapped; }
        if (mapped == 0) ccs.addItem (199, "None yet: right-click a knob or footswitch to learn one", false);
        m.addSubMenu ("MIDI controls (footswitch, expression pedal)", ccs);
        juce::PopupMenu fine, slopeM, ovM, maxM;
        const auto sn = proc.apvts.getParameter ("slope")->getAllValueStrings();
        for (int i = 0; i < sn.size(); ++i) slopeM.addItem (300 + i, sn[i], true, juce::roundToInt (getParam ("slope")) == i);
        const auto hn = proc.apvts.getParameter ("fbHarmonic")->getAllValueStrings();
        for (int i = 0; i < hn.size(); ++i) ovM.addItem (320 + i, hn[i], true, juce::roundToInt (getParam ("fbHarmonic")) == i);
        static const int maxes[5] = { 12, 18, 24, 30, 36 };
        for (int i = 0; i < 5; ++i) maxM.addItem (340 + i, String (maxes[i]) + " dB", true, std::abs (getParam ("sustainMax") - (float) maxes[i]) < 0.5f);
        fine.addSubMenu ("Filter slope", slopeM);
        fine.addSubMenu ("Feedback overtone (when AMP is off)", ovM);
        fine.addSubMenu ("The most SUSTAIN and FEEDBACK may lift a note", maxM);
        juce::PopupMenu detM;
        static const float cents[6] = { 5.f, 7.f, 10.1f, 15.f, 20.f, 25.f };
        for (int i = 0; i < 6; ++i)
            detM.addItem (360 + i, String (cents[i], cents[i] == 10.1f ? 1 : 0) + " cents" + (i == 2 ? " (POG2)" : ""), true, std::abs (getParam ("detune") - cents[i]) < 0.05f);
        fine.addSubMenu ("Detune amount", detM);
        m.addSubMenu ("Fine tuning", fine);
        m.addSectionHeader ("Window");
        const float sizes[] = { 0.75f, 1.f, 1.25f, 1.5f, 2.f };
        for (int i = 0; i < 5; ++i) size.addItem (40 + i, String (juce::roundToInt (sizes[i] * 100)) + " %", true, std::abs (proc.uiScale - sizes[i]) < 0.01f);
        m.addSubMenu ("Size", size);
        m.addItem (4, panelOpen ? "Hide the effects panel" : "Show the effects panel");
        juce::PopupMenu curveM;
        const char* curves[3] = { "Classic", "Bowed (slow, S-shaped start)", "POG2 (default)" };
        for (int i : { 2, 0, 1 }) curveM.addItem (390 + i, curves[i], true, juce::roundToInt (getParam ("swellShape")) == i);
        curveM.addSeparator();
        curveM.addItem (395, "MATCH: swells end as loud as the pick (instead of the POG2's gain)", true, getParam ("levelMatch") > 0.5f);
        m.addSubMenu ("Swell curve", curveM);
        juce::PopupMenu syncM;
        syncM.addItem (400, "Off", true, getParam ("sync") < 0.5f);
        {
            const auto names = proc.apvts.getParameter ("syncDiv")->getAllValueStrings();
            for (int i = 0; i < names.size(); ++i) syncM.addItem (410 + i, names[i], true, getParam ("sync") > 0.5f && juce::roundToInt (getParam ("syncDiv")) == i);
        }
        m.addSubMenu ("ATTACK tempo sync", syncM);
        juce::PopupMenu skinM;
        for (int i = 0; i < kNumSkins; ++i) skinM.addItem (380 + i, skinAt (i).name, true, proc.uiSkin == i);
        m.addSubMenu ("Skin", skinM);
        m.setLookAndFeel (&look);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&gearB), [this, sizes] (int r)
        {
            auto flip = [this] (const char* id) { setParam (id, getParam (id) > 0.5f ? 0.f : 1.f); };
            if (r >= 380 && r < 380 + kNumSkins) applySkin (r - 380);
            else if (r >= 390 && r < 393) setParam ("swellShape", (float) (r - 390));
            else if (r == 395) setParam ("levelMatch", getParam ("levelMatch") > 0.5f ? 0.f : 1.f);
            else if (r == 400) setParam ("sync", 0.f);
            else if (r >= 410 && r < 440) { setParam ("syncDiv", (float) (r - 410)); setParam ("sync", 1.f); }
            else if (r >= 360 && r < 366) setParam ("detune", cents[r - 360]);
            else if (r >= 340 && r < 345) setParam ("sustainMax", (float) maxes[r - 340]);
            else if (r >= 320 && r < 330) setParam ("fbHarmonic", (float) (r - 320));
            else if (r >= 300 && r < 310) setParam ("slope", (float) (r - 300));
            else if (r >= 200) { if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (proc.getParameters()[r - 200])) proc.forgetCc (rp->getParameterID()); }
            else if (r == 1) flip ("smoothRepick");
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
        const String num = proc.userPresetLoaded ? String ("USER") : PolySwellProcessor::programTag (proc.getCurrentProgram()).toUpperCase();
        if (num != lcd.right) { lcd.right = num; lcd.repaint(); }
        undoB.setEnabled (proc.undo.canUndo()); redoB.setEnabled (proc.undo.canRedo());
        int mask = 0;
        for (int i = 0; i < 6; ++i) if (kMods[i].onId != nullptr && getParam (kMods[i].onId) < 0.5f) mask |= 1 << i;
        if (getParam ("filterOn") < 0.5f) mask |= 64;
        if (getParam ("bypass") > 0.5f) mask |= 128;      // bypassed: the name goes dark
        if (mask != offMask) { offMask = mask; repaint(); }
        for (auto* c : chips) if (c->textFn || c->litFn) c->repaint();

        if (learnId.isNotEmpty())
        {
            if (proc.isLearning (learnId)) window.setReadout ("MIDI LEARN", "move a pedal or knob");
            else
            {
                if (const int cc = proc.ccFor (learnId); cc >= 0) window.setReadout (proc.apvts.getParameter (learnId)->getName (24), "ON CC " + String (cc));
                learnId = {};
            }
        }
        int act = 0;
        if (getParam ("filterOn") > 0.5f) act |= 1;
        if (getParam ("feedback") > 0.f && getParam ("feedbackOn") > 0.5f) act |= 2;
        if (getParam ("motionOn") > 0.5f && (getParam ("vibDepth") > 0.f || getParam ("tremolo") > 0.f || getParam ("spread") > 0.f || getParam ("detuneMix") > 0.f)) act |= 4;
        if (getParam ("voiceOn") > 0.5f && getParam ("voiceMix") > 0.f) act |= 8;
        if (getParam ("smearOn") > 0.5f) act |= 32;
        if (gateHold != nullptr) { const float a = getParam ("smearGate") > 0.5f ? 1.f : 0.4f; gateHold->setAlpha (a); gateCut->setAlpha (a); }
        if (bodyMixC != nullptr) bodyMixC->setAlpha (getParam ("bodyIr") > 0.5f ? 1.f : 0.35f);
        if (act != drawer.mask || drawer.open != panelOpen) { drawer.mask = act; drawer.open = panelOpen; drawer.repaint(); }
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

        // --- the pedal: the skin's plate (the owner's worn enclosure), the printed groups and the logo
        const Rect pr (kPedalX, kPedalY, kPedalW, kPedalH);
        const Skin& sk = currentSkin != nullptr ? *currentSkin : skinAt (0);
        juce::Path pedal; pedal.addRoundedRectangle (pr, 24.f);
        {
            Graphics::ScopedSaveState ss (g);   // shadow outside the plate only (JUCE 8's Direct2D drew it over the plate: grey)
            g.excludeClipRegion (pr.toNearestInt());
            juce::DropShadow (juce::Colours::black.withAlpha (0.8f), 30, { 0, 16 }).drawForPath (g, pedal);
            juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 6, { 0, 3 }).drawForPath (g, pedal);
        }
        {
            Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (pedal);
            g.setOpacity (1.f);
            g.drawImage (sk.plateImg(), pr, juce::RectanglePlacement::stretchToFit);
        }
        for (auto& gr : pedalGroups)
        {
            g.setColour (sk.frame2); g.drawRoundedRectangle (gr.second, gr.second.getHeight() * 0.5f, 1.5f);
            const auto f = uiFont (10.f, true, 0.18f);
            const float tw = juce::GlyphArrangement::getStringWidth (f, gr.first) + 18.f;
            const Rect tab (gr.second.getCentreX() - tw * 0.5f, gr.second.getY() - 7.f, tw, 14.f);
            g.setColour (sk.ink); g.fillRoundedRectangle (tab, 7.f);
            g.setFont (f); g.setColour (sk.body1); g.drawText (gr.first, tab.translated (1.5f, 0.f), juce::Justification::centred);
        }
        {
            const auto logo = sk.logoImg();   // visible part: x 8..1006, y 8..359 of 1012 x 440
            const float lw = 282.f, lh = lw * 351.f / 998.f, lcy = pr.getY() + (436.f + 552.f) * 0.5f, ly = lcy - lh * 0.5f;
            // 70s racing stripes in the skin's three colours, frame to frame through the footswitch row, faint so the
            // plate's wear shows through (the footswitch and its LED sit on them)
            const float sy = pr.getY() + 584.f - 11.f;
            for (int k = 0; k < 3; ++k)
            {
                const Rect st (pr.getX() + 24.f, sy + (float) k * 8.5f, kPedalW - 48.f, 5.5f);
                g.setColour (sk.rings[k].withAlpha (0.42f)); g.fillRect (st);
            }
            g.setOpacity (1.f);   // drawImage uses the current colour's alpha (the faint stripes' would fade the logo)
            g.drawImage (logo, juce::roundToInt (pr.getCentreX() - lw * 0.5f), juce::roundToInt (ly), juce::roundToInt (lw), juce::roundToInt (lh),
                         8, 8, 998, 351);
        }

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
                if (i == 4) continue;
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
                drawModIcon (g, i, Rect (r.getX() + 38.f, r.getY() + 11.f, 18.f, 18.f), ac.interpolatedWith (col::cream, 0.25f));
            }
            for (auto& sb : subBoxes)   // a control and the settings that belong to it share a box
            {
                g.setColour (sb.second.withAlpha (0.07f)); g.fillRoundedRectangle (sb.first, 6.f);
                g.setColour (sb.second.withAlpha (0.28f)); g.drawRoundedRectangle (sb.first, 6.f, 1.f);
            }
        }

        for (auto& t : texts)
        {
            if (! panelOpen && t.r.getX() >= kPanelX) continue;
            g.setFont (uiFont (t.size, t.bold, t.track));
            g.setColour (t.onPedal && currentSkin != nullptr ? currentSkin->ink : t.c);
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
        if (! bg.isValid() || std::abs (ps - bakedScale) > 0.001f) bake (ps);
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        g.drawImage (bg, getLocalBounds().toFloat());
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
            const Skin& sk = currentSkin != nullptr ? *currentSkin : skinAt (0);
            g.setColour (sk.ink.withAlpha (0.55f));
            g.fillEllipse (kPedalX + 308 - 38, kPedalY + 276 - 38, 76, 76);
            g.setColour (sk.body1);
            g.setFont (uiFont (10.f, true, 0.12f));
            g.drawText ("FILTER OFF", Rect (kPedalX + 308 - 60, kPedalY + 276 - 8, 120, 16), juce::Justification::centred);
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
    Chip* saveC = nullptr; Chip* skinC = nullptr; juce::Component* bodyMixC = nullptr;
    const Skin* currentSkin = nullptr; Chip* abC[2] { nullptr, nullptr }; Chip* bodyC = nullptr;
    Led onLed { proc, "bypass", true, "Lit while the pedal is on." };
    FootSwitch onSw;
    std::unique_ptr<MiniToggle> matchSw;
    DrawerTab drawer;
    std::vector<std::pair<Rect, Colour>> subBoxes;
    std::unique_ptr<juce::Component> waveScope, waveGrid;
    juce::OwnedArray<juce::Component> shapeSegs;
    Knob* gateHold = nullptr; Knob* gateCut = nullptr;
    std::vector<std::pair<String, Rect>> pedalGroups;
    String learnId;
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
    proc.editorOpen = true;
    board = std::make_unique<Board> (*this, p);
    addAndMakeVisible (*board);
    tooltips.setLookAndFeel (&board->look);
    applySize();
}

PolySwellEditor::~PolySwellEditor() { tooltips.setLookAndFeel (nullptr); proc.editorOpen = false; }

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

void PolySwellEditor::setSkin (int index) { board->applySkin (index); }

void PolySwellEditor::setUiScale (float scale)
{
    proc.uiScale = juce::jlimit (0.75f, 2.f, scale);
    applySize();
    board->bg = {};
    board->repaint();
}
