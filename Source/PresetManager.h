#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

//==============================================================================
// PresetManager — owns the 16 factory presets, handles save/load to:
//   1. .kickpreset XML files (APVTS state — DAW-portable)
//   2. .json files (flat dict, Python-compatible — round-trips with BazzismRebuild.py)
//
// Path: %APPDATA%\KickAss\Presets\ on Windows; ~/Library/Application Support/KickAss/Presets/
// on macOS. Resolved via juce::File::getSpecialLocation (portability rule §8b#2).
//==============================================================================

class PresetManager
{
public:
    explicit PresetManager (KickAssProcessor& p);

    //--------------------------------------------------------------------------
    // Factory presets
    juce::StringArray getFactoryNames() const;
    int  getFactoryCount() const { return (int) factoryNames.size(); }
    bool applyFactory (int index);
    bool applyByName  (const juce::String& name);   // factory OR user-preset name

    //--------------------------------------------------------------------------
    // User presets (.kickpreset / .json files on disk)
    juce::StringArray getUserPresetNames() const;
    void rescanUserPresets();
    juce::File getUserPresetDir() const;

    //--------------------------------------------------------------------------
    // I/O
    bool saveKickPreset (const juce::File& file);
    bool loadKickPreset (const juce::File& file);

    bool saveJson (const juce::File& file);
    bool loadJson (const juce::File& file);

    /** Auto-detects format from extension. */
    bool loadFile (const juce::File& file);

private:
    KickAssProcessor& processor;

    juce::StringArray factoryNames;
    juce::Array<juce::File> userPresetFiles;

    void applyParameterMap (const juce::NamedValueSet& kv);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};
