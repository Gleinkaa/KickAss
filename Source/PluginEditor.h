#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "KickAssLookAndFeel.h"
#include "WaveformDisplay.h"
#include "PresetManager.h"

//==============================================================================
// One UI widget per APVTS parameter, attachment-managed.
//==============================================================================
struct KnobControl
{
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label  label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

struct ChoiceControl
{
    juce::ComboBox combo;
    juce::Label    label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

struct ToggleControl
{
    juce::ToggleButton button;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

//==============================================================================
// A labeled section panel that holds N controls in a vertical stack.
//==============================================================================
class ParamPanel : public juce::Component
{
public:
    explicit ParamPanel (const juce::String& title) : sectionTitle (title) {}
    void paint (juce::Graphics& g) override;
    void setLayoutBounds() { resized(); }
private:
    juce::String sectionTitle;
};

//==============================================================================
class KickAssEditor : public juce::AudioProcessorEditor,
                       public juce::FileDragAndDropTarget,
                       public juce::AudioProcessorValueTreeState::Listener,
                       public juce::Timer
{
public:
    explicit KickAssEditor (KickAssProcessor&);
    ~KickAssEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // APVTS::Listener — mirror envelope_mode into AMP knob enable-state
    void parameterChanged (const juce::String& paramID, float newValue) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit  (const juce::StringArray&) override;

private:
    KickAssProcessor& processorRef;
    KickAssLookAndFeel lnf;

    // Visualizer
    WaveformDisplay visualizer { processorRef };

    // 6 panels, each a juce::Component child of the editor
    ParamPanel pitchPanel     { "PITCH" };
    ParamPanel ampPanel       { "AMP" };
    ParamPanel scoopPanel     { "SCOOP" };
    ParamPanel transientPanel { "TRANSIENT" };
    ParamPanel drivePanel     { "DRIVE" };
    ParamPanel masterPanel    { "MASTER" };

    // 25 widgets, organized by panel
    // PITCH (6)
    KnobControl startFreq, midFreq, endFreq, sweepTime1, sweepTime2, pitchCurve;
    // AMP (6)
    KnobControl volAttack, volHold, volDecay1, volSustain, volDecay2, volCurve;
    // SCOOP (3)
    KnobControl scoopStart, scoopLength, scoopDepth;
    // TRANSIENT (5)
    KnobControl   clickVol, clickHpf, clickTone, clickDecay;
    ChoiceControl clickType;
    // DRIVE (2 knobs + saturation-type selector)
    KnobControl   drive, tailDrive;
    ChoiceControl satType;
    // MASTER (4)
    ToggleControl invertPhase;
    KnobControl   outputGain, pitchTrack, phaseOffset;

    // Header bar
    juce::ComboBox   presetCombo;
    juce::ComboBox   noteSnapCombo;
    juce::TextButton saveBtn  { "SAVE" };
    juce::TextButton loadBtn  { "LOAD" };
    juce::Label      modifiedDot { {}, "*" };   // shows when current state diverges from last-loaded preset

    // Modified-since-load tracking
    bool isPresetModified           = false;
    bool suppressModificationDetect = false;

    // AMP panel header — Phase 6b envelope mode segmented control
    juce::TextButton modeSimpleBtn   { "Simple" };
    juce::TextButton modeAdvancedBtn { "Advanced" };

    // Footer
    juce::TextButton playBtn    { "PLAY KICK" };
    juce::Slider     bpmSlider;                      // 60–200, default 145, LinearBar
    juce::TextButton autoPlayBtn { "AUTO" };
    juce::TextButton exportBtn  { "EXPORT WAV" };
    juce::TextButton abBtn      { "A/B" };

    bool autoPlayIsOn = false;

    // Preset state
    bool dropHighlight = false;
    std::unique_ptr<juce::FileChooser> fileChooser;

    // A/B compare — two snapshot slots of apvts.state (+ curve subtree). Short-click
    // toggles between A and B; modifier-click (shift / right-click) copies current
    // state into the OTHER slot. The button face shows the currently-active slot.
    juce::ValueTree abSlotA;
    juce::ValueTree abSlotB;
    bool             abSlotIsB = false;   // false → A active, true → B active

    void populatePresetCombo();
    void handlePresetSelection();
    void handleNoteSnapSelection();
    void doSavePreset();
    void doLoadPreset();
    void doExportWav();
    void doToggleAB();
    void doCopyAB();

    // Helpers
    void setupKnob (KnobControl& kc, const juce::String& paramId, const juce::String& display);
    void setupChoice (ChoiceControl& cc, const juce::String& paramId, const juce::String& display,
                      const juce::StringArray& items);
    void setupToggle (ToggleControl& tc, const juce::String& paramId, const juce::String& display);

    void layoutKnobsInPanel (ParamPanel& panel, const std::vector<KnobControl*>& knobs, int columns);
    void layoutPanelHeader (ParamPanel& panel);

    void triggerPreviewNote();
    void timerCallback() override;   // juce::Timer — Auto Play 4/4 retrigger

    // Phase 6b — envelope mode UI sync
    void setEnvelopeMode (bool advanced);          // writes to APVTS via parameter
    void syncEnvelopeModeUI();                     // reads APVTS, updates buttons + knob enable

    // Phase 8 — modified-preset indicator
    void clearPresetModifiedFlag();                // call after preset apply / save
    void markPresetModified();                     // any APVTS change while not suppressed

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickAssEditor)
};
