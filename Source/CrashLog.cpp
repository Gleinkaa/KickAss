#include "CrashLog.h"

namespace kickass::crashlog
{
    juce::File getLogDir()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("KickAss")
                   .getChildFile ("Logs");
    }

    static juce::String appName()
    {
       #if defined (JucePlugin_Name)
        return JucePlugin_Name;
       #else
        return "KickAss";
       #endif
    }

    static juce::String appVersion()
    {
       #if defined (JucePlugin_VersionString)
        return JucePlugin_VersionString;
       #else
        return "dev";
       #endif
    }

    juce::String formatReport (const juce::String& message, juce::Time when)
    {
        juce::String r;
        r << "==== " << appName() << " CRASH REPORT ====" << juce::newLine
          << "Time:        " << when.toISO8601 (true) << juce::newLine
          << "Version:     " << appVersion() << juce::newLine
          << "JUCE:        " << juce::SystemStats::getJUCEVersion() << juce::newLine
          << "OS:          " << juce::SystemStats::getOperatingSystemName() << juce::newLine
          << "CPU:         " << juce::SystemStats::getCpuModel()
                             << "  (" << juce::SystemStats::getNumCpus() << " cores)" << juce::newLine
          << "Memory (MB): " << juce::SystemStats::getMemorySizeInMegabytes() << juce::newLine
          << juce::newLine
          << "Message:" << juce::newLine
          << (message.isNotEmpty() ? message : juce::String ("(no message)")) << juce::newLine
          << juce::newLine
          << "Stack backtrace:" << juce::newLine
          << juce::SystemStats::getStackBacktrace() << juce::newLine;
        return r;
    }

    juce::File writeReportTo (const juce::File& dir, const juce::String& message, juce::Time when)
    {
        if (! dir.createDirectory())
            return {};

        auto file = dir.getChildFile ("KickAss_crash_"
                        + when.formatted ("%Y%m%d_%H%M%S") + ".txt");

        if (! file.replaceWithText (formatReport (message, when)))
            return {};

        return file;
    }

    juce::File writeReport (const juce::String& message)
    {
        return writeReportTo (getLogDir(), message);
    }

    //==============================================================================
    static void crashHandler (void* platformSpecificData)
    {
        // We are in a compromised state here: keep it minimal and best-effort.
        // platformSpecificData is the EXCEPTION_POINTERS* on Windows; we don't
        // decode it — getStackBacktrace() already captures the call site.
        juce::ignoreUnused (platformSpecificData);
        writeReport ("Application crash handler invoked.");
    }

    void install()
    {
        static bool installed = false;
        if (installed)
            return;

        installed = true;
        juce::SystemStats::setApplicationCrashHandler (crashHandler);
    }
}
