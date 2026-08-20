#include "OnDeviceBuild/Compiler.h"
#include "OnDeviceBuild/MachOCheck.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>

namespace
{
[[noreturn]] void fail (const std::string& message)
{
    std::cerr << "FAIL: " << message << '\n';
    std::exit (1);
}

const char* requireEnv (const char* name)
{
    const char* value = std::getenv (name);

    if (value != nullptr && value[0] != '\0')
        return value;

    const char* allowSkip = std::getenv ("ONDEVICE_ALLOW_SKIP");
    const bool skip = allowSkip != nullptr && std::strcmp (allowSkip, "1") == 0;

    if (skip)
    {
        std::cout << "SKIP: " << name << " is unset\n";
        std::exit (0);
    }

    std::cerr << "FAIL: required environment variable " << name << '\n';
    std::exit (1);
}
} // namespace

int main()
{
    const std::string sysroot = requireEnv ("ONDEVICE_SYSROOT");
    const std::string resourceDir = requireEnv ("ONDEVICE_RESOURCE_DIR");

    const auto stddefPath = std::filesystem::path (resourceDir) / "include" / "stddef.h";

    if (! std::filesystem::is_regular_file (stddefPath))
        fail ("ONDEVICE_RESOURCE_DIR does not contain include/stddef.h: " + resourceDir);

#ifndef ONDEVICE_FIXTURE_UIKIT
#error ONDEVICE_FIXTURE_UIKIT must be defined to the uikit.mm fixture path
#endif
    const std::string sourcePath = ONDEVICE_FIXTURE_UIKIT;

    if (! std::filesystem::is_regular_file (sourcePath))
        fail ("fixture missing: " + sourcePath);

    const auto outputPath = std::filesystem::temp_directory_path()
                                / ("ondevice-uikit-" + std::to_string (::getpid()) + ".o");
    std::filesystem::remove (outputPath);

    ondevice::CompileRequest request;
    request.sourcePath = sourcePath;
    request.outputPath = outputPath.string();
    request.resourceDir = resourceDir;
    request.sysroot = sysroot;
    request.extraArgs = { "-x", "objective-c++", "-std=c++17" };

    std::cout << "rss before: " << ondevice::getResidentMemoryBytes() << " bytes\n";

    const auto result = ondevice::compileToObject (request);

    if (! result.diagnostics.empty())
        std::cout << "clang diagnostics:\n" << result.diagnostics << '\n';

    if (result.diagnostics.find ("'UIKit/UIKit.h' file not found") != std::string::npos
        || result.diagnostics.find ("UIKit/UIKit.h' file not found") != std::string::npos)
    {
        fail ("UIKit/UIKit.h not found — iframework / sysroot flags are wrong");
    }

    if (! result.success)
        fail ("compileToObject failed");

    if (result.outputBytes == 0)
        fail ("compile reported success but output is empty");

    const auto info = ondevice::inspectMachO (outputPath.string());
    std::cout << "object: " << info.describe() << '\n';
    std::cout << "rss after: " << ondevice::getResidentMemoryBytes() << " bytes\n";

    if (! info.isIOSArm64Object())
        fail ("produced file is not isIOSArm64Object(): " + info.describe());

    std::filesystem::remove (outputPath);
    std::cout << "PASS: object is arm64 iOS MH_OBJECT\n";
    return 0;
}
