#include "PluginEditor.h"

//==============================================================================
// Color palette (per ARCHITECTURE.md §5)
namespace KickColors
{
    const juce::Colour bg          { 0xff0c0c10 };
    const juce::Colour panel       { 0xff15151c };
    const juce::Colour canvasBg    { 0xff07070a };
    const juce::Colour accentHot   { 0xffff2266 };
    const juce::Colour textBright  { 0xffe8e8f0 };
    const juce::Colour textDim     { 0xff7a7a8a };
    const juce::Colour textGhost   { 0xff4a4a55 };
}

//==============================================================================
KickAssEditor::KickAssEditor (KickAssProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setSize (1000, 680);
}

void KickAssEditor::paint (juce::Graphics& g)
{
    using namespace KickColors;

    g.fillAll (bg);

    auto bounds = getLocalBounds();

    // ---- Header strip (52 px) ----
    auto header = bounds.removeFromTop (52);
    g.setColour (panel);
    g.fillRect (header);

    // Wordmark
    auto wordmarkArea = header.reduced (16, 0);
    g.setFont (juce::Font (28.0f, juce::Font::bold));
    // "KICK" in textBright then "ASS" in accentHot, drawn side by side
    juce::Font wordFont (28.0f, juce::Font::bold);
    g.setFont (wordFont);
    const juce::String kick = "KICK";
    const juce::String ass  = "ASS";
    const int kickW = juce::Font (wordFont).getStringWidth (kick);
    const int assW  = juce::Font (wordFont).getStringWidth (ass);
    g.setColour (textBright);
    g.drawText (kick, wordmarkArea.getX(), wordmarkArea.getY() + 4,
                kickW + 8, wordmarkArea.getHeight() - 8, juce::Justification::centredLeft);
    g.setColour (accentHot);
    g.drawText (ass, wordmarkArea.getX() + kickW + 2, wordmarkArea.getY() + 4,
                assW + 8, wordmarkArea.getHeight() - 8, juce::Justification::centredLeft);

    // Subtitle
    g.setColour (textGhost);
    g.setFont (juce::Font (11.0f, juce::Font::italic));
    g.drawText ("by Gleinkaa",
                wordmarkArea.getX(), wordmarkArea.getBottom() - 18,
                200, 16, juce::Justification::centredLeft);

    // Header divider hairline
    g.setColour (accentHot.withAlpha (0.15f));
    g.drawHorizontalLine (header.getBottom(), 0.0f, (float) getWidth());

    // ---- Placeholder for visualizer area ----
    auto vizArea = bounds.removeFromTop (340).reduced (16, 8);
    g.setColour (canvasBg);
    g.fillRoundedRectangle (vizArea.toFloat(), 8.0f);
    g.setColour (accentHot.withAlpha (0.20f));
    g.drawRoundedRectangle (vizArea.toFloat(), 8.0f, 1.0f);
    g.setColour (textDim);
    g.setFont (juce::Font (14.0f, juce::Font::italic));
    g.drawText ("[ visualizer — Phase 4 ]", vizArea, juce::Justification::centred);

    // ---- Placeholder for param panels area ----
    auto paramsArea = bounds.removeFromTop (224).reduced (16, 8);
    g.setColour (panel);
    g.fillRoundedRectangle (paramsArea.toFloat(), 8.0f);
    g.setColour (textDim);
    g.drawText ("[ PITCH | AMP | SCOOP | TRANSIENT | DRIVE | MASTER — Phase 3 ]",
                paramsArea, juce::Justification::centred);

    // ---- Placeholder for footer ----
    auto footer = bounds.reduced (16, 8);
    g.setColour (panel);
    g.fillRoundedRectangle (footer.toFloat(), 6.0f);
    g.setColour (textGhost);
    g.setFont (juce::Font (11.0f, juce::Font::plain));
    g.drawText ("Phase 1 scaffold — silence. " + juce::String (processorRef.apvts.state.getNumChildren())
                  + " state children. " + juce::String (25) + " parameters wired.",
                footer, juce::Justification::centred);
}

void KickAssEditor::resized()
{
    // Phase 3 lays out child components here.
}
