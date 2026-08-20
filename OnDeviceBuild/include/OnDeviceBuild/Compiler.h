#pragma once

#include <string>
#include <vector>

namespace ondevice {

/** One in-process invocation of clang's -cc1 frontend.

    Note what is deliberately absent: any notion of a clang *driver*. The driver
    is the part that spawns `clang -cc1` as a child process, and spawning is
    exactly what iOS does not allow, so this never builds a command line for
    an executable that does not exist. It drives the frontend directly, which is
    the only shape that can ever work on a device.
*/
struct CompileRequest
{
    std::string sourcePath;
    std::string outputPath;
    std::string triple = "arm64-apple-ios17.0";
    std::string resourceDir;
    std::string sysroot;
    std::string minimumOSVersion = "17.0";
    std::vector<std::string> extraArgs;
};

struct CompileResult
{
    bool success = false;
    std::string diagnostics;
    unsigned long long outputBytes = 0;
};

CompileResult compileToObject (const CompileRequest&);

/** Resident set size in bytes, for the repeat-invocation leak check. Every
    frontend run happens inside this one process, so a per-run leak is what
    decides whether a real build of a few dozen files survives on a device. */
unsigned long long getResidentMemoryBytes();

} // namespace ondevice
