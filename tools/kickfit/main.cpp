// KickFit — headless: apply {params, curve} JSON to a KickAssProcessor, render, optionally save .kickpreset.
// usage: KickFit <spec.json> <out.wav> [durationMs] [out.kickpreset]
#include "PluginProcessor.h"
#include "PresetManager.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

int main (int argc, char** argv)
{
    if (argc < 3) { std::cerr << "usage: KickFit spec.json out.wav [ms] [out.kickpreset]\n"; return 2; }
    juce::ScopedJuceInitialiser_GUI init;
    KickAssProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    const juce::File specFile { juce::String (argv[1]) };
    if (specFile.hasFileExtension (".kickpreset"))
    {
        if (! proc.getPresetManager().loadKickPreset (specFile)) { std::cerr << "preset load failed\n"; return 5; }
    }
    auto spec = specFile.hasFileExtension (".kickpreset") ? juce::var() : juce::JSON::parse (specFile);
    if (auto* params = spec["params"].getDynamicObject())
        for (auto& kv : params->getProperties())
        {
            auto* p = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (kv.name.toString()));
            if (p == nullptr) { std::cerr << "unknown param " << kv.name.toString() << "\n"; return 3; }
            p->setValueNotifyingHost (p->convertTo0to1 ((float) (double) kv.value));
        }
    if (auto* pts = spec["curve"].getArray())
    {
        {
            const juce::SpinLock::ScopedLockType sl (proc.getVolCurveLock());
            auto& c = proc.getVolEnvCurve();
            c.points.clear();
            for (auto& pt : *pts)
                c.points.push_back ({ (float) (double) pt[0], (float) (double) pt[1], (float) (double) pt[2] });
        }
        proc.setVolCurveMode ("custom");
        proc.syncVolCurveToValueTree();
        proc.publishVolCurveToEngines();
    }

    const double ms = argc > 3 ? juce::String (argv[3]).getDoubleValue() : 400.0;
    juce::AudioBuffer<float> buf;
    proc.offlineRender (buf, ms);

    const juce::File out { juce::String (argv[2]) };
    out.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (out),
        48000.0, 1, 32, {}, 0));
    w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    w.reset();

    if (argc > 4)
    {
        const juce::File pf { juce::String (argv[4]) };
        if (! proc.getPresetManager().saveKickPreset (pf)) { std::cerr << "preset save failed\n"; return 4; }
    }
    std::cout << "rendered " << buf.getNumSamples() << " samples, ch=" << buf.getNumChannels() << "\n";
    return 0;
}
