#include "PresetManager.h"

//==============================================================================
// Factory bank: 16 presets (6 from Python BUILTIN_PRESETS + 10 from research/04 §7).
// Field order matches the APVTS parameter IDs.
//==============================================================================
struct FactoryPreset
{
    const char* name;

    float start_freq, mid_freq, end_freq;
    float sweep_time_1, sweep_time_2, pitch_curve;

    float vol_attack, vol_hold, vol_decay_1, vol_sustain, vol_decay_2, vol_curve;

    float scoop_start, scoop_length, scoop_depth;

    float click_vol; int click_type; float click_hpf, click_tone, click_decay;

    float drive, tail_drive;

    bool  invert_phase;
    float output_gain, pitch_track;
};

static const FactoryPreset kFactoryPresets[] = {
    // --- 6 from Python BUILTIN_PRESETS ---
    {"Psytrance (Default)",    8000.f, 150.f, 49.00f,  10.f,  80.f, 3.0f,    2.0f, 10.f,  50.f, 40.f, 150.f, 3.0f,   10.f, 30.f,  0.f,   0.00f, 0, 800.f, 10000.f, 5.f,   1.5f, 0.0f, false,   0.f, 0.f},
    {"Techno Deep",            2000.f, 200.f, 43.00f,  20.f, 150.f, 2.0f,    5.0f, 30.f, 100.f, 60.f, 300.f, 1.5f,   20.f, 50.f,  0.f,   0.05f, 0, 800.f, 10000.f, 5.f,   3.0f, 2.0f, false,   0.f, 0.f},
    {"Hardstyle Zap",         15000.f, 500.f, 55.00f,   5.f, 100.f, 5.0f,    0.0f, 20.f,  80.f, 20.f, 100.f, 5.0f,    5.f, 20.f,  0.f,   0.10f, 0, 800.f, 10000.f, 5.f,   8.0f, 5.0f, false,   0.f, 0.f},
    {"Projektor Punch",       12000.f, 350.f, 49.00f,   8.f,  80.f, 4.0f,    0.5f, 15.f,  60.f, 30.f, 250.f, 3.5f,   15.f, 40.f, 10.f,   0.15f, 0, 800.f, 10000.f, 5.f,   2.5f, 1.0f, false,   0.f, 0.f},
    {"Projektor Deep Sub",     8000.f, 200.f, 43.65f,  15.f, 150.f, 2.5f,    2.0f, 30.f, 100.f, 70.f, 400.f, 2.0f,   20.f, 60.f,  5.f,   0.05f, 0, 800.f, 10000.f, 5.f,   1.2f, 3.0f,  true,   0.f, 0.f},
    {"Projektor Hard F#",     14000.f, 450.f, 46.25f,   5.f, 110.f, 4.5f,    0.0f, 10.f,  70.f, 40.f, 200.f, 4.0f,   10.f, 35.f, 15.f,   0.20f, 0, 800.f, 10000.f, 5.f,   4.0f, 4.0f, false,   0.f, 0.f},

    // --- 10 from research/04 §7 ---
    {"Forest Stomp 145",       7000.f, 250.f, 46.25f,  12.f, 130.f, 2.8f,    1.0f, 20.f,  80.f, 55.f, 320.f, 3.0f,   18.f, 50.f,  8.f,   0.07f, 0, 800.f, 10000.f, 5.f,   1.8f, 2.5f, false,   0.f, 0.f},
    {"Hard Psy Main F#",      13000.f, 400.f, 46.25f,   6.f,  90.f, 4.2f,    0.0f, 12.f,  65.f, 35.f, 220.f, 3.5f,   12.f, 38.f, 12.f,   0.18f, 0, 800.f, 10000.f, 5.f,   3.2f, 2.0f, false,   0.f, 0.f},
    {"Deep Twilight G",        6000.f, 180.f, 49.00f,  18.f, 180.f, 2.2f,    2.5f, 35.f, 110.f, 72.f, 420.f, 2.5f,   22.f, 65.f,  4.f,   0.04f, 0, 800.f, 10000.f, 5.f,   1.1f, 3.2f,  true,   0.f, 0.f},
    {"Progressive 138",        9000.f, 300.f, 49.00f,  10.f, 110.f, 3.2f,    1.0f, 18.f,  75.f, 45.f, 290.f, 3.0f,   16.f, 45.f,  7.f,   0.10f, 0, 800.f, 10000.f, 5.f,   2.0f, 1.5f, false,   0.f, 0.f},
    {"Hi-Tech Snap A",        15000.f, 500.f, 55.00f,   4.f,  95.f, 5.0f,    0.0f,  8.f,  60.f, 30.f, 180.f, 4.5f,    8.f, 30.f, 18.f,   0.22f, 0, 800.f, 10000.f, 5.f,   4.5f, 3.5f, false,   0.f, 0.f},
    {"Darkpsy Stomp E",        8000.f, 220.f, 41.20f,  10.f, 120.f, 3.0f,    0.5f, 15.f,  70.f, 50.f, 260.f, 3.5f,   14.f, 42.f, 12.f,   0.12f, 0, 800.f, 10000.f, 5.f,   3.0f, 3.5f, false,   0.f, 0.f},
    {"Astrix Main F#",        12500.f, 380.f, 46.25f,   7.f,  85.f, 4.0f,    0.2f, 14.f,  62.f, 32.f, 230.f, 3.5f,   13.f, 38.f, 11.f,   0.16f, 0, 800.f, 10000.f, 5.f,   2.8f, 1.5f, false,   0.f, 0.f},
    {"Burn In Noise Forest",   7500.f, 240.f, 43.65f,  11.f, 125.f, 2.7f,    1.5f, 22.f,  85.f, 58.f, 340.f, 2.8f,   18.f, 52.f,  9.f,   0.08f, 0, 800.f, 10000.f, 5.f,   2.2f, 3.0f,  true,   0.f, 0.f},
    {"Sub Lover 142",          6500.f, 200.f, 43.65f,  15.f, 160.f, 2.4f,    2.0f, 28.f,  95.f, 65.f, 380.f, 2.5f,   20.f, 58.f,  5.f,   0.05f, 0, 800.f, 10000.f, 5.f,   1.4f, 3.8f, false,   0.f, 0.f},
    {"Tight Club F#",         11000.f, 340.f, 46.25f,   8.f,  75.f, 3.6f,    0.3f, 12.f,  55.f, 28.f, 200.f, 3.5f,   12.f, 35.f, 10.f,   0.13f, 0, 800.f, 10000.f, 5.f,   2.4f, 1.2f, false,   0.f, 0.f},
};

static constexpr int kFactoryCount = (int) (sizeof (kFactoryPresets) / sizeof (kFactoryPresets[0]));

//==============================================================================
PresetManager::PresetManager (KickAssProcessor& p) : processor (p)
{
    for (int i = 0; i < kFactoryCount; ++i)
        factoryNames.add (kFactoryPresets[i].name);

    auto dir = getUserPresetDir();
    if (! dir.exists())
        dir.createDirectory();
    rescanUserPresets();
}

//==============================================================================
juce::File PresetManager::getUserPresetDir() const
{
    // Portability rule §8b#2: use juce::File::getSpecialLocation, never hardcode.
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
              .getChildFile ("KickAss")
              .getChildFile ("Presets");
}

juce::StringArray PresetManager::getFactoryNames() const
{
    return factoryNames;
}

juce::StringArray PresetManager::getUserPresetNames() const
{
    juce::StringArray names;
    for (auto& f : userPresetFiles)
        names.add (f.getFileNameWithoutExtension());
    return names;
}

void PresetManager::rescanUserPresets()
{
    userPresetFiles.clearQuick();
    auto dir = getUserPresetDir();
    if (! dir.exists()) return;

    for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.kickpreset"))
        userPresetFiles.add (f);
    for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.json"))
        userPresetFiles.add (f);
}

//==============================================================================
void PresetManager::applyParameterMap (const juce::NamedValueSet& kv)
{
    for (int i = 0; i < kv.size(); ++i)
    {
        const auto& name  = kv.getName (i).toString();
        const auto  value = kv.getValueAt (i);

        if (auto* p = processor.apvts.getParameter (name))
        {
            float raw = 0.0f;
            if (value.isBool())          raw = (bool) value ? 1.0f : 0.0f;
            else if (value.isInt())      raw = (float) (int) value;
            else if (value.isDouble())   raw = (float) (double) value;
            else if (value.isString())   raw = value.toString().getFloatValue();
            else continue;

            // Convert raw -> normalized -> set via the proper notifying path.
            if (auto* rap = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                const float norm = rap->getNormalisableRange().convertTo0to1 (raw);
                rap->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, norm));
            }
        }
    }
}

//==============================================================================
bool PresetManager::applyFactory (int index)
{
    if (index < 0 || index >= kFactoryCount) return false;
    const auto& fp = kFactoryPresets[index];

    juce::NamedValueSet kv;
    kv.set ("start_freq",   fp.start_freq);
    kv.set ("mid_freq",     fp.mid_freq);
    kv.set ("end_freq",     fp.end_freq);
    kv.set ("sweep_time_1", fp.sweep_time_1);
    kv.set ("sweep_time_2", fp.sweep_time_2);
    kv.set ("pitch_curve",  fp.pitch_curve);
    kv.set ("vol_attack",   fp.vol_attack);
    kv.set ("vol_hold",     fp.vol_hold);
    kv.set ("vol_decay_1",  fp.vol_decay_1);
    kv.set ("vol_sustain",  fp.vol_sustain);
    kv.set ("vol_decay_2",  fp.vol_decay_2);
    kv.set ("vol_curve",    fp.vol_curve);
    kv.set ("scoop_start",  fp.scoop_start);
    kv.set ("scoop_length", fp.scoop_length);
    kv.set ("scoop_depth",  fp.scoop_depth);
    kv.set ("click_vol",    fp.click_vol);
    kv.set ("click_type",   fp.click_type);
    kv.set ("click_hpf",    fp.click_hpf);
    kv.set ("click_tone",   fp.click_tone);
    kv.set ("click_decay",  fp.click_decay);
    kv.set ("drive",        fp.drive);
    kv.set ("tail_drive",   fp.tail_drive);
    kv.set ("invert_phase", fp.invert_phase);
    kv.set ("output_gain",  fp.output_gain);
    kv.set ("pitch_track",  fp.pitch_track);
    applyParameterMap (kv);
    return true;
}

bool PresetManager::applyByName (const juce::String& name)
{
    for (int i = 0; i < kFactoryCount; ++i)
        if (name == kFactoryPresets[i].name)
            return applyFactory (i);

    // Try user presets
    for (auto& f : userPresetFiles)
        if (f.getFileNameWithoutExtension() == name)
            return loadFile (f);

    return false;
}

//==============================================================================
bool PresetManager::saveKickPreset (const juce::File& file)
{
    if (auto xml = processor.apvts.copyState().createXml())
        return xml->writeTo (file);
    return false;
}

bool PresetManager::loadKickPreset (const juce::File& file)
{
    if (! file.existsAsFile()) return false;
    if (auto xml = juce::XmlDocument::parse (file))
    {
        if (xml->hasTagName (processor.apvts.state.getType()))
        {
            processor.apvts.replaceState (juce::ValueTree::fromXml (*xml));
            return true;
        }
    }
    return false;
}

//==============================================================================
bool PresetManager::saveJson (const juce::File& file)
{
    juce::DynamicObject::Ptr obj (new juce::DynamicObject());
    for (auto* p : processor.apvts.processor.getParameters())
    {
        if (auto* rap = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto id  = rap->paramID;
            const auto raw = rap->getNormalisableRange().convertFrom0to1 (rap->getValue());
            // Bools written as JSON bool to match Python format
            if (id == "invert_phase")
                obj->setProperty (id, raw > 0.5f);
            else
                obj->setProperty (id, (double) raw);
        }
    }
    juce::var v (obj.get());
    return file.replaceWithText (juce::JSON::toString (v, true));
}

bool PresetManager::loadJson (const juce::File& file)
{
    if (! file.existsAsFile()) return false;
    juce::var v = juce::JSON::parse (file);
    if (auto* obj = v.getDynamicObject())
    {
        applyParameterMap (obj->getProperties());
        return true;
    }
    return false;
}

bool PresetManager::loadFile (const juce::File& file)
{
    if (file.hasFileExtension (".kickpreset"))  return loadKickPreset (file);
    if (file.hasFileExtension (".json"))        return loadJson (file);
    return false;
}
