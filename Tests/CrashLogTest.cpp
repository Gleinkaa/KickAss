// CrashLog headless test — verifies the GUI-free pieces of the crash-logging
// module (Source/CrashLog.{h,cpp}): log-dir path shape, report formatting, and
// the file-write round-trip. The crash *handler* itself can't be unit-tested
// without faulting the process, so we test everything around it.
//
// Mirrors the style of DspSafetyTest.cpp / SpectrumUtilTest.cpp (own REQUIRE
// macros, g_failures, return 0/1). Writes only into a throwaway temp dir.
//
// Build/run:
//   cmake --build build --config Release --target KickAss_CrashTests
//   build/KickAss_CrashTests_artefacts/Release/KickAss_CrashTests.exe
//
// Returns 0 on PASS, 1 on FAIL.

#include "../Source/CrashLog.h"
#include <juce_core/juce_core.h>
#include <iostream>

namespace
{
    int g_failures = 0;

    void reportFail (const char* expr, const char* file, int line, const juce::String& extra = {})
    {
        ++g_failures;
        std::cerr << "FAIL  " << file << ":" << line << "  " << expr;
        if (extra.isNotEmpty()) std::cerr << "  [" << extra.toRawUTF8() << "]";
        std::cerr << std::endl;
    }
}

#define REQUIRE(cond) do { if (! (cond)) reportFail (#cond, __FILE__, __LINE__); } while (0)
#define REQUIRE_MSG(cond, msg) do { if (! (cond)) reportFail (#cond, __FILE__, __LINE__, msg); } while (0)

static void runHeader (const char* name) { std::cout << "---- " << name << " ----" << std::endl; }

//==============================================================================
static void test_logDir_isUnderAppDataKickAss()
{
    runHeader ("logDir under %APPDATA%/KickAss/Logs");

    auto dir = kickass::crashlog::getLogDir();
    REQUIRE (dir.getFileName() == "Logs");
    REQUIRE (dir.getParentDirectory().getFileName() == "KickAss");

    auto appData = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    REQUIRE_MSG (dir.isAChildOf (appData), dir.getFullPathName());
}

static void test_formatReport_containsKeyFields()
{
    runHeader ("formatReport contains key fields");

    const juce::String marker = "SENTINEL_CRASH_MESSAGE_42";
    const auto when = juce::Time (2026, 4 /*0-based: May*/, 29, 13, 37, 0);
    auto report = kickass::crashlog::formatReport (marker, when);

    REQUIRE (report.isNotEmpty());
    REQUIRE (report.contains ("CRASH REPORT"));
    REQUIRE_MSG (report.contains (marker), "message not echoed");
    REQUIRE (report.contains (juce::SystemStats::getJUCEVersion()));
    REQUIRE (report.contains ("OS:"));
    REQUIRE (report.contains ("Stack backtrace:"));
    // Deterministic given `when`: the formatted timestamp must appear.
    REQUIRE_MSG (report.contains ("2026"), "year missing from report");
}

static void test_writeReportTo_createsReadableFile()
{
    runHeader ("writeReportTo creates a readable, round-tripping file");

    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("KickAss_CrashTest_" + juce::String (juce::Random::getSystemRandom().nextInt()))
                   .getChildFile ("Logs");

    const juce::String marker = "ROUNDTRIP_MARKER_99";
    auto file = kickass::crashlog::writeReportTo (tmp, marker);

    REQUIRE_MSG (file.existsAsFile(), "report file not written");
    REQUIRE (file.getFileName().startsWith ("KickAss_crash_"));
    REQUIRE (file.getFileName().endsWith (".txt"));

    auto contents = file.loadFileAsString();
    REQUIRE_MSG (contents.contains (marker), "written content missing marker");
    REQUIRE (contents.length() > 100);

    // Cleanup: remove the throwaway tree (parent of Logs).
    tmp.getParentDirectory().deleteRecursively();
}

static void test_install_isIdempotent()
{
    runHeader ("install() is safe to call repeatedly");
    // No crash / no throw across repeated installs is the whole assertion.
    kickass::crashlog::install();
    kickass::crashlog::install();
    REQUIRE (true);
}

//==============================================================================
int main (int, char**)
{
    test_logDir_isUnderAppDataKickAss();
    test_formatReport_containsKeyFields();
    test_writeReportTo_createsReadableFile();
    test_install_isIdempotent();

    if (g_failures == 0)
    {
        std::cout << "\nALL CRASHLOG TESTS PASSED\n";
        return 0;
    }

    std::cerr << "\n" << g_failures << " FAILURE(S)\n";
    return 1;
}
