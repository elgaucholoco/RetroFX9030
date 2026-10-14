#include "PluginEditor.h"
#include "Font5x7.h"

using namespace juce;

namespace
{
const Colour kText     { 0xffe9e9e6 };
const Colour kLedRed   { 0xffff3b2a };
const Colour kLedGreen { 0xff46ff5e };
const Colour kLedAmber { 0xffffb21e };

Font panelFont (float size, bool bold = true)
{
    return Font (FontOptions ("Helvetica Neue", size, bold ? Font::bold : Font::plain)).withHorizontalScale (0.92f);
}

void label (Graphics& g, const String& s, float cx, float y, float size = 10.5f)
{
    g.setColour (kText);
    g.setFont (panelFont (size));
    g.drawText (s, Rectangle<float> (cx - 60, y, 120, size + 3), Justification::centred, false);
}

void triangle (Graphics& g, float cx, float cy, float s, int dir)   // 0 bas, 1 haut, 2 gauche, 3 droite
{
    Path p;
    if (dir == 0) p.addTriangle (cx - s, cy - s * 0.6f, cx + s, cy - s * 0.6f, cx, cy + s * 0.8f);
    if (dir == 1) p.addTriangle (cx - s, cy + s * 0.6f, cx + s, cy + s * 0.6f, cx, cy - s * 0.8f);
    if (dir == 2) p.addTriangle (cx + s * 0.6f, cy - s, cx + s * 0.6f, cy + s, cx - s * 0.9f, cy);
    if (dir == 3) p.addTriangle (cx - s * 0.6f, cy - s, cx - s * 0.6f, cy + s, cx + s * 0.9f, cy);
    g.setColour (kText);
    g.fillPath (p);
}

Colour ledColour (Controller::Led l)
{
    switch (l)
    {
        case Controller::Led::Red:   return kLedRed;
        case Controller::Led::Green: return kLedGreen;
        case Controller::Led::Amber: return kLedAmber;
        default: return Colours::transparentBlack;
    }
}
}

namespace ui
{
//==============================================================================
void drawKnob (Graphics& g, Point<float> c, float r, float pos)
{
    constexpr float start = -2.35619f, sweep = 4.71239f;
    auto polar = [c] (float a, float rad) { return Point<float> (c.x + std::sin (a) * rad, c.y - std::cos (a) * rad); };

    // graduations
    g.setColour (kText.withAlpha (0.85f));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = start + sweep * (float) i / 10.0f;
        g.drawLine (Line<float> (polar (a, r + 3.5f), polar (a, r + (i % 5 == 0 ? 9.0f : 7.0f))), 1.3f);
    }
    // ombre + jupe crantée
    g.setColour (Colours::black.withAlpha (0.55f));
    g.fillEllipse (Rectangle<float> (r * 2, r * 2).withCentre (c.translated (0, 2.5f)));
    g.setColour (Colour (0xff0d0d0e));
    g.fillEllipse (Rectangle<float> (r * 2, r * 2).withCentre (c));
    g.setColour (Colour (0xff2c2d30));
    for (int i = 0; i < 30; ++i)
    {
        const float a = (float) i / 30.0f * MathConstants<float>::twoPi;
        g.drawLine (Line<float> (polar (a, r * 0.84f), polar (a, r * 0.98f)), 1.2f);
    }
    // chapeau
    const float cr = r * 0.78f;
    ColourGradient grad (Colour (0xff4b4c51), c.x - cr * 0.4f, c.y - cr * 0.6f, Colour (0xff141517), c.x + cr * 0.5f, c.y + cr, true);
    g.setGradientFill (grad);
    g.fillEllipse (Rectangle<float> (cr * 2, cr * 2).withCentre (c));
    g.setColour (Colours::white.withAlpha (0.08f));
    g.drawEllipse (Rectangle<float> (cr * 2, cr * 2).withCentre (c), 1.0f);
    // index
    const float a = start + sweep * jlimit (0.0f, 1.0f, pos);
    g.setColour (Colours::white);
    g.drawLine (Line<float> (polar (a, r * 0.22f), polar (a, r * 0.74f)), 2.6f);
}

void KnobLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider&)
{
    const auto b = Rectangle<int> (x, y, w, h).toFloat();
    drawKnob (g, b.getCentre(), std::min (w, h) * 0.5f - 11.0f, pos);
}

//==============================================================================
void Vfd::setState (const std::string& l1, const std::string& l2, int cursor, bool cursorOn, int dimmer)
{
    if (l1 == line1 && l2 == line2 && cursor == cursorCol && cursorOn == cursorVisible && dimmer == dim) return;
    line1 = l1; line2 = l2; cursorCol = cursor; cursorVisible = cursorOn; dim = dimmer;
    repaint();
}

void Vfd::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setGradientFill (ColourGradient (Colour (0xff081311), 0, 0, Colour (0xff020605), 0, b.getHeight(), false));
    g.fillRect (b);

    const float pitch = std::min ((b.getWidth() - 14.0f) / 119.0f, (b.getHeight() - 10.0f) / 18.0f);
    const float dot = pitch * 0.74f;
    const float ox = (b.getWidth() - 119.0f * pitch) * 0.5f, oy = (b.getHeight() - 18.0f * pitch) * 0.5f;
    static const float levels[4] = { 0.42f, 0.6f, 0.8f, 1.0f };
    const float bright = levels[jlimit (1, 4, dim) - 1];
    const Colour lit = Colour::fromFloatRGBA (0.40f, 1.0f, 0.86f, bright);
    const Colour glow = lit.withAlpha (0.16f * bright);
    const Colour off = Colour (0xff10231f).withAlpha (0.55f);

    auto lineText = [this] (int l) -> const std::string& { return l == 0 ? line1 : line2; };
    auto isLit = [&] (int l, int c, int col, int row) -> bool
    {
        const auto& s = lineText (l);
        if (row == 7) return l == 1 && cursorVisible && c == cursorCol;
        if (c >= (int) s.size()) return false;
        return (font5x7::glyph ((unsigned char) s[(size_t) c])[col] >> row) & 1;
    };

    for (int pass = 0; pass < 3; ++pass)
        for (int l = 0; l < 2; ++l)
            for (int c = 0; c < 20; ++c)
                for (int col = 0; col < 5; ++col)
                    for (int row = 0; row < 8; ++row)
                    {
                        const bool on = isLit (l, c, col, row);
                        if (row == 7 && ! on && pass != 0) continue;
                        const float x = ox + (float) (c * 6 + col) * pitch;
                        const float y = oy + (float) (l * 10 + row) * pitch;
                        if (pass == 0 && row < 7)  { g.setColour (off); g.fillRect (x, y, dot, dot); }
                        if (pass == 1 && on)       { g.setColour (glow); g.fillRoundedRectangle (x - pitch * 0.35f, y - pitch * 0.35f, dot + pitch * 0.7f, dot + pitch * 0.7f, pitch * 0.4f); }
                        if (pass == 2 && on)       { g.setColour (lit); g.fillRect (x, y, dot, dot); }
                    }

    // reflet de la vitre
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.05f), 0, 0, Colours::transparentWhite, b.getWidth() * 0.6f, b.getHeight(), false));
    g.fillRect (b);
}

//==============================================================================
void SevenSeg::setText (const std::string& t)
{
    if (t != text) { text = t; repaint(); }
}

void SevenSeg::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (Colour (0xff0b0302));
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (Colour (0xff3a3b3e));
    g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);

    static const uint8_t map[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
    const float h = b.getHeight() * 0.7f, w = h * 0.52f, t = w * 0.2f, gap = w * 0.38f;
    const float x0 = b.getCentreX() - w - gap * 0.5f, y0 = b.getCentreY() - h * 0.5f;

    for (int d = 0; d < 2; ++d)
    {
        const int bits = d < (int) text.size() && std::isdigit ((unsigned char) text[(size_t) d]) ? map[text[(size_t) d] - '0'] : 0;
        const float x = x0 + (float) d * (w + gap), y = y0;
        const Rectangle<float> seg[7] = {
            { x + t, y, w - 2 * t, t },                        // a
            { x + w - t, y + t, t, h * 0.5f - 1.5f * t },      // b
            { x + w - t, y + h * 0.5f + 0.5f * t, t, h * 0.5f - 1.5f * t }, // c
            { x + t, y + h - t, w - 2 * t, t },                // d
            { x, y + h * 0.5f + 0.5f * t, t, h * 0.5f - 1.5f * t },         // e
            { x, y + t, t, h * 0.5f - 1.5f * t },              // f
            { x + t, y + h * 0.5f - t * 0.5f, w - 2 * t, t }   // g
        };
        Graphics::ScopedSaveState ss (g);
        g.addTransform (AffineTransform::shear (-0.09f, 0).translated (b.getCentreY() * 0.09f, 0));
        for (int s = 0; s < 7; ++s)
        {
            const bool on = (bits >> s) & 1;
            if (on)
            {
                g.setColour (Colour (0xffff2e1c).withAlpha (0.22f));
                g.fillRoundedRectangle (seg[s].expanded (t * 0.5f), t);
            }
            g.setColour (on ? Colour (0xffff3a24) : Colour (0xff2a0906));
            g.fillRoundedRectangle (seg[s].reduced (t * 0.08f), t * 0.4f);
        }
    }
}

//==============================================================================
void Indicators::setState (const std::array<Controller::Led, defs::NumModules>& l)
{
    if (l != leds) { leds = l; repaint(); }
}

void Indicators::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (Colour (0xff070707));
    g.fillRoundedRectangle (b, 4.0f);
    const float cw = (b.getWidth() - 8.0f) / 3.0f, ch = (b.getHeight() - 8.0f) / 3.0f;
    g.setFont (Font (FontOptions ("Arial Narrow", ch * 0.68f, Font::bold)));
    for (int m = 0; m < defs::NumModules; ++m)
    {
        const auto cell = Rectangle<float> (4.0f + (float) (m % 3) * cw, 4.0f + (float) (m / 3) * ch, cw, ch).reduced (2.0f);
        const auto col = ledColour (leds[(size_t) m]);
        const bool on = leds[(size_t) m] != Controller::Led::Off;
        if (on)
        {
            g.setColour (col.withAlpha (0.16f));
            g.fillRect (cell);
        }
        g.setColour (on ? col.withAlpha (0.55f) : Colour (0xff2b2b2b));
        g.drawRect (cell, 1.0f);
        g.setColour (on ? col : Colour (0xff333333));
        g.drawText (defs::kModuleNames[m], cell, Justification::centred, false);
    }
}

//==============================================================================
void SignalLeds::setLevel (float l)
{
    const int q0 = (int) (level * 20), q1 = (int) (l * 20);
    level = l;
    if (q0 != q1) repaint();
}

void SignalLeds::paint (Graphics& g)
{
    const float w = (float) getWidth();
    auto led = [&] (float y, Colour c, bool on)
    {
        const auto r = Rectangle<float> (w * 0.7f, w * 0.7f).withCentre ({ w * 0.5f, y });
        if (on) { g.setColour (c.withAlpha (0.3f)); g.fillEllipse (r.expanded (2.5f)); }
        g.setColour (on ? c : c.darker (0.85f).withAlpha (0.6f));
        g.fillEllipse (r);
    };
    led (w * 0.5f, kLedRed, level > 0.89f);
    const Colour sig = level > 0.35f ? kLedAmber : kLedGreen;
    led ((float) getHeight() - w * 0.5f, sig, level > 0.01f);
}

//==============================================================================
void PanelButton::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.5f);
    if (down) r = r.translated (0, 1.0f);
    Path p;
    const float rad = r.getHeight() * 0.5f;
    switch (shape)
    {
        case Shape::Pill:        p.addRoundedRectangle (r, rad); break;
        case Shape::Round:
        case Shape::Power:       p.addEllipse (r); break;
        case Shape::RockerLeft:  p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), rad, rad, true, false, true, false); break;
        case Shape::RockerRight: p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), rad, rad, false, true, false, true); break;
    }
    if (! down)
    {
        g.setColour (Colours::black.withAlpha (0.6f));
        g.fillPath (p, AffineTransform::translation (0, 1.8f));
    }
    const bool light = shape == Shape::Power;
    const Colour top = light ? Colour (0xffb5b6b9) : Colour (0xff4c4d52), bottom = light ? Colour (0xff7d7e82) : Colour (0xff27282b);
    g.setGradientFill (ColourGradient (down ? bottom : top, 0, r.getY(), down ? top.darker (0.2f) : bottom, 0, r.getBottom(), false));
    g.fillPath (p);
    g.setColour (Colour (0xff0b0b0c));
    g.strokePath (p, PathStrokeType (1.2f));
    g.setColour (Colours::white.withAlpha (light ? 0.35f : 0.09f));
    g.drawLine (r.getX() + rad * 0.8f, r.getY() + 1.6f, r.getRight() - rad * 0.8f, r.getY() + 1.6f, 1.0f);
}

void PanelButton::mouseDown (const MouseEvent&)
{
    down = true; repaint();
    if (onPress) onPress();
    if (autoRepeat) startTimer (420);
}

void PanelButton::mouseUp (const MouseEvent&)
{
    down = false; repaint();
    stopTimer();
}

void PanelButton::timerCallback()
{
    if (onPress) onPress();
    startTimer (75);
}

//==============================================================================
void DataKnob::setPosition (float p)
{
    if (std::abs (p - pos) > 0.002f) { pos = p; repaint(); }
}

void DataKnob::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    drawKnob (g, b.getCentre(), std::min (b.getWidth(), b.getHeight()) * 0.5f - 11.0f, pos);
}

void DataKnob::mouseDown (const MouseEvent& e) { lastY = e.position.y; accum = 0; }

void DataKnob::mouseDrag (const MouseEvent& e)
{
    const float dy = lastY - e.position.y;
    lastY = e.position.y;
    const int range = getRange ? getRange() : 100;
    if (range <= 0) return;
    float stepsPerPx = std::max (1.0f / 12.0f, (float) range / 180.0f);
    if (e.mods.isShiftDown()) stepsPerPx *= 0.25f;
    emit (dy * stepsPerPx);
}

void DataKnob::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    const int range = getRange ? getRange() : 100;
    if (range <= 0) return;
    const float dir = (w.isReversed ? -1.0f : 1.0f) * (w.deltaY != 0 ? w.deltaY : w.deltaX);
    emit (dir * std::max (1.5f, (float) range / 25.0f) * 4.0f);
}

void DataKnob::emit (float steps)
{
    accum += steps;
    const int whole = (int) accum;
    if (whole != 0)
    {
        accum -= (float) whole;
        if (onTurn) onTurn (whole);
    }
}
}

//==============================================================================
void RetroFXEditor::Panel::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.fillAll (Colour (0xff050505));

    // châssis
    g.setGradientFill (ColourGradient (Colour (0xff2a2b2e), 0, 0, Colour (0xff141517), 0, b.getHeight(), false));
    g.fillRoundedRectangle (b.reduced (2.0f), 16.0f);
    g.setColour (Colour (0xff47484c));
    g.drawRoundedRectangle (b.reduced (2.5f), 16.0f, 1.4f);

    // sections
    auto bevel = [&g] (Rectangle<float> r, float rad, Colour fill)
    {
        g.setColour (fill);
        g.fillRoundedRectangle (r, rad);
        g.setColour (Colour (0xff3d3e42));
        g.drawRoundedRectangle (r, rad, 1.2f);
        g.setColour (Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (r.translated (0, 1.2f), rad, 0.8f);
    };
    bevel ({ 222, 10, 862, 240 }, 10, Colour (0xff1d1e21));
    bevel ({ 234, 170, 838, 72 }, 9, Colour (0xff1a1b1e));

    // ---- section gauche
    label (g, "Hi-Z", 66, 92, 10); label (g, "INPUT", 66, 103, 10);
    label (g, "PHONES", 172, 97, 10);
    label (g, "SIGNAL", 119, 87, 8.5f);
    for (float cx : { 66.0f, 172.0f })   // jacks 6,35
    {
        const Point<float> c (cx, 168);
        g.setColour (Colours::black.withAlpha (0.6f)); g.fillEllipse (Rectangle<float> (46, 46).withCentre (c.translated (0, 2)));
        g.setGradientFill (ColourGradient (Colour (0xffc9cacd), cx - 12, 152, Colour (0xff5d5e62), cx + 14, 186, false));
        g.fillEllipse (Rectangle<float> (42, 42).withCentre (c));
        g.setColour (Colour (0xff2e2f32)); g.drawEllipse (Rectangle<float> (42, 42).withCentre (c), 1.0f);
        g.setColour (Colour (0xff8e8f93)); g.fillEllipse (Rectangle<float> (28, 28).withCentre (c));
        g.setColour (Colour (0xff050505)); g.fillEllipse (Rectangle<float> (14, 14).withCentre (c));
    }
    {   // logo DSP
        g.setColour (kText);
        g.setFont (Font (FontOptions ("Helvetica Neue", 17.0f, Font::bold)).withExtraKerningFactor (0.45f));
        g.drawText ("DSP", Rectangle<float> (70, 203, 100, 22), Justification::centred, false);
        g.fillRect (Rectangle<float> (88, 201, 64, 1.6f));
        g.fillRect (Rectangle<float> (88, 226, 64, 1.6f));
    }

    // ---- logo (marque fictive, rayée façon années 90)
    {
        GlyphArrangement ga;
        ga.addLineOfText (Font (FontOptions ("Futura", 23.0f, Font::bold | Font::italic)).withExtraKerningFactor (0.08f), "RETROFX", 240.0f, 36.0f);
        Path logo; ga.createPath (logo);
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (logo);
        g.setColour (kText);
        g.fillRect (logo.getBounds());
        g.setColour (Colour (0xff1d1e21));
        for (float y = logo.getBounds().getY() + 3.0f; y < logo.getBounds().getBottom(); y += 4.0f)
            g.fillRect (logo.getBounds().getX(), y, logo.getBounds().getWidth(), 1.1f);
    }
    label (g, "PATCH NO.", 286, 46, 9.5f);

    // ---- fenêtre de l'afficheur
    g.setColour (Colour (0xff2f3034));
    g.fillRoundedRectangle (348, 16, 514, 140, 8);
    g.setColour (Colour (0xff4a4b50));
    g.drawRoundedRectangle (348, 16, 514, 140, 8, 1.2f);
    g.setColour (kText);
    g.setFont (panelFont (10.0f).withExtraKerningFactor (0.12f));
    g.drawText ("ADVANCED  INSTRUMENT  EFFECTS  PROCESSOR", Rectangle<float> (348, 18, 514, 14), Justification::centred, false);
    g.setColour (Colours::black);
    g.fillRoundedRectangle (356, 32, 498, 116, 5);

    // ---- « 9030 » en chiffres creux
    {
        GlyphArrangement ga;
        ga.addLineOfText (Font (FontOptions ("Arial Rounded MT Bold", 42.0f, Font::plain)).withHorizontalScale (1.25f).withExtraKerningFactor (0.06f), "9030", 893.0f, 56.0f);
        Path p; ga.createPath (p);
        g.setColour (kText);
        g.strokePath (p, PathStrokeType (1.8f));
    }
    g.setColour (Colour (0xff070707));
    g.fillRoundedRectangle (893, 66, 174, 86, 5);

    // ---- touches : sérigraphie
    label (g, "PATCH", 308, 177);
    triangle (g, 276, 183, 3.5f, 0); triangle (g, 340, 183, 3.5f, 1);
    label (g, "COMPARE", 338, 226, 9.5f);
    label (g, "STORE", 405, 177); label (g, "EXECUTE", 405, 226, 9.5f);
    label (g, "EXIT", 475, 177);
    label (g, "UTILITY", 568, 177); label (g, "PAGE", 568, 226, 9.5f);
    label (g, "PRG-CHG", 666, 172, 9); label (g, "LEARN", 666, 182, 9.5f);
    triangle (g, 666, 231, 3.5f, 0);
    label (g, "CTL-CHG", 760, 172, 9); label (g, "MONITOR", 760, 182, 9.5f);
    triangle (g, 760, 231, 3.5f, 1);
    label (g, "BYPASS", 840, 177);
    label (g, "EDIT", 950, 177);
    triangle (g, 918, 231, 4.0f, 2); triangle (g, 982, 231, 4.0f, 3);

    // ---- section droite : molettes 1-4
    g.setColour (kText.withAlpha (0.8f));
    g.drawLine (1195, 18, 1195, 190, 1.2f);
    g.drawLine (1098, 106, 1290, 106, 1.2f);
    g.setFont (panelFont (10.5f));
    g.drawText ("1", Rectangle<float> (1182, 92, 10, 12), Justification::centred, false);
    g.drawText ("2", Rectangle<float> (1198, 92, 10, 12), Justification::centred, false);
    g.drawText ("3", Rectangle<float> (1182, 108, 10, 12), Justification::centred, false);
    g.drawText ("4", Rectangle<float> (1198, 108, 10, 12), Justification::centred, false);
    label (g, "POWER", 1150, 200, 10);
    g.setFont (panelFont (9.0f));
    g.setColour (kText);
    g.fillRect (1172.0f, 222.0f, 6.0f, 3.0f);   g.drawText ("ON", Rectangle<float> (1182, 217, 30, 12), Justification::centredLeft, false);
    g.drawRect (1172.0f, 236.0f, 6.0f, 4.0f, 1.0f); g.drawText ("OFF", Rectangle<float> (1182, 232, 30, 12), Justification::centredLeft, false);
}

//==============================================================================
RetroFXEditor::RetroFXEditor (RetroFXProcessor& p)
    : AudioProcessorEditor (&p), proc (p), ctl (p.controller)
{
    setOpaque (true);
    addAndMakeVisible (panel);
    panel.setBounds (0, 0, kW, kH);

    panel.addAndMakeVisible (vfd);        vfd.setBounds (362, 36, 486, 108);
    panel.addAndMakeVisible (seg);        seg.setBounds (242, 60, 90, 62);
    panel.addAndMakeVisible (indicators); indicators.setBounds (896, 69, 168, 80);
    panel.addAndMakeVisible (signal);     signal.setBounds (113, 70, 12, 40);

    for (auto* s : { &inputKnob, &phonesKnob })
    {
        s->setSliderStyle (Slider::RotaryVerticalDrag);
        s->setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        s->setLookAndFeel (&knobLnf);
        s->setMouseDragSensitivity (180);
        panel.addAndMakeVisible (*s);
    }
    inputKnob.setBounds (66 - 41, 56 - 41, 82, 82);
    phonesKnob.setBounds (172 - 41, 56 - 41, 82, 82);
    inputKnob.setTooltip ("Gain d'entree (Hi-Z INPUT). Reglez-le pour que la LED SIGNAL passe a l'orange sur les attaques.");
    phonesKnob.setTooltip ("Volume de sortie (PHONES)");
    inputAtt  = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "input", inputKnob);
    phonesAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "output", phonesKnob);

    const Point<int> centres[4] = { { 1145, 58 }, { 1245, 58 }, { 1145, 152 }, { 1245, 152 } };
    for (int k = 0; k < 4; ++k)
    {
        auto& d = dataKnobs[k];
        panel.addAndMakeVisible (d);
        d.setBounds (Rectangle<int> (86, 86).withCentre (centres[k]));
        d.onTurn = [this, k] (int steps) { ctl.turn (k, steps); refresh(); };
        d.getRange = [this, k] { return ctl.knobRange (k); };
        d.setTooltip ("Molette " + String (k + 1) + " : glisser verticalement ou molette souris (Maj = reglage fin)");
    }

    using S = ui::PanelButton::Shape;
    using K = Controller::Key;
    addButton (S::RockerLeft, true, K::PatchDown, { 248, 194, 60, 28 }, "PATCH \xe2\x96\xbc : patch precedent");
    addButton (S::RockerRight, true, K::PatchUp, { 308, 194, 60, 28 }, "PATCH \xe2\x96\xb2 : patch suivant. En mode Edit : COMPARE (ecoute le patch memorise)");
    addButton (S::Round, false, K::Store, { 392, 195, 26, 26 }, "STORE / EXECUTE : memoriser le patch (choisir la destination avec PATCH, puis STORE)");
    addButton (S::Pill, false, K::Exit, { 440, 194, 70, 28 }, "EXIT : retour au mode Play");
    addButton (S::Pill, false, K::Utility, { 530, 194, 76, 28 }, "UTILITY / PAGE : mode Utility. En mode Edit : page suivante de parametres");
    addButton (S::Pill, true, K::Dec, { 628, 194, 76, 28 }, "PRG-CHG LEARN / DEC \xe2\x96\xbc : en mode Edit, type d'effet precedent");
    addButton (S::Pill, true, K::Inc, { 722, 194, 76, 28 }, "CTL-CHG MONITOR / INC \xe2\x96\xb2 : en mode Edit, type d'effet suivant");
    addButton (S::Round, false, K::Bypass, { 827, 195, 26, 26 }, "BYPASS : bypass general. En mode Edit : active / coupe le module");
    addButton (S::RockerLeft, false, K::EditLeft, { 885, 194, 65, 28 }, "EDIT \xe2\x97\x80 : module precedent (entre en mode Edit)");
    addButton (S::RockerRight, false, K::EditRight, { 950, 194, 65, 28 }, "EDIT \xe2\x96\xb6 : module suivant (entre en mode Edit)");
    addButton (S::Power, false, K::Power, { 1137, 216, 26, 26 }, "POWER");

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) kW / kH);
    setResizeLimits (kW / 2, kH / 2, kW * 2, kH * 2);
    setSize ((int) (kW * 0.85f), (int) (kH * 0.85f));

    refresh();
    startTimerHz (30);
}

RetroFXEditor::~RetroFXEditor()
{
    stopTimer();
    inputKnob.setLookAndFeel (nullptr);
    phonesKnob.setLookAndFeel (nullptr);
}

ui::PanelButton& RetroFXEditor::addButton (ui::PanelButton::Shape s, bool repeat, Controller::Key key, Rectangle<int> r, const char* tipUtf8)
{
    buttons.push_back (std::make_unique<ui::PanelButton> (s, repeat));
    auto& b = *buttons.back();
    b.onPress = [this, key] { ctl.press (key); refresh(); };
    b.setTooltip (String::fromUTF8 (tipUtf8));
    b.setBounds (r);
    panel.addAndMakeVisible (b);
    return b;
}

void RetroFXEditor::paint (Graphics& g) { g.fillAll (Colours::black); }

void RetroFXEditor::resized()
{
    panel.setTransform (AffineTransform::scale ((float) getWidth() / (float) kW));
}

void RetroFXEditor::timerCallback() { refresh(); }

void RetroFXEditor::refresh()
{
    const bool blink = (Time::getMillisecondCounter() / 330) % 2 == 0;
    std::string l1, l2; int cursor = -1;
    ctl.render (l1, l2, cursor, blink);
    vfd.setState (l1, l2, cursor, blink, ctl.dimmer());
    seg.setText (ctl.patchDigits (blink));

    std::array<Controller::Led, defs::NumModules> leds;
    for (int m = 0; m < defs::NumModules; ++m) leds[(size_t) m] = ctl.indicator (m, blink);
    indicators.setState (leds);

    const float pk = proc.shared.inPeak.exchange (0.0f);
    peakHold = std::max (pk, peakHold * 0.85f);
    signal.setLevel (ctl.isPowerOn() ? peakHold : 0.0f);

    for (int k = 0; k < 4; ++k) dataKnobs[k].setPosition (ctl.knobPosition (k));
}
