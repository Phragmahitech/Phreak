#include "PluginEditor.h"
#include "BinaryData.h"

using P = PhreakAudioProcessor;
using L = PhreakLookAndFeel;

//==============================================================================
// Look
PhreakLookAndFeel::PhreakLookAndFeel()
{
    semiBoldFace = juce::Typeface::createSystemTypefaceFor (BinaryData::ChakraPetchSemiBold_ttf, (size_t) BinaryData::ChakraPetchSemiBold_ttfSize);
    boldFace = juce::Typeface::createSystemTypefaceFor (BinaryData::ChakraPetchBold_ttf, (size_t) BinaryData::ChakraPetchBold_ttfSize);

    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff142229));
    setColour (juce::PopupMenu::textColourId, text());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent());
}

juce::Font PhreakLookAndFeel::font (float height, bool bold, float tracking) const
{
    const juce::Font base (juce::FontOptions (bold ? boldFace : semiBoldFace).withPointHeight (height));
    return tracking > 0.0f ? base.withExtraKerningFactor (tracking) : base;
}

juce::Colour PhreakLookAndFeel::pointColour (int point)
{
    const float t = (float) juce::jlimit (0, 15, point) / 15.0f;
    return t < 0.5f ? accent().interpolatedWith (mauve(), t * 2.0f)
                    : mauve().interpolatedWith (lilac(), (t - 0.5f) * 2.0f);
}

juce::Path PhreakLookAndFeel::chamfered (juce::Rectangle<float> r, float cut)
{
    cut = juce::jmin (cut, r.getWidth() * 0.5f, r.getHeight() * 0.5f);
    juce::Path p;
    p.startNewSubPath (r.getX() + cut, r.getY());
    p.lineTo (r.getRight() - cut, r.getY());
    p.lineTo (r.getRight(), r.getY() + cut);
    p.lineTo (r.getRight(), r.getBottom() - cut);
    p.lineTo (r.getRight() - cut, r.getBottom());
    p.lineTo (r.getX() + cut, r.getBottom());
    p.lineTo (r.getX(), r.getBottom() - cut);
    p.lineTo (r.getX(), r.getY() + cut);
    p.closeSubPath();
    return p;
}

void PhreakLookAndFeel::drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, const juce::Font& f)
{
    const auto shape = chamfered (r, 8.0f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 10, { 0, 3 }).drawForPath (g, shape);
    g.setGradientFill (juce::ColourGradient (panelTop(), 0.0f, r.getY(), panelBottom(), 0.0f, r.getBottom(), false));
    g.fillPath (shape);
    g.setColour (edge());
    g.strokePath (shape, juce::PathStrokeType (1.0f));
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 8.0f, r.getRight() - 8.0f);

    if (title.isNotEmpty())
    {
        g.setFont (f);
        g.setColour (lilac());
        g.drawText (title, r.withHeight (22.0f).translated (12.0f, 4.0f).withWidth (r.getWidth() - 24.0f), juce::Justification::centredLeft, false);
    }
}

void PhreakLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float start, float end, juce::Slider& s)
{
    const bool big = (bool) s.getProperties().getWithDefault ("big", false);
    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
    const auto colour = s.getProperties().contains ("colour") ? juce::Colour ((juce::uint32) (juce::int64) s.getProperties()["colour"]) : accent();
    const bool enabled = s.isEnabled();

    auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);

    // Nom en haut, valeur en bas
    const float nameH = big ? 18.0f : 13.0f, valueH = big ? 20.0f : 14.0f;
    auto nameArea = area.removeFromTop (nameH);
    auto valueArea = area.removeFromBottom (valueH);

    g.setColour (enabled ? dim() : dim().withAlpha (0.4f));
    g.setFont (font (big ? 12.0f : 9.5f, true, 0.18f));
    g.drawText (s.getName(), nameArea, juce::Justification::centred, false);

    g.setColour (enabled ? text() : text().withAlpha (0.35f));
    g.setFont (font (big ? 15.0f : 10.5f, true, 0.04f));
    g.drawText (s.getTextFromValue (s.getValue()), valueArea, juce::Justification::centred, false);

    const float radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - (big ? 6.0f : 3.0f);
    const auto c = area.getCentre();
    const float arcR = radius, bodyR = radius - (big ? 10.0f : 5.5f);
    const float thick = big ? 6.0f : 3.5f;

    // Piste
    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (juce::Colour (0xff081014));
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Valeur (dégradé orange -> mauve ; depuis le centre pour les réglages bipolaires)
    const float angle = start + pos * (end - start);
    const float from = bipolar ? start + 0.5f * (end - start) : start;
    if (std::abs (angle - from) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        juce::ColourGradient grad (colour, c.x - arcR, c.y + arcR, mauve(), c.x + arcR, c.y - arcR, false);
        if (colour != accent())
            grad = juce::ColourGradient (colour, c.x - arcR, c.y, colour.brighter (0.25f), c.x + arcR, c.y, false);
        g.setGradientFill (grad);
        g.setOpacity (enabled ? 1.0f : 0.35f);
        g.strokePath (value, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setOpacity (1.0f);
    }

    // Corps : anneau cuivre, centre turquoise sombre
    juce::DropShadow (juce::Colours::black.withAlpha (0.6f), big ? 12 : 6, { 0, big ? 4 : 2 })
        .drawForPath (g, [&] { juce::Path p; p.addEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c)); return p; }());
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a3f4a), c.x, c.y - bodyR, juce::Colour (0xff111c22), c.x, c.y + bodyR, false));
    g.fillEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c));
    g.setColour (lilac().withAlpha (enabled ? 0.85f : 0.35f));
    g.drawEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c), big ? 1.6f : 1.0f);

    // Index
    const auto tip = c.getPointOnCircumference (bodyR - (big ? 6.0f : 3.0f), angle);
    const auto base = c.getPointOnCircumference (bodyR * (big ? 0.35f : 0.25f), angle);
    g.setColour (enabled ? text() : text().withAlpha (0.35f));
    g.drawLine ({ base, tip }, big ? 3.0f : 1.8f);
}

void PhreakLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto area = b.getLocalBounds().toFloat().reduced (0.5f);
    const auto shape = chamfered (area, juce::jmin (5.0f, area.getHeight() * 0.25f));
    const bool on = b.getToggleState();
    const auto tint = b.getProperties().contains ("colour") ? juce::Colour ((juce::uint32) (juce::int64) b.getProperties()["colour"]) : accent();

    if (on)
    {
        juce::ColourGradient grad (tint, area.getX(), area.getY(), tint == accent() ? mauve() : tint.darker (0.3f), area.getRight(), area.getBottom(), false);
        g.setGradientFill (grad);
        g.fillPath (shape);
        g.setColour (juce::Colours::white.withAlpha (over ? 0.25f : 0.12f));
        g.strokePath (shape, juce::PathStrokeType (1.0f));
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1a2a31), 0.0f, area.getY(), juce::Colour (0xff121e24), 0.0f, area.getBottom(), false));
        g.fillPath (shape);
        g.setColour (over ? tint.withAlpha (0.8f) : edge());
        g.strokePath (shape, juce::PathStrokeType (1.0f));
    }

    if (down)
    {
        g.setColour (juce::Colours::black.withAlpha (0.2f));
        g.fillPath (shape);
    }
}

void PhreakLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool on = b.getToggleState();
    auto colour = on ? juce::Colour (0xff0b1216) : text();
    if (! b.isEnabled())
        colour = colour.withAlpha (0.3f);
    g.setColour (colour);
    g.setFont (font (juce::jlimit (8.5f, 11.0f, (float) b.getHeight() * 0.42f), true, 0.14f));
    g.drawText (b.getButtonText(), b.getLocalBounds().reduced (2, 0), juce::Justification::centred, true);
}

void PhreakLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (juce::Colour (0xff142229));
    g.setColour (edge());
    g.drawRect (0, 0, w, h, 1);
}

void PhreakLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                           bool isHighlighted, bool isTicked, bool, const juce::String& itemText,
                                           const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour (edge());
        g.fillRect (area.reduced (10, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    auto r = area.reduced (3, 1);
    if (isHighlighted && isActive)
    {
        g.setGradientFill (juce::ColourGradient (accent(), (float) r.getX(), 0.0f, mauve(), (float) r.getRight(), 0.0f, false));
        g.fillRect (r);
    }
    if (isTicked)
    {
        g.setColour (isHighlighted ? juce::Colours::white : accent());
        g.fillRect (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ (float) r.getX() + 12.0f, (float) r.getCentreY() }));
    }
    g.setColour (! isActive ? dim().withAlpha (0.6f) : isHighlighted ? juce::Colours::white : text());
    g.setFont (getPopupMenuFont());
    g.drawText (itemText, r.withTrimmedLeft (24).withTrimmedRight (10), juce::Justification::centredLeft, true);
}

void PhreakLookAndFeel::getIdealPopupMenuItemSize (const juce::String& itemText, bool isSeparator, int, int& w, int& h)
{
    if (isSeparator) { w = 60; h = 9; return; }
    h = 24;
    w = juce::jmax (150, (int) std::ceil (juce::GlyphArrangement::getStringWidth (getPopupMenuFont(), itemText)) + 44);
}


//==============================================================================
// Forme d'onde
namespace
{
    juce::String timeText (double seconds)
    {
        if (seconds < 60.0)
            return juce::String (seconds, seconds < 10.0 ? 3 : 2) + " s";
        const int m = (int) (seconds / 60.0);
        return juce::String (m) + ":" + juce::String (seconds - m * 60.0, 2).paddedLeft ('0', 5);
    }

    void setRaw (juce::RangedAudioParameter* p, float real)
    {
        if (p != nullptr)
            p->setValueNotifyingHost (p->convertTo0to1 (real));
    }
}

WaveView::WaveView (PhreakAudioProcessor& p, PhreakLookAndFeel& l) : processor (p), lf (l)
{
    setComponentID ("wave");
    startTimerHz (30);
}

WaveView::~WaveView()
{
    stopTimer();
}

void WaveView::setSlot (int s)
{
    s = juce::jlimit (0, P::numSlots - 1, s);
    if (s != slot)
    {
        slot = s;
        viewStart = 0.0;
        viewLength = 1.0;
        cacheValid = false;
        repaint();
    }
}

void WaveView::setSelectedPoint (int p)
{
    selected = p;
    // le point choisi hors de la vue zoomée : on recentre
    const double pos = processor.getParam (P::pointId (p, "Pos")) * 0.01;
    if (pointOnThisSlot (p) && (pos < viewStart || pos > viewStart + viewLength))
    {
        viewStart = pos - viewLength * 0.5;
        clampView();
        cacheValid = false;
    }
    repaint();
}

float WaveView::xForPos (float pos) const
{
    const auto r = plot();
    return r.getX() + (float) ((pos - viewStart) / viewLength) * r.getWidth();
}

float WaveView::posForX (float x) const
{
    const auto r = plot();
    return (float) juce::jlimit (0.0, 1.0, viewStart + (double) (x - r.getX()) / r.getWidth() * viewLength);
}

void WaveView::clampView()
{
    const auto& s = processor.getSample (slot);
    const double minLength = s.isLoaded() ? juce::jmin (1.0, 400.0 / s.data->length()) : 1.0;
    viewLength = juce::jlimit (minLength, 1.0, viewLength);
    viewStart = juce::jlimit (0.0, 1.0 - viewLength, viewStart);
}

bool WaveView::pointOnThisSlot (int p) const
{
    return juce::roundToInt (processor.getParam (P::pointId (p, "Sample"))) == slot;
}

int WaveView::hitPoint (float x) const
{
    int best = -1;
    float bestDist = 8.0f;
    for (int p = 0; p < P::numPoints; ++p)
    {
        if (! pointOnThisSlot (p)) continue;
        const float d = std::abs (xForPos (processor.getParam (P::pointId (p, "Pos")) * 0.01f) - x) - (p == selected ? 1.5f : 0.0f);
        if (d < bestDist) { bestDist = d; best = p; }
    }
    return best;
}

void WaveView::rebuildCache()
{
    const auto r = plot();
    const int w = juce::jmax (1, (int) r.getWidth()), h = juce::jmax (1, (int) r.getHeight());
    cache = juce::Image (juce::Image::ARGB, w, h, true);
    cacheValid = true;

    const auto& s = processor.getSample (slot);
    if (! s.isLoaded()) return;

    juce::Graphics g (cache);
    const int len = s.data->length();
    const double spp = viewLength * len / w;
    const float mid = h * 0.5f, amp = h * 0.46f;

    // trait central
    g.setColour (L::edge().withAlpha (0.6f));
    g.drawHorizontalLine ((int) mid, 0.0f, (float) w);

    g.setGradientFill (juce::ColourGradient (L::accent(), 0.0f, 0.0f, L::mauve(), 0.0f, (float) h, false));
    for (int x = 0; x < w; ++x)
    {
        const double a = viewStart * len + x * spp;
        const double b = a + juce::jmax (1.0, spp);
        float lo = 0.0f, hi = 0.0f;
        if (spp >= LoadedSample::peakBlock)
        {
            const int b0 = (int) (a / LoadedSample::peakBlock), b1 = juce::jmax (b0 + 1, (int) (b / LoadedSample::peakBlock));
            const int nb = (int) s.peaks.size() / 2;
            for (int k = juce::jmax (0, b0); k < juce::jmin (nb, b1); ++k)
            {
                lo = juce::jmin (lo, s.peaks[(size_t) k * 2]);
                hi = juce::jmax (hi, s.peaks[(size_t) k * 2 + 1]);
            }
        }
        else
        {
            const int i0 = juce::jlimit (0, len - 1, (int) a), i1 = juce::jlimit (i0 + 1, len, (int) std::ceil (b) + 1);
            lo = hi = 0.5f * (s.data->ch[0][(size_t) i0] + s.data->ch[1][(size_t) i0]);
            for (int i = i0; i < i1; ++i)
            {
                const float m = 0.5f * (s.data->ch[0][(size_t) i] + s.data->ch[1][(size_t) i]);
                lo = juce::jmin (lo, m);
                hi = juce::jmax (hi, m);
            }
        }
        const float y0 = mid - juce::jlimit (-1.0f, 1.0f, hi) * amp, y1 = mid - juce::jlimit (-1.0f, 1.0f, lo) * amp;
        g.fillRect ((float) x, y0, 1.0f, juce::jmax (1.0f, y1 - y0));
    }
}

void WaveView::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const auto r = plot();
    g.setColour (L::screen());
    g.fillRect (area);

    const auto& s = processor.getSample (slot);
    if (! cacheValid || cache.getWidth() != (int) r.getWidth())
        rebuildCache();
    g.drawImageAt (cache, (int) r.getX(), (int) r.getY());

    if (! s.isLoaded())
    {
        g.setColour (L::dim());
        g.setFont (lf.font (15.0f, true, 0.12f));
        g.drawText (s.missing ? "MISSING : " + s.name + "  -  drop it again or LOAD" : "DROP A SAMPLE HERE  -  OR CLICK LOAD",
                    area, juce::Justification::centred, false);
    }
    else
    {
        const int len = s.data->length();

        // transitoires
        g.setColour (L::lilac().withAlpha (0.35f));
        for (int on : s.data->onsets)
        {
            const float x = xForPos ((float) on / (float) (len - 1));
            if (x >= r.getX() && x <= r.getRight())
                g.fillRect (x, r.getBottom() - 6.0f, 1.0f, 6.0f);
        }

        // points
        const int active = juce::roundToInt (processor.getParam (P::pointsId));
        for (int pass = 0; pass < 2; ++pass)
            for (int p = 0; p < P::numPoints; ++p)
            {
                if (! pointOnThisSlot (p) || (pass == 1) != (p == selected)) continue;
                const float x = xForPos (processor.getParam (P::pointId (p, "Pos")) * 0.01f);
                if (x < r.getX() - 20.0f || x > r.getRight() + 2.0f) continue;
                const bool on = p < active;
                auto c = L::pointColour (p).withAlpha (on ? 1.0f : 0.35f);
                if (p == playPoint && playPos >= 0.0f) c = c.brighter (0.4f);
                g.setColour (c.withAlpha (p == selected ? 0.95f : (on ? 0.7f : 0.3f)));
                g.fillRect (x - (p == selected ? 1.0f : 0.5f), r.getY(), p == selected ? 2.0f : 1.0f, r.getHeight());
                auto flag = juce::Rectangle<float> (x, r.getY(), 20.0f, 15.0f);
                g.setColour (c);
                g.fillRect (flag);
                if (p == selected)
                {
                    g.setColour (juce::Colours::white);
                    g.drawRect (flag, 1.0f);
                }
                g.setColour (juce::Colour (0xff0b1216));
                g.setFont (lf.font (10.5f, true));
                g.drawText (juce::String (p + 1), flag, juce::Justification::centred, false);
            }

        // tête de lecture
        if (playPos >= 0.0f && processor.getEngine().displaySlot.load() == slot)
        {
            const float x = xForPos (playPos);
            if (x >= r.getX() && x <= r.getRight())
            {
                g.setColour (juce::Colours::white.withAlpha (0.25f));
                g.fillRect (x - 2.0f, r.getY(), 4.0f, r.getHeight());
                g.setColour (juce::Colours::white);
                g.fillRect (x - 0.5f, r.getY(), 1.0f, r.getHeight());
            }
        }

        // infos
        g.setFont (lf.font (10.5f, true, 0.08f));
        g.setColour (L::dim());
        auto info = r.reduced (8.0f, 4.0f).removeFromBottom (14.0f);
        g.drawText (timeText (viewStart * s.seconds()) + "  -  " + timeText ((viewStart + viewLength) * s.seconds())
                        + (viewLength < 0.999 ? "     ZOOM x" + juce::String (1.0 / viewLength, 1) : juce::String()),
                    info.translated (0.0f, -8.0f), juce::Justification::bottomLeft, false);
        g.drawText (s.name + "   " + timeText (s.seconds()) + "   " + juce::String (s.data->sampleRate / 1000.0, 1) + " kHz   "
                        + juce::String ((int) s.data->onsets.size()) + " transients",
                    info.translated (0.0f, -8.0f), juce::Justification::bottomRight, false);

        if (hover >= 0 && dragging < 0)
        {
            g.setColour (L::text());
            const float x = xForPos (processor.getParam (P::pointId (hover, "Pos")) * 0.01f);
            g.drawText (timeText (processor.getParam (P::pointId (hover, "Pos")) * 0.01 * s.seconds()),
                        juce::Rectangle<float> (x + 24.0f, r.getY() + 1.0f, 90.0f, 14.0f), juce::Justification::centredLeft, false);
        }
        if (dragging >= 0)
        {
            g.setColour (L::text());
            const float pos = processor.getParam (P::pointId (dragging, "Pos")) * 0.01f;
            g.drawText (timeText (pos * s.seconds()), juce::Rectangle<float> (xForPos (pos) + 24.0f, r.getY() + 1.0f, 90.0f, 14.0f),
                        juce::Justification::centredLeft, false);
        }
    }

    if (dropping)
    {
        g.setColour (L::accent().withAlpha (0.15f));
        g.fillRect (area);
        g.setColour (L::accent());
        g.drawRect (area, 2.0f);
    }

    g.setColour (L::edge());
    g.drawRect (area, 1.0f);
}

void WaveView::mouseDown (const juce::MouseEvent& e)
{
    const int hit = hitPoint (e.position.x);
    if (hit >= 0)
    {
        selected = hit;
        if (onPointSelected) onPointSelected (hit);
        dragging = hit;
        dragStartPos = processor.getParam (P::pointId (hit, "Pos")) * 0.01f;
        dragStartX = e.position.x;
        if (auto* p = processor.apvts.getParameter (P::pointId (hit, "Pos")))
            p->beginChangeGesture();
    }
    else
    {
        panning = viewLength < 0.999;
        panStartView = viewStart;
        dragStartX = e.position.x;
    }
    repaint();
}

void WaveView::mouseDrag (const juce::MouseEvent& e)
{
    const auto r = plot();
    if (dragging >= 0)
    {
        const float scale = e.mods.isShiftDown() ? 0.1f : 1.0f;
        float pos = juce::jlimit (0.0f, 1.0f, dragStartPos + (e.position.x - dragStartX) / r.getWidth() * (float) viewLength * scale);
        if (! e.mods.isAltDown())
            pos = processor.snapPosition (slot, pos);
        setRaw (processor.apvts.getParameter (P::pointId (dragging, "Pos")), pos * 100.0f);
        repaint();
    }
    else if (panning)
    {
        viewStart = panStartView - (e.position.x - dragStartX) / r.getWidth() * viewLength;
        clampView();
        cacheValid = false;
        repaint();
    }
}

void WaveView::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
        if (auto* p = processor.apvts.getParameter (P::pointId (dragging, "Pos")))
            p->endChangeGesture();
    dragging = -1;
    panning = false;
    repaint();
}

void WaveView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! processor.getSample (slot).isLoaded()) return;
    processor.setParam (P::pointId (selected, "Sample"), (float) slot);
    processor.setParam (P::pointId (selected, "Pos"), 100.0f * processor.snapPosition (slot, posForX (e.position.x)));
    repaint();
}

void WaveView::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitPoint (e.position.x);
    if (h != hover)
    {
        hover = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void WaveView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto r = plot();
    const float dx = e.mods.isShiftDown() ? w.deltaY : w.deltaX;
    const float dy = e.mods.isShiftDown() ? 0.0f : w.deltaY;

    if (std::abs (dy) > 0.0f)
    {
        const double anchor = posForX (e.position.x);
        const double frac = (e.position.x - r.getX()) / r.getWidth();
        viewLength *= std::exp (-dy * 2.5);
        clampView();
        viewStart = anchor - frac * viewLength;
    }
    if (std::abs (dx) > 0.0f)
        viewStart -= dx * viewLength * 0.6;

    clampView();
    cacheValid = false;
    repaint();
}

bool WaveView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"))
            return true;
    return false;
}

void WaveView::filesDropped (const juce::StringArray& files, int, int)
{
    dropping = false;
    int target = slot;
    for (auto& f : files)
    {
        if (target >= P::numSlots) break;
        if (processor.loadSample (target, juce::File (f)))
            ++target;
    }
    cacheValid = false;
    if (onSlotChanged) onSlotChanged (slot);
    repaint();
}

void WaveView::timerCallback()
{
    const auto& eng = processor.getEngine();
    const float pos = eng.displayPos.load();
    const int point = eng.displayPoint.load();
    const int gen = processor.getSampleGeneration();
    if (gen != lastGeneration)
    {
        lastGeneration = gen;
        clampView();
        cacheValid = false;
        repaint();
    }
    if (pos != playPos || point != playPoint || isMouseButtonDown())
    {
        playPos = pos;
        playPoint = point;
        repaint();
    }
}

//==============================================================================
// Grille de pas
StepGrid::StepGrid (PhreakAudioProcessor& p, PhreakLookAndFeel& l) : processor (p), lf (l)
{
    setComponentID ("grid");
    startTimerHz (30);
}

StepGrid::~StepGrid()
{
    stopTimer();
}

juce::Rectangle<float> StepGrid::cellBounds (int step) const
{
    const float gap = 3.0f;
    const float w = ((float) getWidth() - 31.0f * gap) / 32.0f;
    const float h = ((float) getHeight() - gap) / 2.0f;
    const int row = step / 32, col = step % 32;
    return { col * (w + gap), row * (h + gap), w, h };
}

int StepGrid::cellAt (juce::Point<float> p) const
{
    for (int k = 0; k < P::numSteps; ++k)
        if (cellBounds (k).contains (p))
            return k;
    return -1;
}

int StepGrid::value (int step) const
{
    return juce::roundToInt (processor.getParam (P::stepId (step)));
}

void StepGrid::setValue (int step, int v)
{
    processor.setParam (P::stepId (step), (float) juce::jlimit (0, phreak::stepRest, v));
    repaint();
}

void StepGrid::paint (juce::Graphics& g)
{
    const int steps = juce::roundToInt (processor.getParam (P::stepsId));
    const bool gridMode = juce::roundToInt (processor.getParam (P::modeId)) == phreak::modeGrid;

    for (int k = 0; k < P::numSteps; ++k)
    {
        const auto r = cellBounds (k);
        const bool on = k < steps;
        const int v = value (k);
        const auto shape = L::chamfered (r, 3.0f);

        juce::Colour fill = juce::Colour (0xff121e24);
        juce::String label;
        if (v >= 1 && v <= P::numPoints)
        {
            fill = L::pointColour (v - 1).withAlpha (gridMode ? 0.85f : 0.3f);
            label = juce::String (v);
        }
        else if (v == phreak::stepTie)
        {
            fill = juce::Colour (0xff1c2c33);
            label = "-";
        }
        else
        {
            fill = juce::Colour (0xff0a1013);
            label = "x";
        }
        if (! on) fill = fill.withMultipliedAlpha (0.25f);

        g.setColour (fill);
        g.fillPath (shape);
        g.setColour (k == shownStep ? juce::Colours::white : L::edge());
        g.strokePath (shape, juce::PathStrokeType (k == shownStep ? 2.0f : 1.0f));

        g.setColour ((v >= 1 && v <= P::numPoints) ? juce::Colour (0xff0b1216).withAlpha (on ? 1.0f : 0.4f)
                                                     : L::dim().withAlpha (on ? 1.0f : 0.3f));
        g.setFont (lf.font (11.0f, true));
        g.drawText (label, r, juce::Justification::centred, false);

        if (k % 4 == 0)
        {
            g.setColour (L::lilac().withAlpha (on ? 0.7f : 0.2f));
            g.fillRect (r.getX() + 3.0f, r.getBottom() - 3.0f, r.getWidth() - 6.0f, 1.5f);
        }
    }

    if (! gridMode)
    {
        g.setColour (L::screen().withAlpha (0.55f));
        g.fillRect (getLocalBounds());
        g.setColour (L::text());
        g.setFont (lf.font (13.0f, true, 0.14f));
        g.drawText (juce::roundToInt (processor.getParam (P::modeId)) == phreak::modeCycle
                        ? "CYCLE : POINTS 1 > N IN ORDER  -  GRID NOT USED"
                        : "RANDOM : SEEDED RANDOM POINT EACH STEP  -  GRID NOT USED",
                    getLocalBounds(), juce::Justification::centred, false);
    }
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    dragStep = cellAt (e.position);
    dragged = false;
    if (dragStep < 0) return;

    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addItem (1, "TIE  (keep playing)", true, value (dragStep) == phreak::stepTie);
        m.addItem (2, "REST  (silence)", true, value (dragStep) == phreak::stepRest);
        m.addSeparator();
        for (int p = 0; p < P::numPoints; ++p)
            m.addItem (10 + p, "POINT " + juce::String (p + 1), true, value (dragStep) == p + 1);
        const int step = dragStep;
        juce::Component::SafePointer<StepGrid> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, step] (int r)
        {
            if (safe == nullptr || r == 0) return;
            safe->setValue (step, r == 1 ? phreak::stepTie : (r == 2 ? phreak::stepRest : r - 9));
        });
        dragStep = -1;
        return;
    }

    dragStartValue = value (dragStep);
    dragStartY = e.position.y;
    gestureParam = processor.apvts.getParameter (P::stepId (dragStep));
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStep < 0 || gestureParam == nullptr) return;
    const int delta = juce::roundToInt ((dragStartY - e.position.y) / 8.0f);
    if (delta == 0 && ! dragged) return;
    if (! dragged) { gestureParam->beginChangeGesture(); dragged = true; }
    setRaw (gestureParam, (float) juce::jlimit (0, phreak::stepRest, dragStartValue + delta));
    repaint();
}

void StepGrid::mouseUp (const juce::MouseEvent& e)
{
    if (dragStep < 0) return;
    if (dragged)
    {
        if (gestureParam != nullptr) gestureParam->endChangeGesture();
    }
    else if (e.mods.isAltDown())
        setValue (dragStep, phreak::stepTie);
    else if (e.mods.isShiftDown())
        setValue (dragStep, phreak::stepRest);
    else
    {
        const int p = (selectedPoint ? selectedPoint() : 0) + 1;
        setValue (dragStep, value (dragStep) == p ? phreak::stepRest : p);
    }
    dragStep = -1;
    gestureParam = nullptr;
    dragged = false;
}

void StepGrid::timerCallback()
{
    const int s = processor.getEngine().displayStep.load();
    if (s != shownStep)
    {
        shownStep = s;
        repaint();
    }
}

//==============================================================================
// Façade
namespace
{
    // Panneaux
    const juce::Rectangle<int> samplePanel  { 14, 56, 1252, 288 };
    const juce::Rectangle<int> pointPanel   { 14, 354, 760, 234 };
    const juce::Rectangle<int> randomPanel  { 786, 354, 480, 234 };
    const juce::Rectangle<int> seqPanel     { 14, 598, 900, 250 };
    const juce::Rectangle<int> fxPanel      { 926, 598, 340, 250 };

    juce::StringArray choicesOf (juce::RangedAudioParameter* p)
    {
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p)) return c->choices;
        return {};
    }
}

PhreakFacade::PhreakFacade (PhreakAudioProcessor& p)
    : audioProcessor (p), wave (p, lookAndFeel), grid (p, lookAndFeel),
      presetBar (p.presets, [this] (float h, bool b, float t) { return lookAndFeel.font (h, b, t); })
{
    setLookAndFeel (&lookAndFeel);
    setSize (width, height);

    // --- Samples ---
    addAndMakeVisible (wave);
    wave.setBounds (samplePanel.getX() + 10, samplePanel.getY() + 64, samplePanel.getWidth() - 20, samplePanel.getHeight() - 74);
    wave.onPointSelected = [this] (int pt) { selectPoint (pt); };
    wave.onSlotChanged = [this] (int) { refreshSlotTabs(); };

    for (int s = 0; s < 4; ++s)
    {
        auto& b = slotTabs[s];
        b.setComponentID ("slot" + juce::String (s + 1));
        b.setClickingTogglesState (false);
        b.onClick = [this, s] { showSlot (s); };
        b.setBounds (samplePanel.getX() + 104 + s * 232, samplePanel.getY() + 30, 226, 26);
        addAndMakeVisible (b);
    }
    loadButton.setComponentID ("load");
    clearButton.setComponentID ("clear");
    loadButton.setBounds (samplePanel.getRight() - 210, samplePanel.getY() + 30, 96, 26);
    clearButton.setBounds (samplePanel.getRight() - 108, samplePanel.getY() + 30, 96, 26);
    loadButton.onClick = [this] { loadIntoSlot (shownSlot); };
    clearButton.onClick = [this] { audioProcessor.clearSample (shownSlot); refreshSlotTabs(); };
    addAndMakeVisible (loadButton);
    addAndMakeVisible (clearButton);

    // --- Point ---
    for (int i = 0; i < 16; ++i)
    {
        auto& c = pointChips[i];
        c.setButtonText (juce::String (i + 1));
        c.setComponentID ("chip" + juce::String (i + 1));
        c.getProperties().set ("colour", (juce::int64) L::pointColour (i).getARGB());
        c.onClick = [this, i] { selectPoint (i); };
        c.setBounds (pointPanel.getX() + 84 + i * 42, pointPanel.getY() + 8, 38, 22);
        addAndMakeVisible (c);
    }

    const int px = pointPanel.getX(), py = pointPanel.getY();
    for (auto* s : { &posKnob, &pitchKnob, &gainKnob, &panKnob, &filterKnob, &revKnob, &dlyKnob })
    {
        s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible (*s);
    }
    const juce::String names[] = { "POSITION", "PITCH", "GAIN", "PAN", "FILTER", "REVERB", "DELAY" };
    juce::Slider* pointKnobs[] = { &posKnob, &pitchKnob, &gainKnob, &panKnob, &filterKnob, &revKnob, &dlyKnob };
    for (int i = 0; i < 7; ++i)
    {
        pointKnobs[i]->setName (names[i]);
        pointKnobs[i]->setBounds (px + 14 + i * 104, py + 104, 100, 120);
    }
    pitchKnob.getProperties().set ("bipolar", true);
    panKnob.getProperties().set ("bipolar", true);
    filterKnob.getProperties().set ("bipolar", true);
    posKnob.setComponentID ("pointPos");

    // --- Hasard ---
    const int rx = randomPanel.getX(), ry = randomPanel.getY();
    knob (pointsKnob, P::pointsId, { rx + 10, ry + 30, 92, 100 }, "POINTS");
    knob (seedKnob, P::seedId, { rx + 102, ry + 30, 92, 100 }, "SEED");
    knob (chanceKnob, P::chanceId, { rx + 194, ry + 30, 92, 100 }, "CHANCE");
    knob (reverseKnob, P::reverseId, { rx + 286, ry + 30, 92, 100 }, "REVERSE");
    knob (scatterKnob, P::scatterId, { rx + 378, ry + 30, 92, 100 }, "SCATTER");

    randomButton.setComponentID ("randomize");
    randomButton.setBounds (rx + 14, ry + 142, 140, 28);
    randomButton.onClick = [this] { showRandomMenu(); };
    spreadButton.setBounds (rx + 160, ry + 142, 92, 28);
    spreadButton.onClick = [this] { audioProcessor.spreadPointsEvenly(); };
    transientsButton.setBounds (rx + 258, ry + 142, 110, 28);
    transientsButton.onClick = [this] { audioProcessor.pointsOnTransients(); };
    diceButton.setBounds (rx + 374, ry + 142, 92, 28);
    diceButton.onClick = [this] { audioProcessor.setParam (P::seedId, (float) juce::Random().nextInt ({ 1, 10000 })); };
    for (auto* b : { &randomButton, &spreadButton, &transientsButton, &diceButton })
        addAndMakeVisible (*b);

    choiceSwitch (snapSwitch, P::snapId, { "OFF", "ONSETS", "ZERO X" }, { rx + 72, ry + 190, 270, 26 });
    toggle (lockButton, P::lockId, { rx + 374, ry + 190, 92, 26 });

    // --- Séquenceur ---
    const int sx = seqPanel.getX(), sy = seqPanel.getY();
    choiceSwitch (modeSwitch, P::modeId, { "GRID", "CYCLE", "RANDOM" }, { sx + 150, sy + 30, 210, 24 });
    choiceSwitch (triggerSwitch, P::triggerId, { "TRANSPORT", "NOTE" }, { sx + 376, sy + 30, 190, 24 });
    choiceMenu (rateMenu, P::rateId, { sx + 582, sy + 30, 72, 24 }, "rate");
    toggle (syncButton, P::syncId, { sx + 660, sy + 30, 64, 24 });
    patternButton.setBounds (sx + 776, sy + 30, 110, 24);
    patternButton.onClick = [this] { showPatternMenu(); };
    addAndMakeVisible (patternButton);

    addAndMakeVisible (grid);
    grid.setBounds (sx + 12, sy + 64, seqPanel.getWidth() - 24, 82);
    grid.selectedPoint = [this] { return selectedPoint; };

    knob (stepsKnob, P::stepsId, { sx + 14, sy + 152, 96, 94 }, "STEPS");
    knob (freeKnob, P::freeMsId, { sx + 110, sy + 152, 96, 94 }, "FREE TIME");
    knob (driftKnob, P::driftId, { sx + 206, sy + 152, 96, 94 }, "DRIFT");
    knob (swingKnob, P::swingId, { sx + 302, sy + 152, 96, 94 }, "SWING");
    knob (gateKnob, P::gateId, { sx + 398, sy + 152, 96, 94 }, "GATE");
    knob (fadeKnob, P::fadeId, { sx + 494, sy + 152, 96, 94 }, "FADE");

    // --- Effets ---
    const int fx = fxPanel.getX(), fy = fxPanel.getY();
    knob (revSizeKnob, P::revSizeId, { fx + 6, fy + 26, 82, 88 }, "SIZE");
    knob (revDampKnob, P::revDampId, { fx + 88, fy + 26, 82, 88 }, "DAMP");
    knob (revWidthKnob, P::revWidthId, { fx + 170, fy + 26, 82, 88 }, "WIDTH");
    knob (revLevelKnob, P::revLevelId, { fx + 252, fy + 26, 82, 88 }, "LEVEL");
    choiceMenu (dlyTimeMenu, P::dlyTimeId, { fx + 70, fy + 124, 70, 22 }, "dlyTime");
    toggle (pingButton, P::dlyPingId, { fx + 146, fy + 124, 96, 22 });
    toggle (limiterButton, P::limiterId, { fx + 266, fy + 124, 64, 22 });
    knob (dlyFbKnob, P::dlyFbId, { fx + 6, fy + 152, 82, 92 }, "FEEDBACK");
    knob (dlyToneKnob, P::dlyToneId, { fx + 88, fy + 152, 82, 92 }, "TONE");
    knob (dlyLevelKnob, P::dlyLevelId, { fx + 170, fy + 152, 82, 92 }, "LEVEL");
    knob (gainKnobOut, P::gainId, { fx + 252, fy + 152, 82, 92 }, "OUTPUT", true);

    // --- Presets ---
    addAndMakeVisible (presetBar);
    presetBar.setBounds (width - 14 - phragma::PresetBar::idealWidth - 40, 14, phragma::PresetBar::idealWidth + 40, 28);
    presetBar.attachSavePanelTo (*this);

    selectPoint (0);
    refreshSlotTabs();
    startTimerHz (20);
}

PhreakFacade::~PhreakFacade()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PhreakFacade::knob (juce::Slider& s, const juce::String& id, juce::Rectangle<int> area, const juce::String& name, bool bipolar)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setName (name);
    s.setComponentID (id);
    if (bipolar) s.getProperties().set ("bipolar", true);
    s.setBounds (area);
    addAndMakeVisible (s);
    sliderAttachments.push_back (std::make_unique<APVTS::SliderAttachment> (audioProcessor.apvts, id, s));
}

void PhreakFacade::toggle (juce::TextButton& b, const juce::String& id, juce::Rectangle<int> area)
{
    b.setClickingTogglesState (true);
    b.setComponentID (id);
    b.setBounds (area);
    addAndMakeVisible (b);
    buttonAttachments.push_back (std::make_unique<APVTS::ButtonAttachment> (audioProcessor.apvts, id, b));
}

void PhreakFacade::choiceSwitch (ChoiceSwitch& sw, const juce::String& id, const juce::StringArray& texts, juce::Rectangle<int> area)
{
    auto* param = audioProcessor.apvts.getParameter (id);
    const bool fresh = sw.buttons.isEmpty();
    if (fresh)
    {
        const int n = texts.size();
        const int w = area.getWidth() / n;
        for (int i = 0; i < n; ++i)
        {
            auto* b = sw.buttons.add (new juce::TextButton (texts[i]));
            b->setBounds (area.getX() + i * w, area.getY(), w - 2, area.getHeight());
            addAndMakeVisible (b);
        }
    }
    for (int i = 0; i < sw.buttons.size(); ++i)
    {
        sw.buttons[i]->setComponentID (id + "_" + juce::String (i));
        sw.buttons[i]->onClick = [&sw, i] { if (sw.attachment) sw.attachment->setValueAsCompleteGesture ((float) i); };
    }
    sw.attachment = std::make_unique<juce::ParameterAttachment> (*param, [&sw] (float v)
    {
        const int idx = juce::roundToInt (v);
        for (int i = 0; i < sw.buttons.size(); ++i)
            sw.buttons[i]->setToggleState (i == idx, juce::dontSendNotification);
    });
    sw.attachment->sendInitialUpdate();
}

void PhreakFacade::choiceMenu (ChoiceMenu& m, const juce::String& id, juce::Rectangle<int> area, const juce::String& componentId)
{
    auto* param = audioProcessor.apvts.getParameter (id);
    if (m.button.getParentComponent() == nullptr)
    {
        m.button.setBounds (area);
        m.button.setComponentID (componentId);
        addAndMakeVisible (m.button);
    }
    m.button.onClick = [this, &m, param]
    {
        juce::PopupMenu menu;
        const auto items = choicesOf (param);
        const int current = juce::roundToInt (param->convertFrom0to1 (param->getValue()));
        for (int i = 0; i < items.size(); ++i)
            menu.addItem (i + 1, items[i], true, i == current);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&m.button), [&m] (int r)
        {
            if (r > 0 && m.attachment) m.attachment->setValueAsCompleteGesture ((float) (r - 1));
        });
    };
    m.attachment = std::make_unique<juce::ParameterAttachment> (*param, [&m, param] (float v)
    {
        m.button.setButtonText (choicesOf (param)[juce::roundToInt (v)]);
    });
    m.attachment->sendInitialUpdate();
}

void PhreakFacade::bindPointControls()
{
    const int p = selectedPoint;
    auto& apvts = audioProcessor.apvts;
    posA.reset(); pitchA.reset(); gainA.reset(); panA.reset(); filterA.reset(); revA.reset(); dlyA.reset();
    posA    = std::make_unique<APVTS::SliderAttachment> (apvts, P::pointId (p, "Pos"), posKnob);
    pitchA  = std::make_unique<APVTS::SliderAttachment> (apvts, P::pointId (p, "Pitch"), pitchKnob);
    gainA   = std::make_unique<APVTS::SliderAttachment> (apvts, P::pointId (p, "Gain"), gainKnob);
    panA    = std::make_unique<APVTS::SliderAttachment> (apvts, P::pointId (p, "Pan"), panKnob);
    filterA = std::make_unique<APVTS::SliderAttachment> (apvts, P::pointId (p, "Filter"), filterKnob);
    revA    = std::make_unique<APVTS::SliderAttachment> (apvts, P::pointId (p, "Rev"), revKnob);
    dlyA    = std::make_unique<APVTS::SliderAttachment> (apvts, P::pointId (p, "Dly"), dlyKnob);

    const int px = pointPanel.getX(), py = pointPanel.getY();
    choiceSwitch (pointSampleSwitch, P::pointId (p, "Sample"), { "1", "2", "3", "4" }, { px + 14, py + 58, 136, 26 });
    choiceSwitch (dirSwitch, P::pointId (p, "Dir"), { "FWD", "REV", "PING" }, { px + 166, py + 58, 186, 26 });
    choiceMenu (stutterMenu, P::pointId (p, "Stutter"), { px + 368, py + 58, 100, 26 }, "stutter");
    choiceSwitch (tailSwitch, P::pointId (p, "Tail"), { "KEEP", "CUT" }, { px + 484, py + 58, 130, 26 });

    for (auto* s : { &posKnob, &pitchKnob, &gainKnob, &panKnob, &filterKnob, &revKnob, &dlyKnob })
        s->getProperties().set ("colour", (juce::int64) L::pointColour (p).getARGB());
    repaint();
}

void PhreakFacade::selectPoint (int pt)
{
    selectedPoint = juce::jlimit (0, 15, pt);
    for (int i = 0; i < 16; ++i)
        pointChips[i].setToggleState (i == selectedPoint, juce::dontSendNotification);
    bindPointControls();
    lastPointSlot = juce::roundToInt (audioProcessor.getParam (P::pointId (selectedPoint, "Sample")));
    showSlot (lastPointSlot);
    wave.setSelectedPoint (selectedPoint);
    grid.repaint();
}

void PhreakFacade::showSlot (int slot)
{
    shownSlot = juce::jlimit (0, 3, slot);
    wave.setSlot (shownSlot);
    refreshSlotTabs();
}

void PhreakFacade::refreshSlotTabs()
{
    for (int s = 0; s < 4; ++s)
    {
        const auto& smp = audioProcessor.getSample (s);
        juce::String t = juce::String (s + 1) + "   ";
        t << (smp.isLoaded() ? smp.name : (smp.missing ? "MISSING" : juce::String ("EMPTY")));
        slotTabs[s].setButtonText (t);
        slotTabs[s].setToggleState (s == shownSlot, juce::dontSendNotification);
    }
    wave.repaint();
}

void PhreakFacade::loadIntoSlot (int slot)
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sample into slot " + juce::String (slot + 1), juce::File(),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    juce::Component::SafePointer<PhreakFacade> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, slot] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto f = fc.getResult();
        if (f.existsAsFile())
        {
            juce::String error;
            if (! safe->audioProcessor.loadSample (slot, f, &error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Phreak", error);
        }
        safe->refreshSlotTabs();
    });
}

void PhreakFacade::showRandomMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("RANDOMIZE POINTS 1 > " + juce::String (juce::roundToInt (audioProcessor.getParam (P::pointsId))));
    m.addItem (1, "Positions");
    m.addItem (2, "Positions + samples");
    m.addItem (3, "Positions + direction + stutter");
    m.addItem (4, "Everything (+ pitch, FX, pan, tails)");
    m.addItem (5, "Only FX / pan / filter");
    m.addSeparator();
    m.addItem (6, "Pattern (grid)");
    juce::Component::SafePointer<PhreakFacade> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&randomButton), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& ap = safe->audioProcessor;
        switch (r)
        {
            case 1: ap.randomizePoints (P::rndPositions); break;
            case 2: ap.randomizePoints (P::rndPositions | P::rndSamples); break;
            case 3: ap.randomizePoints (P::rndPositions | P::rndDirections | P::rndStutter); break;
            case 4: ap.randomizePoints (P::rndPositions | P::rndSamples | P::rndDirections | P::rndStutter | P::rndPitch | P::rndFx | P::rndTails); break;
            case 5: ap.randomizePoints (P::rndFx | P::rndTails); break;
            case 6: ap.randomizePattern(); break;
            default: break;
        }
        safe->selectPoint (safe->selectedPoint);
    });
}

void PhreakFacade::showPatternMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Randomize pattern");
    m.addItem (2, "Reset : 1 > N in a row");
    m.addItem (3, "Only point " + juce::String (selectedPoint + 1) + " on the beats, TIE between");
    m.addItem (4, "All REST");
    juce::Component::SafePointer<PhreakFacade> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patternButton), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& ap = safe->audioProcessor;
        if (r == 1) ap.randomizePattern();
        if (r == 2) ap.resetPattern();
        if (r == 3)
            for (int k = 0; k < P::numSteps; ++k)
                ap.setParam (P::stepId (k), (float) (k % 4 == 0 ? safe->selectedPoint + 1 : phreak::stepTie));
        if (r == 4)
            for (int k = 0; k < P::numSteps; ++k)
                ap.setParam (P::stepId (k), (float) phreak::stepRest);
        safe->grid.repaint();
    });
}

void PhreakFacade::timerCallback()
{
    const int gen = audioProcessor.getSampleGeneration();
    if (gen != lastGeneration)
    {
        lastGeneration = gen;
        refreshSlotTabs();
    }

    const int active = juce::roundToInt (audioProcessor.getParam (P::pointsId));
    const int playing = audioProcessor.getEngine().displayPoint.load();
    for (int i = 0; i < 16; ++i)
    {
        pointChips[i].setAlpha (i < active ? 1.0f : 0.45f);
        const bool lit = i == playing;
        if ((bool) pointChips[i].getProperties().getWithDefault ("lit", false) != lit)
        {
            pointChips[i].getProperties().set ("lit", lit);
            pointChips[i].setButtonText (lit ? "> " + juce::String (i + 1) : juce::String (i + 1));
        }
    }

    // le point choisi a changé de sample (SAMPLE, randomize, automation) : on affiche ce sample
    const int slotOfPoint = juce::roundToInt (audioProcessor.getParam (P::pointId (selectedPoint, "Sample")));
    if (slotOfPoint != lastPointSlot)
    {
        lastPointSlot = slotOfPoint;
        if (slotOfPoint != shownSlot && ! wave.isMouseButtonDown())
            showSlot (slotOfPoint);
    }

    const float peak = audioProcessor.takeOutputPeak();
    const float db = juce::Decibels::gainToDecibels (peak, -60.0f);
    outLevel = db > outLevel ? db : juce::jmax (-60.0f, outLevel - 1.5f);
    repaint (fxPanel.getRight() - 24, fxPanel.getY() + 6, 16, 20);
    grid.repaint();
    wave.repaint();
}

void PhreakFacade::paint (juce::Graphics& g)
{
    g.fillAll (L::ground());

    // Titre
    const auto titleFont = lookAndFeel.font (30.0f, true, 0.22f);
    g.setFont (titleFont);
    g.setGradientFill (juce::ColourGradient (L::accent(), 20.0f, 0.0f, L::lilac(), 200.0f, 0.0f, false));
    g.drawText ("PHREAK", 20, 10, 220, 36, juce::Justification::centredLeft, false);
    g.setColour (L::dim());
    g.setFont (lookAndFeel.font (11.0f, true, 0.2f));
    g.drawText ("GLITCH SAMPLE SEQUENCER  |  PHRAGMA", 196, 10, 400, 36, juce::Justification::centredLeft, false);

    g.setFont (lookAndFeel.font (9.5f, true, 0.14f));
    g.drawText ("MIDI   C1 > D#2 = POINTS 1 > 16", 540, 12, 310, 14, juce::Justification::centredLeft, false);
    g.drawText ("OTHER NOTES = TRANSPOSE (C3 = 0)", 540, 28, 310, 14, juce::Justification::centredLeft, false);

    const auto panelFont = lookAndFeel.font (12.0f, true, 0.2f);
    L::drawPanel (g, samplePanel.toFloat(), "SAMPLES", panelFont);
    L::drawPanel (g, pointPanel.toFloat(), "POINT", panelFont);
    L::drawPanel (g, randomPanel.toFloat(), "RANDOM", panelFont);
    L::drawPanel (g, seqPanel.toFloat(), "SEQUENCER", panelFont);
    L::drawPanel (g, fxPanel.toFloat(), "REVERB", panelFont);

    // Petites étiquettes
    g.setFont (lookAndFeel.font (9.5f, true, 0.18f));
    g.setColour (L::dim());
    auto label = [&] (const juce::String& t, int x, int y, int w = 120)
    {
        g.drawText (t, x, y, w, 14, juce::Justification::centredLeft, false);
    };
    const int px = pointPanel.getX(), py = pointPanel.getY();
    label ("SAMPLE", px + 14, py + 42);
    label ("DIRECTION", px + 166, py + 42);
    label ("STUTTER", px + 368, py + 42);
    label ("FX TAIL", px + 484, py + 42);


    label ("SNAP", randomPanel.getX() + 18, randomPanel.getY() + 196);
    label ("MODE", seqPanel.getX() + 150, seqPanel.getY() + 14);
    label ("PLAY WITH", seqPanel.getX() + 376, seqPanel.getY() + 14);
    label ("RATE", seqPanel.getX() + 582, seqPanel.getY() + 14);
    g.drawFittedText ("GRID :  CLICK = SELECTED POINT\nALT = TIE (-)    SHIFT = REST (x)\nDRAG = CHANGE    RIGHT-CLICK = MENU",
                      seqPanel.getX() + 606, seqPanel.getY() + 172, 290, 56, juce::Justification::topLeft, 3, 1.0f);

    g.setFont (lookAndFeel.font (12.0f, true, 0.2f));
    g.setColour (L::lilac());
    g.drawText ("DELAY", fxPanel.getX() + 12, fxPanel.getY() + 128, 60, 16, juce::Justification::centredLeft, false);

    // Indicateur de sortie
    auto meter = juce::Rectangle<float> ((float) fxPanel.getRight() - 22.0f, (float) fxPanel.getY() + 8.0f, 10.0f, 16.0f);
    g.setColour (juce::Colour (0xff081014));
    g.fillRect (meter);
    const float lvl = juce::jlimit (0.0f, 1.0f, (outLevel + 60.0f) / 60.0f);
    g.setColour (outLevel > -0.5f ? juce::Colour (0xffff5a6e) : L::accent());
    g.fillRect (meter.withTop (meter.getBottom() - meter.getHeight() * lvl));
}

juce::Component* PhreakFacade::findControl (const juce::String& componentId)
{
    std::function<juce::Component* (juce::Component&)> find = [&] (juce::Component& c) -> juce::Component*
    {
        if (c.getComponentID() == componentId) return &c;
        for (auto* child : c.getChildren())
            if (auto* f = find (*child)) return f;
        return nullptr;
    };
    return find (*this);
}

//==============================================================================
PhreakAudioProcessorEditor::PhreakAudioProcessorEditor (PhreakAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), facade (p)
{
    addAndMakeVisible (facade);

    const int savedWidth = (int) audioProcessor.apvts.state.getProperty ("editorWidth", PhreakFacade::width);

    setResizable (true, true);
    setResizeLimits (juce::roundToInt (PhreakFacade::width * 0.6), juce::roundToInt (PhreakFacade::height * 0.6),
                     juce::roundToInt (PhreakFacade::width * 1.5), juce::roundToInt (PhreakFacade::height * 1.5));
    getConstrainer()->setFixedAspectRatio ((double) PhreakFacade::width / (double) PhreakFacade::height);

    facade.getPresetBar().getScale = [this] { return getUiScale(); };
    facade.getPresetBar().setScale = [this] (float scale) { setUiScale (scale); };

    const int shownWidth = juce::jlimit (juce::roundToInt (PhreakFacade::width * 0.6), juce::roundToInt (PhreakFacade::width * 1.5), savedWidth);
    setSize (shownWidth, juce::roundToInt ((double) shownWidth * PhreakFacade::height / PhreakFacade::width));
    sized = true;
    resized();
}

PhreakAudioProcessorEditor::~PhreakAudioProcessorEditor() = default;

void PhreakAudioProcessorEditor::setUiScale (float scale)
{
    scale = juce::jlimit (0.6f, 1.5f, scale);
    setSize (juce::roundToInt ((float) PhreakFacade::width * scale), juce::roundToInt ((float) PhreakFacade::height * scale));
}

void PhreakAudioProcessorEditor::resized()
{
    facade.setTransform (juce::AffineTransform::scale (getUiScale()));
    facade.getPresetBar().refresh();

    if (sized)
        audioProcessor.apvts.state.setProperty ("editorWidth", getWidth(), nullptr);
}
