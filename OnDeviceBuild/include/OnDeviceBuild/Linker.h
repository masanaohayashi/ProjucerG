#pragma once

#include <string>
#include <vector>

namespace ondevice {

/** One in-process invocation of lld's Mach-O driver.

    Same shape as compileToObject: the thing being driven is the *library*, not
    the `ld64.lld` executable, because there is no executing anything on iOS.
    lld is unusual in being explicit about this - lldMain() documents itself as
    safe for re-entry and reports back whether it can be called again, which is
    exactly the question a long-lived build process has to ask.
*/
struct LinkRequest
{
    std::vector<std::string> objectFiles;
    std::string outputPath;
    /** Root of an iOS SDK, or of the subset of one holding the .tbd stubs. */
    std::string sysroot;
    std::string architecture = "arm64";
    std::string minimumOSVersion = "17.0";
    std::string sdkVersion = "17.0";
    /** e.g. "System" for -lSystem. */
    std::vector<std::string> libraries { "System", "c++" };
    std::vector<std::string> frameworks;

    /** compiler-rt's builtins archive (libclang_rt.ios.a).

        The driver adds this without being asked, and nothing needs it until
        something does: JUCE's dsp module divides std::complex<double>, and the
        link then fails on ___divdc3 with no hint that an entire runtime library
        is missing. Any project doing 64-bit division, complex arithmetic or
        soft-float will hit the same wall. */
    std::string builtinsArchive;

    std::vector<std::string> extraArgs;
};

struct LinkResult
{
    bool success = false;
    /** True unless lld decided its own state is too damaged to be re-entered. */
    bool canRunAgain = true;
    std::string diagnostics;
    unsigned long long outputBytes = 0;
};

LinkResult linkObjects (const LinkRequest&);

} // namespace ondevice
