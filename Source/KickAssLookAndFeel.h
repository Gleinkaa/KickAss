#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
// Centralised palette — keep ARCHITECTURE.md §5 in sync.
namespace KickColors
{
    inline const juce::Colour bg          { 0xff0c0c10 };
    inline const juce::Colour panel       { 0xff15151c };
    inline const juce::Colour panelHi     { 0xff1c1c26 };
    inline const juce::Colour canvasBg    { 0xff07070a };
    inline const juce::Colour gridLine    { 0xff1e1e2a };
    inline const juce::Colour gridBeat    { 0xff2c2c40 };
    inline const juce::Colour accentHot   { 0xffff2266 };
    inline const juce::Colour envAmp      { 0xffffcc00 };
    inline const juce::Colour envPitch    { 0xff00d4ff };
    inline const juce::Colour envScoop    { 0xffb388ff };
    inline const juce::Colour spectrum    { 0xff00ff88 };
    inline const juce::Colour textBright  { 0xffe8e8f0 };
    inline const juce::Colour textDim     { 0xff7a7a8a };
    inline const juce::Colour textGhost   { 0xff4a4a55 };
    inline const juce::Colour warn        { 0xffff8844 };
    inline const juce::Colour ok          { 0xff00e676 };
}

//==============================================================================
// Font factory — single place to bump if/when we switch to bundled BinaryData fonts.
// Using FontOptions = JUCE 8 portable, no deprecation warnings, works on macOS.
namespace KickFonts
{
    inline juce::Font ui (float height, bool bold = false)
    {
        juce::FontOptions opts (height);
        if (bold) opts = opts.withStyle ("Bold");
        return juce::Font (opts);
    }
    inline juce::Font uiItalic (float height)
    {
        return juce::Font (juce::FontOptions (height).withStyle ("Italic"));
    }
    inline juce::Font mono (float height)
    {
        // Cross-platform monospace: Consolas ships on Windows, Menlo on macOS.
        // Anything else falls back to the system default monospaced face.
       #if JUCE_WINDOWS
        const juce::String face ("Consolas");
       #elif JUCE_MAC
        const juce::String face ("Menlo");
       #else
        const juce::String face (juce::Font::getDefaultMonospacedFontName());
       #endif
        return juce::Font (juce::FontOptions (face, height, juce::Font::plain));
    }
}

//==============================================================================
class KickAssLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KickAssLookAndFeel();

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override;

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getPopupMenuFont() override;

    void drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox& box) override;
};
