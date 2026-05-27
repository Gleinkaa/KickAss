#include "KickAssLookAndFeel.h"

//==============================================================================
KickAssLookAndFeel::KickAssLookAndFeel()
{
    // ComboBox / popup defaults
    setColour (juce::ComboBox::backgroundColourId, KickColors::panel);
    setColour (juce::ComboBox::outlineColourId,    KickColors::accentHot.withAlpha (0.30f));
    setColour (juce::ComboBox::arrowColourId,      KickColors::accentHot);
    setColour (juce::ComboBox::textColourId,       KickColors::textBright);
    setColour (juce::ComboBox::buttonColourId,     KickColors::panel);

    setColour (juce::PopupMenu::backgroundColourId,        KickColors::panelHi);
    setColour (juce::PopupMenu::textColourId,              KickColors::textBright);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, KickColors::accentHot);
    setColour (juce::PopupMenu::highlightedTextColourId,   juce::Colours::white);

    // TextButton defaults
    setColour (juce::TextButton::buttonColourId,     KickColors::panel);
    setColour (juce::TextButton::buttonOnColourId,   KickColors::accentHot);
    setColour (juce::TextButton::textColourOffId,    KickColors::textBright);
    setColour (juce::TextButton::textColourOnId,     juce::Colours::white);

    // Label
    setColour (juce::Label::textColourId, KickColors::textDim);

    // Slider text box
    setColour (juce::Slider::textBoxTextColourId,        KickColors::textBright);
    setColour (juce::Slider::textBoxBackgroundColourId,  juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId,     juce::Colours::transparentBlack);
}

//==============================================================================
void KickAssLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float startAngle, float endAngle,
                                          juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const float diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const float radius = diameter * 0.5f;
    const auto centre = bounds.getCentre();
    const float trackWidth = 3.0f;

    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    // 1. Underglow halo on active arc (the SnareGen trick, widened)
    {
        juce::Path haloArc;
        haloArc.addCentredArc (centre.x, centre.y,
                               radius - trackWidth * 0.5f, radius - trackWidth * 0.5f,
                               0.0f, startAngle, angle, true);
        g.setColour (KickColors::accentHot.withAlpha (0.18f));
        g.strokePath (haloArc, juce::PathStrokeType (8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // 2. Track (full sweep, dim)
    {
        juce::Path track;
        track.addCentredArc (centre.x, centre.y,
                             radius - trackWidth * 0.5f, radius - trackWidth * 0.5f,
                             0.0f, startAngle, endAngle, true);
        g.setColour (KickColors::gridLine);
        g.strokePath (track, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // 3. Active arc
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y,
                           radius - trackWidth * 0.5f, radius - trackWidth * 0.5f,
                           0.0f, startAngle, angle, true);
        g.setColour (KickColors::accentHot);
        g.strokePath (arc, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // 4. Centre disc
    const float discR = radius - trackWidth - 4.0f;
    g.setColour (KickColors::panel);
    g.fillEllipse (centre.x - discR, centre.y - discR, discR * 2.0f, discR * 2.0f);
    g.setColour (KickColors::accentHot.withAlpha (0.30f));
    g.drawEllipse (centre.x - discR, centre.y - discR, discR * 2.0f, discR * 2.0f, 1.0f);

    // 5. Indicator dot
    const float dotR = 2.5f;
    const float indR = discR - 4.0f;
    const float dotX = centre.x + indR * std::cos (angle - juce::MathConstants<float>::halfPi);
    const float dotY = centre.y + indR * std::sin (angle - juce::MathConstants<float>::halfPi);
    g.setColour (KickColors::accentHot);
    g.fillEllipse (dotX - dotR, dotY - dotR, dotR * 2.0f, dotR * 2.0f);

    juce::ignoreUnused (slider);
}

//==============================================================================
void KickAssLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                           bool isHighlighted, bool isDown)
{
    auto bounds = button.getLocalBounds().toFloat();
    const float boxSize = juce::jmin (16.0f, bounds.getHeight() - 4.0f);
    auto box = juce::Rectangle<float> (bounds.getX() + 2.0f,
                                       bounds.getY() + (bounds.getHeight() - boxSize) * 0.5f,
                                       boxSize, boxSize);

    // Box background
    g.setColour (button.getToggleState() ? KickColors::accentHot : KickColors::panel);
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (KickColors::accentHot.withAlpha (isHighlighted ? 0.70f : 0.40f));
    g.drawRoundedRectangle (box, 3.0f, 1.0f);

    // Checkmark when on
    if (button.getToggleState())
    {
        juce::Path tick;
        tick.startNewSubPath (box.getX() + boxSize * 0.25f, box.getCentreY());
        tick.lineTo          (box.getX() + boxSize * 0.45f, box.getY() + boxSize * 0.72f);
        tick.lineTo          (box.getX() + boxSize * 0.78f, box.getY() + boxSize * 0.28f);
        g.setColour (juce::Colours::white);
        g.strokePath (tick, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Label
    g.setColour (KickColors::textBright);
    g.setFont (KickFonts::ui (12.0f));
    auto textBounds = bounds.withTrimmedLeft (boxSize + 10.0f);
    g.drawText (button.getButtonText(), textBounds.toNearestInt(),
                juce::Justification::centredLeft, false);

    juce::ignoreUnused (isDown);
}

//==============================================================================
void KickAssLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                               const juce::Colour& bgColour,
                                               bool isHighlighted, bool isDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    const float radius = 5.0f;

    juce::Colour fill = bgColour;
    if (button.getToggleState()) fill = KickColors::accentHot;
    if (isHighlighted)           fill = fill.brighter (0.10f);
    if (isDown)                  fill = fill.darker (0.15f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, radius);

    g.setColour (KickColors::accentHot.withAlpha (button.getToggleState() ? 0.0f : 0.30f));
    g.drawRoundedRectangle (bounds, radius, 1.0f);
}

//==============================================================================
juce::Font KickAssLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return KickFonts::ui (juce::jmin (15.0f, buttonHeight * 0.50f), true);
}

juce::Font KickAssLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return KickFonts::ui (12.0f);
}

juce::Font KickAssLookAndFeel::getLabelFont (juce::Label&)
{
    return KickFonts::ui (11.0f);
}

juce::Font KickAssLookAndFeel::getPopupMenuFont()
{
    return KickFonts::ui (12.0f);
}

//==============================================================================
void KickAssLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                      int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
                                      juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (1.0f);
    const float radius = 4.0f;

    g.setColour (isButtonDown ? KickColors::panelHi : KickColors::panel);
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (KickColors::accentHot.withAlpha (0.30f));
    g.drawRoundedRectangle (bounds, radius, 1.0f);

    // Dropdown chevron
    juce::Path arrow;
    const float ax = (float) width - 14.0f;
    const float ay = (float) height * 0.5f;
    arrow.startNewSubPath (ax, ay - 2.5f);
    arrow.lineTo          (ax + 5.0f, ay + 3.0f);
    arrow.lineTo          (ax + 10.0f, ay - 2.5f);
    g.setColour (KickColors::accentHot);
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::ignoreUnused (box);
}
