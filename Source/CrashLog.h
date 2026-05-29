#pragma once
#include <juce_core/juce_core.h>

//==============================================================================
// v1.1 — crash logging.
//
// Process-wide best-effort crash handler that writes a human-readable report
// (timestamp, app/JUCE/OS/CPU info + stack backtrace) to %APPDATA%/KickAss/Logs
// when the process faults. The pure pieces (path, formatting, file write) are
// factored out so they are exercised headlessly by Tests/CrashLogTest.cpp —
// the handler itself can't be unit-tested without actually crashing.
//==============================================================================
namespace kickass::crashlog
{
    /** Where reports go: userApplicationDataDirectory/KickAss/Logs
        (i.e. %APPDATA%\KickAss\Logs on Windows). Not created until first write. */
    juce::File getLogDir();

    /** Human-readable crash report body. Deterministic given `when` + `message`. */
    juce::String formatReport (const juce::String& message,
                               juce::Time when = juce::Time::getCurrentTime());

    /** Write a timestamped report (KickAss_crash_YYYYMMDD_HHMMSS.txt) into `dir`,
        creating `dir` if needed. Returns the written file, or a non-existent
        juce::File on failure. Used by the test with a temp dir. */
    juce::File writeReportTo (const juce::File& dir,
                              const juce::String& message,
                              juce::Time when = juce::Time::getCurrentTime());

    /** Convenience: writeReportTo (getLogDir(), message). */
    juce::File writeReport (const juce::String& message);

    /** Install the process-wide handler (idempotent — safe to call repeatedly). */
    void install();
}
