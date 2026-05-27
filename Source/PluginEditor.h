#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

//==============================================================================
// PHASE 1: empty editor, just the brand wordmark and the dimensions.
// Phase 3 will add LookAndFeel, header bar, 6 param panels, footer.
// Phase 4 will add the WaveformDisplay visualizer.
//==============================================================================

class KickAssEditor : public juce::AudioProcessorEditor
{
public:
    explicit KickAssEditor (KickAssProcessor&);
    ~KickAssEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    KickAssProcessor& processorRef;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickAssEditor)
};
