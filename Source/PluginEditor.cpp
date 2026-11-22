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

juce::Font uiFont (float h, bool bold = true, float track = 0.12f)
{
    return juce::Font (juce::FontOptions ("Bahnschrift", h, bold ? juce::Font::bold : juce::Font::plain))
             .withHorizontalScale (0.88f).withExtraKerningFactor (track);
}
juce::Font monoFont (float h) { return juce::Font (juce::FontOptions ("Consolas", h, juce::Font::plain)); }

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
    juce::Image plateImg() const { return juce::ImageCache::getFromMemory (plate, plateSize); }
    juce::Image logoImg() const  { return juce::ImageCache::getFromMemory (logo, logoSize); }
};
const Skin& skinAt (int i)
{
    static const Skin s[3] = {
        { "Sunburst", BinaryData::plate_sunburst_png, BinaryData::plate_sunburst_pngSize, BinaryData::logo_sunburst_png, BinaryData::logo_sunburst_pngSize,
          Colour (0xff2a231b), Colour (0xffefe5cc), Colour (0x592a231b), Colour (0xffd9732f), Colour (0xfffff4df), Colour (0xffffb030),
          { Colour (0xffe09a45), Colour (0xffd9732f), Colour (0xffbc4320) },
          Colour (0xff3a1a08), Colour (0xff0c0503), Colour (0xffff8a1f), Colour (0xffffb347), Colour (0xfffff1cf), Colour (0xffe89a45), Colour (0xffffc777) },
        { "Lagoon", BinaryData::plate_lagoon_png, BinaryData::plate_lagoon_pngSize, BinaryData::logo_lagoon_png, BinaryData::logo_lagoon_pngSize,
          Colour (0xff1f2433), Colour (0xffebe9de), Colour (0x541f2433), Colour (0xff3b86c9), Colour (0xfff2f6ff), Colour (0xff5fe8d6),
          { Colour (0xff4fc3b5), Colour (0xff3b86c9), Colour (0xff6a3aa0) },
          Colour (0xff082a2c), Colour (0xff03090b), Colour (0xff35bfd1), Colour (0xff6ff0dd), Colour (0xffe4fffa), Colour (0xff4fc9bb), Colour (0xff8ff5e6) },
        { "Midnight", BinaryData::plate_midnight_png, BinaryData::plate_midnight_pngSize, BinaryData::logo_midnight_png, BinaryData::logo_midnight_pngSize,
          Colour (0xffefe5cb), Colour (0xff23263a), Colour (0x47efe5cb), Colour (0xff4c8fe0), Colour (0xfff2f6ff), Colour (0xff7fe9dc),
          { Colour (0xff5ed6c5), Colour (0xff4c8fe0), Colour (0xff9248c2) },
          Colour (0xff1d1036), Colour (0xff06040b), Colour (0xff7a5cff), Colour (0xffb49bff), Colour (0xfff1eaff), Colour (0xff9a86e6), Colour (0xffcbb8ff) } };
    return s[juce::jlimit (0, 2, i)];
}

// ------------------------------------------------------------------------------------------------ geometry
constexpr float kPedalX = 22, kPedalY = 56, kPedalW = 392, kPedalH = 646;
constexpr float kPanelX = 432, kPanelY = 56, kPanelW = 622, kPanelH = 646;
constexpr float kModW = 293, kModH = 199.3f;

// panel: FILTER | FEEDBACK, MOTION across the full width, LAYER | SPACE (HOLD lives on the pedal, index 4 unused)
Rect moduleRect (int i)
{
    const float x0 = kPanelX + 12, y0 = kPanelY + 12;
    if (i == 4) return {};
    if (i == 2) return { x0, y0 + kModH + 12, kPanelW - 24, kModH };
    const float col = (i == 1 || i == 5) ? 1.f : 0.f, row = i < 2 ? 0.f : 2.f;
    return { x0 + col * (kModW + 12), y0 + row * (kModH + 12), kModW, kModH };
}

struct ModDef { const char* title; const char* sub; const char* onId; Colour ac; };
const ModDef kMods[6] = {
    { "FILTER",   "Sweeps each note",        "filterOn",   col::filter },
    { "FEEDBACK", "Amp in the room",         "feedbackOn", col::feedback },
    { "MOTION",   "Movement per note",       "motionOn",   col::motion },
    { "LAYER",    "Synth under each note",   "voiceOn",    col::layer },
    { "HOLD",     "Freeze into a pad",       nullptr,      col::hold },
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

    void drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        const int kind = (int) s.getProperties().getWithDefault ("kind", 2);
        const Colour ac ((juce::uint32) (juce::int64) s.getProperties().getWithDefault ("ac", (juce::int64) col::amber.getARGB()));
        const Rect b ((float) x, (float) y, (float) w, (float) h);
        if (kind == 3) { drawValueChip (g, b, pos, s, ac); return; }

        if (kind < 2 && skin != nullptr)   // the pedal's knobs: ivory, the logo's three rings around it filling with the value
        {
            const float bw = (float) w, c = b.getCentreX(), cy = b.getCentreY();
            const float rw = bw > 100.f ? 3.4f : bw > 60.f ? 2.6f : 2.f, gap = rw + 1.8f, pad = 6.f + 3.f * gap + 4.f;
            const float R = bw * 0.5f - pad, a = a0 + pos * (a1 - a0);
            const juce::PathStrokeType st (rw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
            for (int k = 0; k < 3; ++k)
            {
                const float r = R + 5.f + (float) (2 - k) * gap;
                juce::Path track; track.addCentredArc (c, cy, r, r, 0.f, a0, a1, true);
                g.setColour (skin->rings[k].withAlpha (0.22f)); g.strokePath (track, st);
                if (a > a0 + 0.01f)
                {
                    juce::Path lit; lit.addCentredArc (c, cy, r, r, 0.f, a0, a, true);
                    g.setColour (skin->rings[k]); g.strokePath (lit, st);
                }
            }
            juce::Path face; face.addEllipse (c - R, cy - R, 2 * R, 2 * R);
            juce::DropShadow (juce::Colours::black.withAlpha (0.45f), juce::jmax (2, juce::roundToInt (R * 0.18f)), { 0, juce::jmax (1, juce::roundToInt (R * 0.09f)) }).drawForPath (g, face);
            juce::ColourGradient fg (Colour (0xfffffaf0), c - R * 0.24f, cy - R * 0.4f, Colour (0xffc9bc9d), c + R * 0.6f, cy + R * 0.9f, true);
            fg.addColour (0.55, Colour (0xffece2cb));
            g.setGradientFill (fg); g.fillPath (face);
            g.setColour (juce::Colours::black.withAlpha (0.28f)); g.strokePath (face, juce::PathStrokeType (1.f));
            g.setColour (Colour (0x1f3c2814)); g.drawEllipse (c - R * 0.8f, cy - R * 0.8f, R * 1.6f, R * 1.6f, 1.2f);
            const float sn = std::sin (a), cs = -std::cos (a);
            g.setColour (Colour (0xff2a231b));
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
        setSliderStyle (kind == 3 ? juce::Slider::RotaryVerticalDrag : juce::Slider::RotaryHorizontalVerticalDrag);
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
        if ((int) getProperties().getWithDefault ("kind", 2) == 3) return juce::Component::hitTest (x, y);
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

    explicit SignalWindow (PolySwellProcessor& p) : proc (p) { setInterceptsMouseClicks (false, false); startTimerHz (30); }

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
            { "detune", 6 }, { "detuneWobble", 6 }, { "detuneMix", 6 },
            { "voiceMix", 7 }, { "voiceTone", 7 }, { "voiceWave", 7 }, { "voiceOct", 7 }, { "voiceUnison", 7 }, { "voiceMono", 7 },
            { "voiceGlide", 7 }, { "voiceCharacter", 7 },
            { "holdFade", 8 }, { "holdLevel", 8 }, { "holdLatch", 8 },
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
        // the guitar's level at the pick (loudest of the first 4 frames): the projected swell starts from it
        const float inLin = std::sqrt ((float) ei);
        if (c.pick) { inPeakNote = inLin; pickCols = 0; }
        else if (++pickCols < 4) inPeakNote = juce::jmax (inPeakNote, inLin);

        // HOLD: remember which notes were ringing when the pad was frozen
        const float hl = proc.holdLevelNow.load (std::memory_order_relaxed);
        if (hl > 0.f && lastHold <= 0.f)
        {
            heldN = 0; heldAge = 0;
            const Col& now = col (0);
            for (int j = 0; j < now.n; ++j) heldMidi[heldN++] = now.midi[j];
        }
        heldAge = juce::jmin (heldAge + 1, kCols);

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
        if (quiet < kCols + 2 || roTicks > 0 || hadReadout || hl != lastHold || byp != lastByp || viewA > 0.f || before > 0.f || paramsChanged())
            repaint();
        hadReadout = roTicks > 0; lastHold = hl; lastByp = byp;
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
    static float swellGain (float ph, bool bowed)
    {
        ph = juce::jlimit (0.f, 1.f, ph);
        if (! bowed) return ph * ph;
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
        return pogGain (attack) * std::pow (10.f, (-8.f * t + lift) / 20.f) * swellGain (attack < 0.002f ? 1.f : t / attack, juce::roundToInt (P ("swellShape")));
    }
    // the POG2 shape's own make-up gain (as the engine; none with MATCH)
    float pogGain (float attack) const
    {
        if (juce::roundToInt (P ("swellShape")) != 2 || P ("levelMatch") > 0.5f || attack < 0.002f) return 1.f;
        const float tau = juce::jmax (attack / 3.f, 1.0e-3f);
        return std::pow (10.f, juce::jlimit (0.f, 13.f, 5.75f + 4.83f * (std::log10 (tau) + 0.745f)) / 20.f);
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
            const Plot pl = timeGrid (g, b);
            for (int i = 0; i < 2; ++i)
            {
                const float o = on[i], av = amp[i], lev = av + (0.72f - av) * e;
                dashed (g, curve (pl, o, [=] (float t) { return av * natural (t - o); }), guitarC.withAlpha (0.55f));
                glowLine (g, curve (pl, o, [&] (float t) { return lev * swelled (t - o, A); }), amberC, &pl);
                tag (g, i == 0 ? "SOFT PICK" : "HARD PICK", pl.x (o) + 22.f, pl.base - pl.h + 16.f, cText.withAlpha (0xcc / 255.f));
            }
            return e > 0.01f ? "soft and hard picks come out closer in level" : "off: each note keeps its own level";
        }

        // the plain swell against the guitar (no zoom: the level the swell loses is the point), plus what the knob adds
        const Plot pl = timeGrid (g, b);
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
        const float d = P ("detune"), wob = P ("detuneWobble");
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

    // ---------------------------------------------------------------- HOLD and SPACE
    String paintHold (Graphics& g, Rect b)
    {
        const Plot pl = timeGrid (g, b);
        const Colour violet (0xffc4a6ec);
        const float fade = juce::jmax (0.02f, P ("holdFade")), lvl = P ("holdLevel"), tp = 0.6f, tr = 2.7f;
        const bool latch = on ("holdLatch");
        const float target = juce::jmin (1.f, lvl * 1.25f * natural (tp));
        dashed (g, curve (pl, 0.f, [] (float t) { return natural (t); }), Colour (0xffd8c3a2).withAlpha (0.55f));
        glowLine (g, curve (pl, tp, [&] (float t)
        {
            const float up = juce::jlimit (0.f, 1.f, (t - tp) / fade), down = t > tr ? 1.f - juce::jlimit (0.f, 1.f, (t - tr) / fade) : 1.f;
            return target * juce::jmin (up, down);
        }), violet, &pl);
        markAt (g, pl, tp, violet, "PRESS");
        markAt (g, pl, tr, violet, latch ? "PRESS AGAIN" : "LET GO");
        if (focusId == "holdLatch") return latch ? "press to freeze, press again to let go" : "the pad holds only while the footswitch is down";
        if (focusId == "holdLevel") return "the frozen pad at " + String (juce::roundToInt (lvl * 100.f)) + " %";
        return "the pad fades in and out over " + tidyValue (String (fade), "s");
    }

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
        const Rect lane (8.f, 14.f, b.getWidth() - 16.f, 76.f), env (8.f, 96.f, b.getWidth() - 16.f, 22.f);
        const float nowX = std::round (b.getWidth() * kNowFrac), dx = (nowX - lane.getX()) / (float) (kCols - 1);
        auto X = [&] (int age) { return nowX - (float) age * dx; };
        const int fut = (int) ((lane.getRight() - nowX) / dx);
        const Colour amberC (cAmber), pale (cPale), teal (0xff7fd1bd), rust (0xffff8a6a), blue (0xff8fb8e8), violet (0xffc4a6ec),
                     sage (0xffb5d38c), cream (0xffd8c3a2);

        // the open E strings as a faint grid
        g.setFont (monoFont (9.5f));
        for (int m : { 40, 52, 64, 76 })
        {
            const float y = yOfHz (hzOf ((float) m), lane);
            g.setColour (cGlow.withAlpha (0x14 / 255.f)); g.fillRect (lane.getX(), y, lane.getWidth(), 1.f);
            g.setColour (cText.withAlpha (0x66 / 255.f)); g.drawText (noteName ((float) m), Rect (lane.getRight() - 24.f, y - 10.f, 22.f, 10.f), juce::Justification::centredRight);
        }

        // HOLD: the frozen notes as steady violet bands from the press on
        const float hl = lastHold;
        if (hl > 0.f)
            for (int j = 0; j < heldN; ++j)
            {
                const float y = yOfHz (hzOf (heldMidi[j]), lane), xa = juce::jmax (lane.getX(), X (heldAge));
                g.setColour (violet.withAlpha (0.16f * hl)); g.fillRect (xa, y - 4.f, lane.getRight() - xa, 8.f);
                g.setColour (violet.withAlpha (0.8f * hl)); g.fillRect (xa, y - 0.75f, lane.getRight() - xa, 1.5f);
            }

        // FILTER: a soft veil over everything above the cutoff (no line); the notes under it dim (below)
        const bool filterOn = on ("filterOn");
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
            const float cNow = cutoffAt (noteAge), yc = yOfHz (cNow, lane);
            juce::Path tri; tri.addTriangle (lane.getRight(), yc - 4.f, lane.getRight(), yc + 4.f, lane.getRight() - 6.f, yc);
            g.setColour (teal.withAlpha (0.8f)); g.fillPath (tri);
            tag (g, tidyValue (String (cNow), "Hz").toUpperCase(), lane.getRight() - 8.f, juce::jmax (yc - 8.f, lane.getY() + 9.f), teal.withAlpha (0.8f), juce::Justification::centredRight);
        }

        // the notes
        const bool fbOn = feedbackOn(), amp = on ("fbAmp") && P ("fbMode") < 0.5f;
        float rA, rB, wA, wB; fbLadder (rA, rB, wA, wB);
        const float take = (fbOn && amp) ? 0.65f * ((rA == 1.f ? 0.f : wA) + wB) : 0.f;
        const bool layerOn = on ("voiceOn") && on ("voiceMixOn") && P ("voiceMix") > 0.f, spaceOn = on ("smearOn");
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

        // "now"
        for (float y = lane.getY() - 4.f; y < env.getBottom(); y += 5.f) { g.setColour (cTextHi.withAlpha (0x40 / 255.f)); g.fillRect (nowX, y, 1.f, 2.5f); }

        // the output level against the guitar's own; the latest note's swell continues dashed past "now"
        auto yDb = [&] (float db) { return env.getBottom() - juce::jlimit (0.f, 1.f, (db + 60.f) / 54.f) * env.getHeight(); };
        g.setColour (cGlow.withAlpha (0x1a / 255.f)); g.fillRect (env.getX(), env.getBottom(), env.getWidth(), 1.f);
        juce::Path outP, inP;
        for (int age = kCols - 1; age >= 0; --age)
        {
            const Col& c = col (age);
            if (age == kCols - 1) { outP.startNewSubPath (X (age), env.getBottom()); inP.startNewSubPath (X (age), yDb (c.inDb)); }
            else inP.lineTo (X (age), yDb (c.inDb));
            outP.lineTo (X (age), yDb (c.outDb));
        }
        outP.lineTo (nowX, env.getBottom()); outP.closeSubPath();
        g.setGradientFill (juce::ColourGradient (amberC.withAlpha (0.55f), 0.f, env.getY(), amberC.withAlpha (0.06f), 0.f, env.getBottom(), false));
        g.fillPath (outP);
        dashed (g, inP, cream.withAlpha (0.6f), 1.f);
        if (noteAge >= 0.f && noteAge < kT && inPeakNote > 1.0e-5f)
        {
            const float A = noteAttack > 0.f ? noteAttack : P ("attack") * 0.001f;
            juce::Path pr;
            for (int k = 0; k <= fut; ++k)
            {
                const float t = noteAge + (float) k * kColSec, y = yDb (20.f * std::log10 (inPeakNote * swelled (t, A) + 1.0e-9f));
                if (k == 0) pr.startNewSubPath (nowX, y); else pr.lineTo (nowX + (float) k * dx, y);
            }
            dashed (g, pr, amberC.withAlpha (0.85f), 1.3f);
        }
        for (int age = 0; age < kCols; ++age)
            if (col (age).pick) { g.setColour (amberC.withAlpha (0.8f)); g.fillRect (X (age), env.getBottom() - 4.f, 1.f, 5.f); }
        tag (g, "OUT", env.getX() + 2.f, env.getY() + 3.f, amberC.withAlpha (0.85f));
        tag (g, "GUITAR", env.getX() + 28.f, env.getY() + 3.f, cream.withAlpha (0.75f));

        // what is shaping the sound: lit when in use
        struct Item { const char* s; Colour c; bool lit; };
        const Item items[] = { { "SWELL", amberC, ! lastByp }, { "FILTER", teal, filterOn }, { "FEEDBACK", rust, fbOn },
                               { "LAYER", blue, layerOn }, { "HOLD", violet, hl > 0.f }, { "SPACE", sage, spaceOn } };
        const auto lf = uiFont (9.f, true, 0.06f);
        g.setFont (lf);
        float tw = 0.f;
        for (auto& it : items) tw += juce::GlyphArrangement::getStringWidth (lf, it.s) + 8.f;
        const float gapL = juce::jmax (4.f, (b.getWidth() - 20.f - tw) / 5.f);   // spread across the glass
        float lx = 10.f; const float ly = b.getHeight() - 11.f;
        for (auto& it : items)
        {
            const float a = it.lit ? 1.f : 0.28f;
            g.setColour (it.c.withAlpha (a)); g.fillEllipse (lx, ly - 2.5f, 5.f, 5.f);
            const float w = juce::GlyphArrangement::getStringWidth (lf, it.s);
            g.drawText (it.s, Rect (lx + 8.f, ly - 6.f, w + 4.f, 12.f), juce::Justification::centredLeft);
            lx += w + 8.f + gapL;
        }

        // input level, with the zone where the tracking works best
        const float mx1 = b.getWidth() - 12.f, mx0 = mx1 - 58.f, my = 9.f;
        auto mX = [&] (float db) { return mx0 + juce::jlimit (0.f, 1.f, (db + 60.f) / 60.f) * (mx1 - mx0); };
        g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillRoundedRectangle (mx0 - 1.f, my - 1.f, mx1 - mx0 + 2.f, 6.f, 2.f);
        g.setColour (pale.withAlpha (0.16f)); g.fillRect (mX (-24.f), my, mX (-6.f) - mX (-24.f), 4.f);
        g.setColour (meterDb > -3.f ? rust : meterDb < -30.f ? amberC.withAlpha (0.55f) : amberC);
        if (meterDb > -60.f) g.fillRect (mx0, my, mX (meterDb) - mx0, 4.f);
        if (peakDb > -60.f) { g.setColour (pale); g.fillRect (mX (peakDb) - 0.5f, my - 1.f, 1.5f, 6.f); }
        tag (g, peakDb > -3.f ? "HOT" : "IN", mx0 - 4.f, my + 2.f, (peakDb > -3.f ? rust : cText.withAlpha (0x99 / 255.f)), juce::Justification::centredRight);
    }

    void paint (Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        juce::Path clip; clip.addRoundedRectangle (b, 5.f);
        g.reduceClipRegion (clip);
        g.setGradientFill (juce::ColourGradient (cGlass1, b.getCentreX(), b.getHeight() * 0.55f, cGlass2, 0.f, 0.f, true));
        g.fillAll();
        if (lastHold > 0.f) { g.setColour (Colour (0xffc4a6ec).withAlpha (0.05f * lastHold)); g.fillAll(); }

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
                case 8:  says = paintHold (g, b); tc = Colour (0xffd2bcf2); break;
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
    int head = 0, idle = 0, quiet = 0, roTicks = 0, linger = 0, heldN = 0, heldAge = 0, peakHoldT = 0, pickCols = 99;
    float heldMidi[kMaxN] {};
    uint32_t lastFrame = 0, lastSeq = 0;
    float lastHold = 0.f, viewA = 0.f, noteAge = -1.f, noteAttack = 0.f, paramSum = 0.f, lvlRef = 1.0e-6f,
          meterDb = -100.f, peakDb = -100.f, inPeakNote = 0.f;
    bool lastByp = false, hadReadout = false, focusHeld = false;
    String roName, roValue, focusId;
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
        window.setBounds (juce::roundToInt (kPedalX + 43), juce::roundToInt (kPedalY + 35), 309, 143);   // in the plate's window glass

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
        if (id == "holdOn") { m.addSeparator(); m.addItem (10, "Latch: press to freeze, press again to let go", true, getParam ("holdLatch") > 0.5f); }
        m.setLookAndFeel (&look);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [this, id] (int r)
        {
            auto* p = proc.apvts.getParameter (id);
            if (r == 1) { proc.learnCc (id); learnId = id; }
            else if (r == 2) { proc.stopLearning(); learnId = {}; }
            else if (r == 3) proc.forgetCc (id);
            else if (r == 4 && p != nullptr) { p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture(); }
            else if (r == 10) setParam ("holdLatch", getParam ("holdLatch") > 0.5f ? 0.f : 1.f);
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
        proc.uiSkin = juce::jlimit (0, 2, index);
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
        skinC->setSize (128, 22);
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
        const int bsz[3] = { 119, 100, 119 };
        const char* bid[3] = { "attack", "sustain", "freq" }; const char* bn[3] = { "ATTACK", "SUSTAIN", "TONE" };
        for (int i = 0; i < 3; ++i)
        {
            knob (bid[i], bn[i], 0, px + bx[i], py + 266, col::amber, g, bsz[i]);
            label (bn[i], { px + bx[i] - 60, py + (i == 1 ? 311.f : 321.f), 120, 16 }, i == 1 ? 12.f : 13.f, col::ink, juce::Justification::centred, true, 0.18f);
        }
        // row 2 in two printed groups: how the swell follows your playing | what comes out
        struct Grp { const char* title; float x, w; std::vector<std::pair<const char*, const char*>> ks; std::vector<float> xs; };
        const Grp groups[2] = { { "PLAYING", 30.f, 168.f, { { "touch", "FEEL" }, { "follow", "PACE" }, { "even", "EVEN" } }, { 60.f, 114.f, 168.f } },
                                { "OUTPUT", 226.f, 136.f, { { "dry", "DRY" }, { "vol", "LEVEL" } }, { 266.f, 324.f } } };
        for (auto& gr : groups)
        {
            pedalGroups.push_back ({ gr.title, Rect (px + gr.x, py + 356.f, gr.w, 80.f) });
            for (size_t i = 0; i < gr.ks.size(); ++i)
            {
                const float cx = px + gr.xs[i];
                knob (gr.ks[i].first, gr.ks[i].second, 1, cx, py + 388.f, col::amber, g, 78);
                label (gr.ks[i].second, { cx - 40.f, py + 422.f, 80.f, 13.f }, 10.f, col::ink, juce::Justification::centred, true, 0.16f);
            }
        }

        // the swell's own options, in one row under the window
        auto* shape = chip (Chip::menu, "swellShape", "", col::amber, "Swell shape: Classic (the pedal's curve) or Bowed (a slower, S-shaped start).", g);
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
        auto* match = chip (Chip::toggle, "levelMatch", "MATCH", col::amber,
                            "MATCH: a long swell ends as loud as the pick was. Off: slow swells come out quieter, as on the pedal.", g);
        const float total = (float) (shape->getWidth() + match->getWidth() + sync->getWidth()) + 16.f;
        flow ({ shape, match, sync }, px + (kPedalW - total) * 0.5f, py + 192, 8.f);
        for (auto* c : { shape, match, sync }) { c->pedalSkin = &currentSkin; c->setSize (c->getWidth() + 6, 22); }

        addAndMakeVisible (onLed); addAndMakeVisible (holdLed);
        onLed.setBounds (juce::roundToInt (px + 97), juce::roundToInt (py + 556), 28, 22);
        holdLed.setBounds (juce::roundToInt (px + 257), juce::roundToInt (py + 556), 28, 22);
        onLed.pedalSkin = &currentSkin; holdLed.pedalSkin = &currentSkin;
        addAndMakeVisible (onSw); addAndMakeVisible (holdSw);
        onSw.setBounds (juce::roundToInt (px + 33), juce::roundToInt (py + 550), 70, 70);
        holdSw.setBounds (juce::roundToInt (px + 289), juce::roundToInt (py + 550), 70, 70);
        onSw.setTooltip ("ON: switches the pedal in or out (bypass). Right-click: MIDI learn (a footswitch on your controller).");
        holdSw.setTooltip ("HOLD: freezes what is ringing into a pad you can play over. Right-click: MIDI learn.");
        onSw.onMenu = [this] { midiMenu ("bypass", onSw); };
        // HOLD's own two settings, small, right beside its footswitch (LATCH: right-click the footswitch)
        knob ("holdFade", "FADE", 1, px + 166.f, py + 580.f, col::amber, g, 64);
        knob ("holdLevel", "LEVEL", 1, px + 214.f, py + 580.f, col::amber, g, 64);
        label ("FADE", { px + 141.f, py + 607.f, 50.f, 12.f }, 9.f, col::ink, juce::Justification::centred, true, 0.14f);
        label ("LEVEL", { px + 189.f, py + 607.f, 50.f, 12.f }, 9.f, col::ink, juce::Justification::centred, true, 0.14f);
        holdSw.onMenu = [this] { midiMenu ("holdOn", holdSw); };
        onSw.onDown = [this] { setParam ("bypass", getParam ("bypass") > 0.5f ? 0.f : 1.f); };
        holdSw.onDown = [this] { setParam ("holdOn", getParam ("holdLatch") > 0.5f ? (getParam ("holdOn") > 0.5f ? 0.f : 1.f) : 1.f); };
        holdSw.onUp = [this] { if (getParam ("holdLatch") < 0.5f) setParam ("holdOn", 0.f); };
        label ("ON", { px + 104, py + 576, 40, 14 }, 12.f, col::ink, juce::Justification::centredLeft, true, 0.18f);
        label ("HOLD", { px + 247, py + 576, 46, 14 }, 12.f, col::ink, juce::Justification::centredLeft, true, 0.18f);
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
            if (i == 4) continue;   // HOLD is on the pedal
            const auto r = moduleRect (i);
            Led* l = leds.add (new Led (proc, kMods[i].onId != nullptr ? kMods[i].onId : "holdOn", false,
                                        String (kMods[i].title) + (i == 4 ? ": lit while a pad is held." : ": click to switch this module on or off.")));
            l->setBounds (juce::roundToInt (r.getX() + 8), juce::roundToInt (r.getY() + 8), 26, 24);
            addAndMakeVisible (l); g.add (l);
            label (kMods[i].title, { r.getX() + 62, r.getY() + 10, 120, 20 }, 16.f, kMods[i].ac.interpolatedWith (col::cream, 0.4f), juce::Justification::centredLeft, true, 0.2f);
            const float tw = juce::GlyphArrangement::getStringWidth (uiFont (16.f, true, 0.2f), kMods[i].title);
            label (kMods[i].sub, { r.getX() + 72 + tw, r.getY() + 11, r.getWidth() - 84 - tw, 20 }, 12.5f, col::dimText, juce::Justification::centredLeft, false, 0.03f);
        }

        // FILTER
        knobRow (0, { { "mod", "MOD" }, { "res", "RES" }, { "pickFilter", "DYN" } });
        {
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
            flow ({ bodyC, mix }, rowX (0), rowY (0, 0));
        }
        // FEEDBACK
        knobRow (1, { { "feedback", "FEEDBACK" }, { "fbDistance", "DISTANCE" }, { "fbRise", "RISE" }, { "fbTone", "TONE" } });
        flow ({ chip (Chip::toggle, "fbAmp", "AMP", kMods[1].ac, "AMP: the amp-in-the-room model. DISTANCE picks the harmonic, it takes over from the note, muting the string stops it.", g),
                chip (Chip::toggle, "fbOneWins", "ONE WINS", kMods[1].ac, "ONE WINS: in a chord the loudest note takes most of the feedback, as in a real amp.", g),
                chip (Chip::menu, "fbMode", "", kMods[1].ac, "Bloom: the feedback grows on held notes. Immediate: an overtone from the pick, like an octave pedal.", g) },
              rowX (1), rowY (1, 0));
        // MOTION, across the full width: each knob in its own box with its own settings under it
        {
            const auto r = moduleRect (2);
            const Colour ac = kMods[2].ac;
            const char* ids[4] = { "vibDepth", "tremolo", "spread", "detuneMix" }; const char* names[4] = { "VIBRATO", "TREMOLO", "SPREAD", "DETUNE" };
            const float colW = (r.getWidth() - 28.f) / 4.f;
            for (int c = 0; c < 4; ++c)
            {
                const float cx = r.getX() + 14.f + colW * ((float) c + 0.5f);
                subBoxes.push_back ({ Rect (cx - colW * 0.5f + 4.f, r.getY() + 38.f, colW - 8.f, r.getHeight() - 40.f), ac });
                knob (ids[c], names[c], 2, cx, r.getY() + 74.f, ac, g);
                label (names[c], { cx - 45, r.getY() + 105.f, 90, 13 }, 11.f, col::chipText, juce::Justification::centred, true, 0.14f);
                std::vector<juce::Component*> stack;
                if (c == 0)
                {
                    // what the vibrato moves, and its feel, in one chip
                    auto* vt = chip (Chip::custom, "", "VIB ON", ac, "What the vibrato moves (voice, guitar or both), and HUMAN: it starts shallow and slow, then settles.", g);
                    vt->textFn = [this] { return shortChoice (proc.apvts.getParameter ("vibTarget")->getCurrentValueAsText()) + (getParam ("vibHuman") > 0.5f ? " HUMAN" : ""); };
                    vt->setSize (112, 22);
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
                    stack = { valueChip ("vibRate", "RATE", 0, ac, g), valueChip ("vibDelay", "DELAY", 0, ac, g), vt };
                }
                else if (c == 1) stack = { valueChip ("tremRate", "RATE", 0, ac, g), chip (Chip::menu, "tremShape", "", ac, "Tremolo shape.", g) };
                else if (c == 2) stack = { chip (Chip::menu, "spreadMode", "", ac, "How SPREAD places the notes.", g) };
                else stack = { valueChip ("detuneWobble", "WOBBLE", 0, ac, g) };
                float y = r.getY() + 123.f;
                for (auto* comp : stack) { comp->setTopLeftPosition (juce::roundToInt (cx - (float) comp->getWidth() * 0.5f), juce::roundToInt (y)); y += 25.f; }
            }
        }
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
        // SPACE
        {
            const Colour ac = kMods[5].ac;
            knobRow (5, { { "smearSize", "SIZE" }, { "smearLevel", "LEVEL" }, { "smearDrive", "DRIVE" } });
            flow ({ chip (Chip::menu, "smearSend", "SEND", ac, "What feeds the reverb: always, following the swell, or an expression pedal.", g),
                    chip (Chip::toggle, "smearDark", "DARK", ac, "A darker reverb.", g),
                    valueChip ("smearThresh", "THR", 0, ac, g) }, rowX (5), rowY (5, 0));
            // the gate and its two settings share a box; the settings fade while the gate is off
            auto* gate = chip (Chip::toggle, "smearGate", "GATE", ac, "Gate: the reverb is cut shortly after you stop playing.", g);
            gateHold = valueChip ("smearHold", "HOLD", 0, ac, g);
            gateCut  = valueChip ("smearCut", "CUT", 0, ac, g);
            flow ({ gate, gateHold, gateCut }, rowX (5) + 6.f, rowY (5, 1));
            subBoxes.push_back ({ Rect (rowX (5), rowY (5, 1) - 4.f, (float) gateCut->getRight() - rowX (5) + 6.f, 30.f), ac });
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
        setParam ("holdOn", 0.f);
    }

    void presetMenu()
    {
        juce::PopupMenu m;
        for (auto* tag : { "Lead", "Pad", "Feedback", "Classic" })
        {
            m.addSectionHeader (String (tag) == "Classic" ? String ("Classic (from PolySwell 1)") : String (tag));
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

    void skinMenu (juce::Component* target)
    {
        juce::PopupMenu m;
        for (int i = 0; i < 3; ++i) m.addItem (1 + i, skinAt (i).name, true, proc.uiSkin == i);
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
        juce::PopupMenu skinM;
        for (int i = 0; i < 3; ++i) skinM.addItem (380 + i, skinAt (i).name, true, proc.uiSkin == i);
        m.addSubMenu ("Skin", skinM);
        m.setLookAndFeel (&look);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&gearB), [this, sizes] (int r)
        {
            auto flip = [this] (const char* id) { setParam (id, getParam (id) > 0.5f ? 0.f : 1.f); };
            if (r >= 380 && r < 383) applySkin (r - 380);
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
        if (getParam ("holdOn") > 0.5f) act |= 16;
        if (getParam ("smearOn") > 0.5f) act |= 32;
        if (gateHold != nullptr) { const float a = getParam ("smearGate") > 0.5f ? 1.f : 0.4f; gateHold->setAlpha (a); gateCut->setAlpha (a); }
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
        juce::DropShadow (juce::Colours::black.withAlpha (0.8f), 30, { 0, 16 }).drawForPath (g, pedal);
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 6, { 0, 3 }).drawForPath (g, pedal);
        {
            Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (pedal);
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
        g.drawImage (sk.logoImg(), Rect (pr.getX() + 71, pr.getY() + 442, 250, 109), juce::RectanglePlacement::centred);

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
            g.fillEllipse (kPedalX + 308 - 38, kPedalY + 266 - 38, 76, 76);
            g.setColour (sk.body1);
            g.setFont (uiFont (10.f, true, 0.12f));
            g.drawText ("FILTER OFF", Rect (kPedalX + 308 - 60, kPedalY + 266 - 8, 120, 16), juce::Justification::centred);
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
    Chip* saveC = nullptr; Chip* skinC = nullptr;
    const Skin* currentSkin = nullptr; Chip* abC[2] { nullptr, nullptr }; Chip* bodyC = nullptr;
    Led onLed { proc, "bypass", true, "Lit while the pedal is on." }, holdLed { proc, "holdOn", false, "Lit while a pad is held." };
    FootSwitch onSw, holdSw;
    DrawerTab drawer;
    std::vector<std::pair<Rect, Colour>> subBoxes;
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
