#include "PluginEditor.h"

//==============================================================================
// ParamPanel — labeled section background. Children are positioned by KickAssEditor.
//==============================================================================
void ParamPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (KickColors::panel);
    g.fillRoundedRectangle (bounds, 8.0f);

    g.setColour (KickColors::accentHot.withAlpha (0.12f));
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

    // Header strip inside the panel
    g.setColour (KickColors::accentHot.withAlpha (0.70f));
    g.setFont (KickFonts::ui (10.0f, true));
    g.drawText (sectionTitle,
                bounds.toNearestInt().reduced (12, 6).withHeight (16),
                juce::Justification::centredLeft);

    // Header divider
    g.setColour (KickColors::accentHot.withAlpha (0.15f));
    g.drawHorizontalLine (24, bounds.getX() + 8.0f, bounds.getRight() - 8.0f);
}

//==============================================================================
KickAssEditor::KickAssEditor (KickAssProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setLookAndFeel (&lnf);
    setSize (1000, 680);

    // Attach all panels
    addAndMakeVisible (visualizer);

    addAndMakeVisible (pitchPanel);
    addAndMakeVisible (ampPanel);
    addAndMakeVisible (scoopPanel);
    addAndMakeVisible (transientPanel);
    addAndMakeVisible (drivePanel);
    addAndMakeVisible (masterPanel);

    // ---- PITCH ----
    setupKnob (startFreq,  "start_freq",   "Start");
    setupKnob (midFreq,    "mid_freq",     "Mid");
    setupKnob (endFreq,    "end_freq",     "End");
    setupKnob (sweepTime1, "sweep_time_1", "Sweep 1");
    setupKnob (sweepTime2, "sweep_time_2", "Sweep 2");
    setupKnob (pitchCurve, "pitch_curve",  "Curve");
    for (auto* k : { &startFreq, &midFreq, &endFreq, &sweepTime1, &sweepTime2, &pitchCurve })
    {
        pitchPanel.addAndMakeVisible (k->slider);
        pitchPanel.addAndMakeVisible (k->label);
    }

    // ---- AMP ----
    setupKnob (volAttack,  "vol_attack",  "Attack");
    setupKnob (volHold,    "vol_hold",    "Hold");
    setupKnob (volDecay1,  "vol_decay_1", "Decay 1");
    setupKnob (volSustain, "vol_sustain", "Sustain");
    setupKnob (volDecay2,  "vol_decay_2", "Decay 2");
    setupKnob (volCurve,   "vol_curve",   "Curve");
    for (auto* k : { &volAttack, &volHold, &volDecay1, &volSustain, &volDecay2, &volCurve })
    {
        ampPanel.addAndMakeVisible (k->slider);
        ampPanel.addAndMakeVisible (k->label);
    }

    // ---- SCOOP ----
    setupKnob (scoopStart,  "scoop_start",  "Start");
    setupKnob (scoopLength, "scoop_length", "Length");
    setupKnob (scoopDepth,  "scoop_depth",  "Depth");
    for (auto* k : { &scoopStart, &scoopLength, &scoopDepth })
    {
        scoopPanel.addAndMakeVisible (k->slider);
        scoopPanel.addAndMakeVisible (k->label);
    }

    // ---- TRANSIENT ----
    setupChoice (clickType, "click_type", "Source", { "Sine", "Noise", "Both" });
    setupKnob (clickVol,   "click_vol",   "Level");
    setupKnob (clickHpf,   "click_hpf",   "HPF");
    setupKnob (clickTone,  "click_tone",  "Tone");
    setupKnob (clickDecay, "click_decay", "Decay");
    transientPanel.addAndMakeVisible (clickType.combo);
    transientPanel.addAndMakeVisible (clickType.label);
    for (auto* k : { &clickVol, &clickHpf, &clickTone, &clickDecay })
    {
        transientPanel.addAndMakeVisible (k->slider);
        transientPanel.addAndMakeVisible (k->label);
    }

    // ---- DRIVE ----
    setupKnob (drive,     "drive",      "Base");
    setupKnob (tailDrive, "tail_drive", "Tail");
    for (auto* k : { &drive, &tailDrive })
    {
        drivePanel.addAndMakeVisible (k->slider);
        drivePanel.addAndMakeVisible (k->label);
    }

    // ---- MASTER ----
    setupToggle (invertPhase, "invert_phase", "Invert Phase");
    setupKnob (outputGain,  "output_gain",  "Output");
    setupKnob (pitchTrack,  "pitch_track",  "Pitch Track");
    masterPanel.addAndMakeVisible (invertPhase.button);
    for (auto* k : { &outputGain, &pitchTrack })
    {
        masterPanel.addAndMakeVisible (k->slider);
        masterPanel.addAndMakeVisible (k->label);
    }

    // ---- Footer buttons ----
    addAndMakeVisible (playBtn);
    addAndMakeVisible (exportBtn);
    addAndMakeVisible (abBtn);
    playBtn.onClick   = [this] { triggerPreviewNote(); };
    exportBtn.onClick = [] { /* Phase 6 */ };
    abBtn.onClick     = [] { /* Phase 6 */ };

    resized();
}

KickAssEditor::~KickAssEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void KickAssEditor::setupKnob (KnobControl& kc, const juce::String& paramId, const juce::String& display)
{
    kc.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    kc.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 14);
    kc.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    kc.slider.setLookAndFeel (&lnf);

    kc.label.setText (display, juce::dontSendNotification);
    kc.label.setJustificationType (juce::Justification::centredTop);
    kc.label.setFont (KickFonts::ui (10.0f));
    kc.label.setColour (juce::Label::textColourId, KickColors::textDim);

    kc.attachment.reset (
        new juce::AudioProcessorValueTreeState::SliderAttachment (processorRef.apvts, paramId, kc.slider));
}

void KickAssEditor::setupChoice (ChoiceControl& cc, const juce::String& paramId,
                                const juce::String& display, const juce::StringArray& items)
{
    cc.combo.addItemList (items, 1);
    cc.combo.setJustificationType (juce::Justification::centred);

    cc.label.setText (display, juce::dontSendNotification);
    cc.label.setJustificationType (juce::Justification::centredTop);
    cc.label.setFont (KickFonts::ui (10.0f));
    cc.label.setColour (juce::Label::textColourId, KickColors::textDim);

    cc.attachment.reset (
        new juce::AudioProcessorValueTreeState::ComboBoxAttachment (processorRef.apvts, paramId, cc.combo));
}

void KickAssEditor::setupToggle (ToggleControl& tc, const juce::String& paramId, const juce::String& display)
{
    tc.button.setButtonText (display);
    tc.button.setLookAndFeel (&lnf);
    tc.attachment.reset (
        new juce::AudioProcessorValueTreeState::ButtonAttachment (processorRef.apvts, paramId, tc.button));
}

//==============================================================================
void KickAssEditor::triggerPreviewNote()
{
    // Programmatic MIDI trigger for the PLAY button. The processor only consumes
    // MIDI in processBlock — easiest path is to push a note-on into the engine directly.
    processorRef.getEngine().triggerNote (60, 1.0f, 0);
}

//==============================================================================
void KickAssEditor::paint (juce::Graphics& g)
{
    using namespace KickColors;

    g.fillAll (bg);

    // ---- Header strip (52 px) ----
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop (52);
    g.setColour (panel);
    g.fillRect (header);

    // Wordmark (KICK in bright, ASS in red)
    {
        auto hb = header.reduced (16, 0);
        const float titleHeight = 26.0f;

        juce::Font wm = KickFonts::ui (titleHeight, true);
        juce::GlyphArrangement ga;
        ga.addLineOfText (wm, "KICK", (float) hb.getX(), hb.getY() + titleHeight + 8.0f);
        const float kickW = ga.getBoundingBox (0, -1, true).getWidth();

        g.setColour (textBright);
        g.setFont (wm);
        g.drawText ("KICK", hb.getX(), hb.getY() + 4,
                    (int) std::ceil (kickW) + 4, hb.getHeight() - 8,
                    juce::Justification::centredLeft);

        g.setColour (accentHot);
        g.drawText ("ASS", hb.getX() + (int) std::ceil (kickW) + 2, hb.getY() + 4,
                    120, hb.getHeight() - 8, juce::Justification::centredLeft);

        g.setColour (textGhost);
        g.setFont (KickFonts::uiItalic (10.0f));
        g.drawText ("by Gleinkaa",
                    hb.getX(), hb.getBottom() - 14,
                    200, 12, juce::Justification::centredLeft);
    }

    // Header divider hairline
    g.setColour (accentHot.withAlpha (0.15f));
    g.drawHorizontalLine (header.getBottom(), 0.0f, (float) getWidth());

    // (visualizer paints itself — it's a child component)
}

//==============================================================================
void KickAssEditor::layoutPanelHeader (ParamPanel& /*panel*/) { /* header drawn in panel.paint() */ }

void KickAssEditor::layoutKnobsInPanel (ParamPanel& panel,
                                        const std::vector<KnobControl*>& knobs, int columns)
{
    if (knobs.empty()) return;
    const int rows = (int) std::ceil ((float) knobs.size() / (float) columns);

    auto inner = panel.getLocalBounds().reduced (8, 30);  // leave header 24 + gap 6
    const int colW = (inner.getWidth() - (columns - 1) * 4) / columns;
    const int rowH = inner.getHeight() / rows;

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const int col = (int) i % columns;
        const int row = (int) i / columns;
        const int x = inner.getX() + col * (colW + 4);
        const int y = inner.getY() + row * rowH;

        auto cell = juce::Rectangle<int> (x, y, colW, rowH).reduced (2);
        auto labelBox = cell.removeFromBottom (12);
        knobs[i]->label.setBounds (labelBox);
        knobs[i]->slider.setBounds (cell);
    }
}

void KickAssEditor::resized()
{
    auto bounds = getLocalBounds();

    bounds.removeFromTop (52);                          // header
    auto vizArea = bounds.removeFromTop (340).reduced (16, 8);
    visualizer.setBounds (vizArea);
    auto paramsRow = bounds.removeFromTop (224);
    auto footerRow = bounds;                            // remaining (44 + padding)

    // ---- Param row ----
    paramsRow.reduce (16, 8);
    // 6 panels with widths proportional to knob counts (more knobs = wider)
    //   PITCH 6, AMP 6, SCOOP 3, TRANSIENT 5, DRIVE 2, MASTER 3   total = 25
    const int totalKnobs = 6 + 6 + 3 + 5 + 2 + 3;
    const int gap = 8;
    const int availW = paramsRow.getWidth() - 5 * gap;

    auto allocate = [&] (int knobs) {
        return juce::roundToInt ((float) availW * ((float) knobs / (float) totalKnobs));
    };

    auto cut = [&] (int w) {
        auto r = paramsRow.removeFromLeft (w);
        paramsRow.removeFromLeft (gap);
        return r;
    };

    pitchPanel.setBounds     (cut (allocate (6)));
    ampPanel.setBounds       (cut (allocate (6)));
    scoopPanel.setBounds     (cut (allocate (3)));
    transientPanel.setBounds (cut (allocate (5)));
    drivePanel.setBounds     (cut (allocate (2)));
    masterPanel.setBounds    (paramsRow);   // remainder

    // ---- Knob layouts inside each panel ----
    layoutKnobsInPanel (pitchPanel, { &startFreq, &midFreq, &endFreq,
                                       &sweepTime1, &sweepTime2, &pitchCurve }, 2);
    layoutKnobsInPanel (ampPanel,   { &volAttack, &volHold, &volDecay1,
                                       &volSustain, &volDecay2, &volCurve }, 2);
    layoutKnobsInPanel (scoopPanel, { &scoopStart, &scoopLength, &scoopDepth }, 1);
    layoutKnobsInPanel (drivePanel, { &drive, &tailDrive }, 1);

    // Transient: combo at top, then 4 knobs in 2×2
    {
        auto inner = transientPanel.getLocalBounds().reduced (8, 30);
        // Combo row
        auto comboRow = inner.removeFromTop (40);
        clickType.label.setBounds (comboRow.removeFromTop (12));
        clickType.combo.setBounds (comboRow.reduced (4, 2));
        // 2×2 knobs
        std::vector<KnobControl*> tknobs = { &clickVol, &clickHpf, &clickTone, &clickDecay };
        const int colW = (inner.getWidth() - 4) / 2;
        const int rowH = inner.getHeight() / 2;
        for (size_t i = 0; i < tknobs.size(); ++i)
        {
            const int col = (int) i % 2;
            const int row = (int) i / 2;
            auto cell = juce::Rectangle<int> (inner.getX() + col * (colW + 4),
                                              inner.getY() + row * rowH,
                                              colW, rowH).reduced (2);
            auto labelBox = cell.removeFromBottom (12);
            tknobs[i]->label.setBounds (labelBox);
            tknobs[i]->slider.setBounds (cell);
        }
    }

    // Master: toggle on top, then 2 knobs stacked
    {
        auto inner = masterPanel.getLocalBounds().reduced (8, 30);
        auto toggleRow = inner.removeFromTop (28);
        invertPhase.button.setBounds (toggleRow.reduced (2));
        std::vector<KnobControl*> mknobs = { &outputGain, &pitchTrack };
        const int rowH = inner.getHeight() / (int) mknobs.size();
        for (size_t i = 0; i < mknobs.size(); ++i)
        {
            auto cell = juce::Rectangle<int> (inner.getX(),
                                              inner.getY() + (int) i * rowH,
                                              inner.getWidth(), rowH).reduced (2);
            auto labelBox = cell.removeFromBottom (12);
            mknobs[i]->label.setBounds (labelBox);
            mknobs[i]->slider.setBounds (cell);
        }
    }

    // ---- Footer ----
    footerRow.reduce (16, 8);
    const int btnGap = 8;
    auto playW = footerRow.proportionOfWidth (0.50f);
    playBtn.setBounds   (footerRow.removeFromLeft (playW));
    footerRow.removeFromLeft (btnGap);

    const int rightBtns = footerRow.getWidth();
    abBtn.setBounds     (footerRow.removeFromRight ((rightBtns - btnGap) / 2));
    footerRow.removeFromRight (btnGap);
    exportBtn.setBounds (footerRow);
}
