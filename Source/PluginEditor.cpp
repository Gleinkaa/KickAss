#include "PluginEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>

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
    setResizable (true, true);
    setResizeLimits (1100, 720, 1920, 1200);
    setSize (1280, 820);

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

    // Mode segmented control inside AMP panel header (Phase 6b)
    for (auto* b : { &modeSimpleBtn, &modeAdvancedBtn })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (0x6b6d6f64);                 // 'kmod'
        b->setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
        ampPanel.addAndMakeVisible (*b);
    }
    modeSimpleBtn  .onClick = [this] { if (modeSimpleBtn  .getToggleState()) setEnvelopeMode (false); };
    modeAdvancedBtn.onClick = [this] { if (modeAdvancedBtn.getToggleState()) setEnvelopeMode (true);  };

    // Listen to envelope_mode so any external change (preset load, host automation, DAW UI)
    // keeps the buttons + AMP-knob enable-state in sync.
    processorRef.apvts.addParameterListener ("envelope_mode", this);
    syncEnvelopeModeUI();

    // Phase 8 — modified-preset indicator: listen to every APVTS param so any
    // user/host change flips the "*" on, then clear it after a preset load/save.
    for (auto* prm : processorRef.getParameters())
        if (auto* rap = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (rap->paramID != "envelope_mode")        // already registered above
                processorRef.apvts.addParameterListener (rap->paramID, this);

    modifiedDot.setColour (juce::Label::textColourId, KickColors::accentHot);
    modifiedDot.setFont (KickFonts::ui (16.0f, true));
    modifiedDot.setJustificationType (juce::Justification::centred);
    modifiedDot.setVisible (false);
    addAndMakeVisible (modifiedDot);

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
    setupChoice (clickType, "click_type", "Source", { "Sine", "Noise", "Both", "Sample" });
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

    // v1.1 — drag-a-WAV drop zone inside the TRANSIENT panel.
    sampleLabel.setJustificationType (juce::Justification::centred);
    sampleLabel.setFont (KickFonts::ui (10.0f));
    sampleLabel.setColour (juce::Label::textColourId, KickColors::textDim);
    sampleLabel.setColour (juce::Label::outlineColourId, KickColors::accentHot.withAlpha (0.25f));
    transientPanel.addAndMakeVisible (sampleLabel);

    sampleClearBtn.setTooltip ("Clear transient sample");
    sampleClearBtn.onClick = [this]
    {
        processorRef.clearTransientSample();
        updateSampleLabel();
    };
    transientPanel.addAndMakeVisible (sampleClearBtn);
    updateSampleLabel();

    // ---- DRIVE ----
    setupKnob (drive,     "drive",      "Base");
    setupKnob (tailDrive, "tail_drive", "Tail");
    setupChoice (satType, "sat_type", "Type",
                 { "Tanh", "Soft Clip", "Hard Clip", "Tube", "Foldback" });
    for (auto* k : { &drive, &tailDrive })
    {
        drivePanel.addAndMakeVisible (k->slider);
        drivePanel.addAndMakeVisible (k->label);
    }
    drivePanel.addAndMakeVisible (satType.combo);
    drivePanel.addAndMakeVisible (satType.label);

    // ---- MASTER ----
    setupToggle (invertPhase, "invert_phase", "Invert Phase");
    setupToggle (safetyLimit, "safety_limit", "Safety");
    setupKnob (outputGain,  "output_gain",  "Output");
    setupKnob (pitchTrack,  "pitch_track",  "Pitch Track");
    setupKnob (phaseOffset, "phase_offset", "Phase °");
    masterPanel.addAndMakeVisible (invertPhase.button);
    masterPanel.addAndMakeVisible (safetyLimit.button);
    for (auto* k : { &outputGain, &pitchTrack, &phaseOffset })
    {
        masterPanel.addAndMakeVisible (k->slider);
        masterPanel.addAndMakeVisible (k->label);
    }

    // ---- Header bar widgets ----
    addAndMakeVisible (presetCombo);
    addAndMakeVisible (noteSnapCombo);
    addAndMakeVisible (saveBtn);
    addAndMakeVisible (loadBtn);

    populatePresetCombo();
    presetCombo.onChange = [this] { handlePresetSelection(); };

    static const juce::StringArray noteNames {
        "Off","C1","C#1","D1","D#1","E1","F1","F#1","G1","G#1","A1","A#1","B1","C2"
    };
    noteSnapCombo.addItemList (noteNames, 1);
    noteSnapCombo.setSelectedId (1, juce::dontSendNotification);   // "Off"
    noteSnapCombo.onChange = [this] { handleNoteSnapSelection(); };

    saveBtn.onClick = [this] { doSavePreset(); };
    loadBtn.onClick = [this] { doLoadPreset(); };

    // ---- Footer buttons ----
    addAndMakeVisible (playBtn);
    addAndMakeVisible (exportBtn);
    addAndMakeVisible (dragOutBtn);
    addAndMakeVisible (abBtn);
    addAndMakeVisible (undoBtn);
    addAndMakeVisible (redoBtn);
    playBtn.onClick   = [this] { triggerPreviewNote(); };
    exportBtn.onClick = [this] { doExportWav(); };
    dragOutBtn.onDrag = [this] (const juce::MouseEvent& e) { beginWavDragOut (e); };
    undoBtn.onClick   = [this] { doUndo(); };
    redoBtn.onClick   = [this] { doRedo(); };
    // Capture keyboard focus so Ctrl+Z / Ctrl+Shift+Z reach keyPressed().
    setWantsKeyboardFocus (true);
    updateUndoRedoEnablement();
    abBtn.setButtonText ("A");
    abBtn.onClick = [this]
    {
        // Plain click toggles between slots. Shift-held click copies current
        // state into the inactive slot ("commit" / "checkpoint" semantics).
        if (juce::ModifierKeys::currentModifiers.isShiftDown())
            doCopyAB();
        else
            doToggleAB();
    };

    // ---- Auto Play 4/4 ----
    bpmSlider.setSliderStyle (juce::Slider::LinearBar);
    bpmSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    bpmSlider.setRange (60.0, 200.0, 1.0);
    bpmSlider.setValue (145.0, juce::dontSendNotification);
    bpmSlider.setTextValueSuffix (" BPM");
    bpmSlider.setLookAndFeel (&lnf);
    bpmSlider.onValueChange = [this]
    {
        // If the timer is running, restart it with the new interval so the
        // beat grid realigns immediately to the new tempo.
        if (autoPlayIsOn)
            startTimer (juce::jlimit (100, 2000,
                        juce::roundToInt (60000.0 / bpmSlider.getValue())));
    };
    addAndMakeVisible (bpmSlider);

    autoPlayBtn.setClickingTogglesState (true);
    autoPlayBtn.onClick = [this]
    {
        autoPlayIsOn = autoPlayBtn.getToggleState();
        if (autoPlayIsOn)
        {
            triggerPreviewNote();   // fire immediately on first beat
            startTimer (juce::jlimit (100, 2000,
                        juce::roundToInt (60000.0 / bpmSlider.getValue())));
        }
        else
        {
            stopTimer();
        }
    };
    addAndMakeVisible (autoPlayBtn);

    resized();
}

//==============================================================================
void KickAssEditor::populatePresetCombo()
{
    presetCombo.clear (juce::dontSendNotification);
    int id = 1;
    auto& pm = processorRef.getPresetManager();
    presetCombo.addItem ("--- Factory ---", id++);
    presetCombo.setItemEnabled (id - 1, false);
    for (auto& n : pm.getFactoryNames())
        presetCombo.addItem (n, id++);

    auto userNames = pm.getUserPresetNames();
    if (! userNames.isEmpty())
    {
        presetCombo.addItem ("--- User ---", id++);
        presetCombo.setItemEnabled (id - 1, false);
        for (auto& n : userNames)
            presetCombo.addItem (n, id++);
    }
}

void KickAssEditor::handlePresetSelection()
{
    const auto name = presetCombo.getText();
    if (name.startsWith ("---")) return;

    // Suppress modification detection during the burst of listener callbacks
    // that applyByName triggers (setValueNotifyingHost per param), then snapshot
    // the result as the new "loaded" baseline.
    suppressModificationDetect = true;
    processorRef.getPresetManager().applyByName (name);
    suppressModificationDetect = false;
    // The bounced-to-message-thread parameterChanged callbacks fire AFTER this
    // function returns; they read suppressModificationDetect which is false by
    // then. So defer the clear to the back of the message queue too.
    juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<KickAssEditor> (this)]
    {
        if (auto* self = safeThis.getComponent())
        {
            self->clearPresetModifiedFlag();
            self->updateSampleLabel();
        }
    });
}

void KickAssEditor::handleNoteSnapSelection()
{
    const auto name = noteSnapCombo.getText();
    if (name == "Off") return;

    static const std::map<juce::String, float> noteHz = {
        {"C1",  32.70f}, {"C#1", 34.65f}, {"D1",  36.71f}, {"D#1", 38.89f},
        {"E1",  41.20f}, {"F1",  43.65f}, {"F#1", 46.25f}, {"G1",  49.00f},
        {"G#1", 51.91f}, {"A1",  55.00f}, {"A#1", 58.27f}, {"B1",  61.74f},
        {"C2",  65.41f}
    };
    auto it = noteHz.find (name);
    if (it == noteHz.end()) return;

    if (auto* rap = dynamic_cast<juce::RangedAudioParameter*> (processorRef.apvts.getParameter ("end_freq")))
    {
        const float norm = rap->getNormalisableRange().convertTo0to1 (it->second);
        rap->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, norm));
    }
}

void KickAssEditor::doSavePreset()
{
    auto dir = processorRef.getPresetManager().getUserPresetDir();
    fileChooser = std::make_unique<juce::FileChooser> (
        "Save KickAss preset", dir, "*.kickpreset;*.json");
    fileChooser->launchAsync (
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f.getFullPathName().isEmpty()) return;
            auto& pm = processorRef.getPresetManager();
            if (f.hasFileExtension (".json"))
                pm.saveJson (f);
            else
            {
                if (! f.hasFileExtension (".kickpreset"))
                    f = f.withFileExtension (".kickpreset");
                pm.saveKickPreset (f);
            }
            pm.rescanUserPresets();
            populatePresetCombo();
            clearPresetModifiedFlag();   // saving makes "the current file" = current state
        });
}

//==============================================================================
// Phase 8 — EXPORT WAV
//==============================================================================
double KickAssEditor::computeRenderDurationMs() const
{
    // Pick a sensible duration from the current envelope. In Advanced mode that's
    // the curve's total time; in Simple it's the AHDSR sum. Add 50 ms tail so the
    // final decay isn't clipped. Shared by EXPORT WAV and the drag-out affordance.
    if (processorRef.isEnvelopeAdvanced())
    {
        juce::SpinLock::ScopedLockType l (processorRef.getVolCurveLock());
        return juce::jmax (200.0f, processorRef.getVolEnvCurve().getTotalMs() + 50.0f);
    }

    const float ta  = *processorRef.apvts.getRawParameterValue ("vol_attack");
    const float th  = *processorRef.apvts.getRawParameterValue ("vol_hold");
    const float td1 = *processorRef.apvts.getRawParameterValue ("vol_decay_1");
    const float td2 = *processorRef.apvts.getRawParameterValue ("vol_decay_2");
    return juce::jmax (200.0, (double) (ta + th + td1 + td2) + 50.0);
}

void KickAssEditor::doExportWav()
{
    const double durationMs = computeRenderDurationMs();

    fileChooser = std::make_unique<juce::FileChooser> (
        "Export kick as WAV",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("kick.wav"),
        "*.wav");

    fileChooser->launchAsync (
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, durationMs] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f.getFullPathName().isEmpty()) return;
            if (! f.hasFileExtension (".wav"))
                f = f.withFileExtension (".wav");

            processorRef.renderToWavFile (f, durationMs);
        });
}

//==============================================================================
// v1.1 — drag a rendered WAV out onto the desktop / a DAW track.
//==============================================================================
void KickAssEditor::beginWavDragOut (const juce::MouseEvent& e)
{
    // mouseDrag fires repeatedly; only kick off one external drag per gesture,
    // and only once the pointer has actually moved (so a stray click is ignored).
    if (isDragAndDropActive() || e.getDistanceFromDragStart() < 6)
        return;

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("KickAss");
    dir.createDirectory();

    // Name from the active preset (falls back to "kick") so the dropped file is
    // recognisable; timestamp keeps repeated drags from colliding.
    auto base = presetCombo.getText();
    if (base.isEmpty() || base.startsWith ("---")) base = "kick";
    base = juce::File::createLegalFileName (base);
    auto wav = dir.getChildFile (base + "_"
                   + juce::String (juce::Time::getCurrentTime().toMilliseconds()) + ".wav");

    if (processorRef.renderToWavFile (wav, computeRenderDurationMs()))
        performExternalDragDropOfFiles ({ wav.getFullPathName() }, /*canMoveFiles*/ false);
}

//==============================================================================
// DragOutButton — drawn to read like the other footer buttons.
//==============================================================================
void DragOutButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (KickColors::panel.brighter (0.05f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (KickColors::accentHot.withAlpha (0.45f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    g.setColour (KickColors::accentHot.withAlpha (0.85f));
    g.setFont (KickFonts::ui (11.0f, true));
    g.drawText (juce::String::fromUTF8 ("DRAG WAV \xe2\x86\x97"),  // "DRAG WAV ↗"
                getLocalBounds(), juce::Justification::centred);
}

//==============================================================================
// Phase 8 — A/B compare
//==============================================================================
void KickAssEditor::doToggleAB()
{
    // Capture current into the active slot, then load the other slot (if it
    // has been initialized). First-time toggle initializes both to current.
    auto current = processorRef.apvts.copyState();

    auto& currentSlot = abSlotIsB ? abSlotB : abSlotA;
    auto& otherSlot   = abSlotIsB ? abSlotA : abSlotB;

    currentSlot = current;                  // remember where we were
    if (! otherSlot.isValid())
        otherSlot = current.createCopy();   // first flip: B starts equal to A

    // A/B is a deliberate state swap, not a parameter edit — don't let the burst
    // of listener callbacks falsely flip the modified-preset asterisk on.
    suppressModificationDetect = true;
    processorRef.apvts.replaceState (otherSlot.createCopy());
    processorRef.restoreVolCurveFromStateOrAhdsr();
    suppressModificationDetect = false;

    abSlotIsB = ! abSlotIsB;
    abBtn.setButtonText (abSlotIsB ? "B" : "A");
    updateSampleLabel();
}

void KickAssEditor::doCopyAB()
{
    // Shift-click: copy current state into the INACTIVE slot. Useful for
    // "commit this as my B reference" without flipping away from it.
    auto current = processorRef.apvts.copyState();
    auto& otherSlot = abSlotIsB ? abSlotA : abSlotB;
    otherSlot = current;
}

void KickAssEditor::doLoadPreset()
{
    auto dir = processorRef.getPresetManager().getUserPresetDir();
    fileChooser = std::make_unique<juce::FileChooser> (
        "Load KickAss preset", dir, "*.kickpreset;*.json");
    fileChooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            suppressModificationDetect = true;
            processorRef.getPresetManager().loadFile (f);
            suppressModificationDetect = false;
            juce::Component::SafePointer<KickAssEditor> safeThis (this);
            juce::MessageManager::callAsync ([safeThis]
            {
                if (auto* self = safeThis.getComponent())
                    self->clearPresetModifiedFlag();
            });
        });
}

//==============================================================================
bool KickAssEditor::isAudioFile (const juce::String& path)
{
    return path.endsWithIgnoreCase (".wav")
        || path.endsWithIgnoreCase (".aif")
        || path.endsWithIgnoreCase (".aiff")
        || path.endsWithIgnoreCase (".flac");
}

bool KickAssEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& s : files)
        if (s.endsWithIgnoreCase (".json") || s.endsWithIgnoreCase (".kickpreset")
            || isAudioFile (s))
            return true;
    return false;
}

void KickAssEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dropHighlight = false;
    repaint();
    for (auto& s : files)
    {
        juce::File f (s);

        // Branch on extension. JSON / kickpreset → preset load (unchanged).
        if (f.hasFileExtension (".json") || f.hasFileExtension (".kickpreset"))
        {
            suppressModificationDetect = true;
            processorRef.getPresetManager().loadFile (f);
            suppressModificationDetect = false;
            juce::Component::SafePointer<KickAssEditor> safeThis (this);
            juce::MessageManager::callAsync ([safeThis]
            {
                if (auto* self = safeThis.getComponent())
                    self->clearPresetModifiedFlag();
            });
            return;   // first match wins
        }

        // Audio file → load as the transient sample and switch Source to "Sample".
        if (isAudioFile (s))
        {
            if (processorRef.loadTransientSampleFile (f))
            {
                if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (
                        processorRef.apvts.getParameter ("click_type")))
                {
                    // click_type is a 4-item choice → "Sample" is index 3.
                    processorRef.getUndoManager().beginNewTransaction();
                    prm->setValueNotifyingHost (prm->convertTo0to1 (3.0f));
                }
                updateSampleLabel();
                repaint();
            }
            return;   // first match wins
        }
    }
}

void KickAssEditor::updateSampleLabel()
{
    const juce::String path = processorRef.getTransientSamplePath();
    if (path.isNotEmpty())
        sampleLabel.setText (juce::File (path).getFileName(), juce::dontSendNotification);
    else
        sampleLabel.setText ("drag WAV...", juce::dontSendNotification);
    sampleClearBtn.setEnabled (path.isNotEmpty());
}

void KickAssEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    dropHighlight = true;
    repaint();
}

void KickAssEditor::fileDragExit (const juce::StringArray&)
{
    dropHighlight = false;
    repaint();
}

KickAssEditor::~KickAssEditor()
{
    stopTimer();   // stop Auto Play before any member destruction
    for (auto* prm : processorRef.getParameters())
        if (auto* rap = dynamic_cast<juce::RangedAudioParameter*> (prm))
            processorRef.apvts.removeParameterListener (rap->paramID, this);
    setLookAndFeel (nullptr);
}

//==============================================================================
// Phase 6b — envelope mode plumbing
//==============================================================================
void KickAssEditor::parameterChanged (const juce::String& paramID, float /*newValue*/)
{
    // APVTS listener may fire on the audio thread — bounce to message thread for UI work.
    juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<KickAssEditor> (this), paramID]
    {
        auto* self = safeThis.getComponent();
        if (! self) return;
        if (paramID == "envelope_mode")
            self->syncEnvelopeModeUI();
        if (! self->suppressModificationDetect)
            self->markPresetModified();
        // Any parameter edit may have pushed/altered the undo stack.
        self->updateUndoRedoEnablement();
    });
}

void KickAssEditor::markPresetModified()
{
    if (isPresetModified) return;
    isPresetModified = true;
    modifiedDot.setVisible (true);
}

void KickAssEditor::clearPresetModifiedFlag()
{
    isPresetModified = false;
    modifiedDot.setVisible (false);
}

void KickAssEditor::setEnvelopeMode (bool advanced)
{
    // Refresh the breakpoint curve from current AHDSR knob values BEFORE flipping
    // the mode, so the moment DSP swaps to LUT mode it reads a freshly-baked
    // shape matching what was just heard in Simple mode. No-op if the user has
    // already customized the curve (mode == "custom").
    if (advanced)
        processorRef.onEnterAdvancedMode();

    if (auto* p = processorRef.apvts.getParameter ("envelope_mode"))
    {
        // envelope_mode is an AudioParameterChoice with 2 items → norm 0.0/1.0
        const float norm = advanced ? 1.0f : 0.0f;
        if (! juce::approximatelyEqual (p->getValue(), norm))
            p->setValueNotifyingHost (norm);
    }
}

void KickAssEditor::syncEnvelopeModeUI()
{
    const bool advanced = processorRef.isEnvelopeAdvanced();

    modeSimpleBtn  .setToggleState (! advanced, juce::dontSendNotification);
    modeAdvancedBtn.setToggleState (  advanced, juce::dontSendNotification);

    // Grey AHDSR knobs in Advanced mode — the breakpoint editor owns the shape now.
    const float dimAlpha = 0.40f;
    for (auto* k : { &volAttack, &volHold, &volDecay1, &volSustain, &volDecay2, &volCurve })
    {
        k->slider.setEnabled              (! advanced);
        k->slider.setInterceptsMouseClicks (! advanced, ! advanced);
        k->slider.setAlpha                (advanced ? dimAlpha : 1.0f);
        k->label.setAlpha                 (advanced ? dimAlpha : 1.0f);
    }
}

//==============================================================================
void KickAssEditor::setupKnob (KnobControl& kc, const juce::String& paramId, const juce::String& display)
{
    kc.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    kc.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 14);
    kc.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    kc.slider.setLookAndFeel (&lnf);
    kc.slider.setPopupMenuEnabled (true);   // right-click → Reset / Edit value / Copy / Paste

    kc.label.setText (display, juce::dontSendNotification);
    kc.label.setJustificationType (juce::Justification::centredTop);
    kc.label.setFont (KickFonts::ui (10.0f));
    kc.label.setColour (juce::Label::textColourId, KickColors::textDim);

    // Undo coalescing: open a fresh transaction when a drag starts so the whole
    // gesture collapses to a single undo step (not one step per pixel).
    kc.slider.onDragStart = [this] { processorRef.getUndoManager().beginNewTransaction(); };

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

    // Undo coalescing: a discrete combo pick is its own undo step. The attachment
    // drives the parameter via its own ComboBox listener; onChange is free for us.
    cc.combo.onChange = [this] { processorRef.getUndoManager().beginNewTransaction(); };

    cc.attachment.reset (
        new juce::AudioProcessorValueTreeState::ComboBoxAttachment (processorRef.apvts, paramId, cc.combo));
}

void KickAssEditor::setupToggle (ToggleControl& tc, const juce::String& paramId, const juce::String& display)
{
    tc.button.setButtonText (display);
    tc.button.setLookAndFeel (&lnf);
    // Undo coalescing: each toggle click is its own undo step. The attachment uses
    // the button's own listener; onClick is free for us to open a transaction.
    tc.button.onClick = [this] { processorRef.getUndoManager().beginNewTransaction(); };
    tc.attachment.reset (
        new juce::AudioProcessorValueTreeState::ButtonAttachment (processorRef.apvts, paramId, tc.button));
}

//==============================================================================
// Undo/Redo
//==============================================================================
void KickAssEditor::doUndo()
{
    processorRef.undo();
    // Parameter values are restored by the UndoManager, but the breakpoint curve
    // lives in apvts.state (NOT as an APVTS param) and is NOT captured by undo.
    // Re-derive it from the restored state so the curve view + DSP stay consistent.
    processorRef.restoreVolCurveFromStateOrAhdsr();
    updateSampleLabel();
    updateUndoRedoEnablement();
    repaint();
}

void KickAssEditor::doRedo()
{
    processorRef.redo();
    processorRef.restoreVolCurveFromStateOrAhdsr();
    updateSampleLabel();
    updateUndoRedoEnablement();
    repaint();
}

void KickAssEditor::updateUndoRedoEnablement()
{
    undoBtn.setEnabled (processorRef.canUndo());
    redoBtn.setEnabled (processorRef.canRedo());
}

bool KickAssEditor::keyPressed (const juce::KeyPress& key)
{
    const bool ctrl  = key.getModifiers().isCommandDown();   // Ctrl on Win, Cmd on Mac
    const bool shift = key.getModifiers().isShiftDown();

    if (ctrl && key.isKeyCode ('Z'))
    {
        if (shift) doRedo(); else doUndo();
        return true;
    }
    if (ctrl && key.isKeyCode ('Y'))   // Windows-style redo
    {
        doRedo();
        return true;
    }
    return false;
}

//==============================================================================
void KickAssEditor::triggerPreviewNote()
{
    // Queue a note-on for the audio thread. The processor drains this in
    // processBlock, calling engine.triggerNote() on the correct thread.
    processorRef.requestTrigger (60, 1.0f);
}

void KickAssEditor::timerCallback()
{
    // Auto Play 4/4 — fires every (60000 / bpm) ms on the message thread.
    triggerPreviewNote();
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

        g.setColour (textDim);
        g.setFont (KickFonts::uiItalic (14.0f));
        g.drawText ("by Gleinkaa",
                    hb.getX() + (int) std::ceil (kickW) + 100, hb.getY() + 4,
                    160, hb.getHeight() - 8, juce::Justification::centredLeft);
    }

    // Header divider hairline
    g.setColour (accentHot.withAlpha (0.15f));
    g.drawHorizontalLine (header.getBottom(), 0.0f, (float) getWidth());

    // ---- Drop highlight border ----
    if (dropHighlight)
    {
        g.setColour (accentHot);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 6.0f, 2.0f);
    }

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

    auto header = bounds.removeFromTop (52);
    // Header layout: wordmark on left (occupies ~ first 180 px), controls on right
    {
        auto hr = header.reduced (16, 12);
        hr.removeFromLeft (340);   // wordmark gutter (painted in paint(); includes "by Gleinkaa")
        const int gap = 8;
        loadBtn.setBounds        (hr.removeFromRight (60));
        hr.removeFromRight (gap);
        saveBtn.setBounds        (hr.removeFromRight (60));
        hr.removeFromRight (gap);
        noteSnapCombo.setBounds  (hr.removeFromRight (70));
        hr.removeFromRight (gap);
        presetCombo.setBounds    (hr.removeFromLeft  (juce::jmin (260, hr.getWidth() - 200)));
        // Modified-preset asterisk sits flush to the right edge of the preset combo.
        modifiedDot.setBounds    (presetCombo.getRight() + 2, presetCombo.getY(), 14, presetCombo.getHeight());
    }

    auto vizArea = bounds.removeFromTop (340).reduced (16, 8);
    visualizer.setBounds (vizArea);
    auto paramsRow = bounds.removeFromTop (224);
    auto footerRow = bounds;                            // remaining (44 + padding)

    // ---- Param row ----
    paramsRow.reduce (16, 8);
    // 6 panels with widths proportional to knob counts (more knobs = wider)
    //   PITCH 6, AMP 6, SCOOP 3, TRANSIENT 5, DRIVE 3, MASTER 4   total = 27
    //   (DRIVE counts as 3 to give the saturation-type combo comfortable width)
    const int totalKnobs = 6 + 6 + 3 + 5 + 3 + 4;
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
    drivePanel.setBounds     (cut (allocate (3)));
    masterPanel.setBounds    (paramsRow);   // remainder (MASTER = 4)

    // ---- Knob layouts inside each panel ----
    layoutKnobsInPanel (pitchPanel, { &startFreq, &midFreq, &endFreq,
                                       &sweepTime1, &sweepTime2, &pitchCurve }, 2);
    layoutKnobsInPanel (ampPanel,   { &volAttack, &volHold, &volDecay1,
                                       &volSustain, &volDecay2, &volCurve }, 2);

    // Mode segmented control: right-anchored in the AMP panel header strip (top 24 px).
    {
        auto inner = ampPanel.getLocalBounds().reduced (8, 4);
        auto headerStrip = inner.removeFromTop (20);
        const int btnW = 56;
        modeAdvancedBtn.setBounds (headerStrip.removeFromRight (btnW));
        modeSimpleBtn  .setBounds (headerStrip.removeFromRight (btnW));
    }
    layoutKnobsInPanel (scoopPanel, { &scoopStart, &scoopLength, &scoopDepth }, 1);

    // DRIVE: two knobs stacked, saturation-type combo anchored at the bottom.
    {
        auto inner = drivePanel.getLocalBounds().reduced (8, 30);
        auto comboRow = inner.removeFromBottom (40);
        satType.label.setBounds (comboRow.removeFromTop (12));
        satType.combo.setBounds (comboRow.reduced (4, 2));

        std::vector<KnobControl*> dknobs = { &drive, &tailDrive };
        const int rowH = inner.getHeight() / (int) dknobs.size();
        for (size_t i = 0; i < dknobs.size(); ++i)
        {
            auto cell = juce::Rectangle<int> (inner.getX(), inner.getY() + (int) i * rowH,
                                              inner.getWidth(), rowH).reduced (2);
            auto labelBox = cell.removeFromBottom (12);
            dknobs[i]->label.setBounds (labelBox);
            dknobs[i]->slider.setBounds (cell);
        }
    }

    // Transient: combo at top, then 4 knobs in 2×2
    {
        auto inner = transientPanel.getLocalBounds().reduced (8, 30);
        // Combo row
        auto comboRow = inner.removeFromTop (40);
        clickType.label.setBounds (comboRow.removeFromTop (12));
        clickType.combo.setBounds (comboRow.reduced (4, 2));
        // v1.1 — drop-zone row: filename label + a small clear button on the right.
        auto sampleRow = inner.removeFromTop (20).reduced (4, 2);
        sampleClearBtn.setBounds (sampleRow.removeFromRight (18));
        sampleRow.removeFromRight (4);
        sampleLabel.setBounds (sampleRow);
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
        auto safetyRow = inner.removeFromTop (28);
        safetyLimit.button.setBounds (safetyRow.reduced (2));
        std::vector<KnobControl*> mknobs = { &outputGain, &pitchTrack, &phaseOffset };
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
    // Layout (right → left so sizes are fixed, PLAY KICK gets whatever remains):
    //   [PLAY KICK ··] [BPM 110] [AUTO 70] [EXPORT 100] [DRAG WAV 92] [A/B 60] [REDO 56] [UNDO 56]
    footerRow.reduce (16, 8);
    const int btnGap = 8;

    redoBtn.setBounds    (footerRow.removeFromRight (56));
    footerRow.removeFromRight (btnGap);
    undoBtn.setBounds    (footerRow.removeFromRight (56));
    footerRow.removeFromRight (btnGap);
    abBtn.setBounds      (footerRow.removeFromRight (60));
    footerRow.removeFromRight (btnGap);
    dragOutBtn.setBounds (footerRow.removeFromRight (92));
    footerRow.removeFromRight (btnGap);
    exportBtn.setBounds  (footerRow.removeFromRight (100));
    footerRow.removeFromRight (btnGap);
    autoPlayBtn.setBounds (footerRow.removeFromRight (70));
    footerRow.removeFromRight (btnGap);
    bpmSlider.setBounds  (footerRow.removeFromRight (110));
    footerRow.removeFromRight (btnGap);
    playBtn.setBounds    (footerRow);   // remainder
}
