#include "OnDeviceBuild/Compiler.h"
#include "OnDeviceBuild/Linker.h"
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

ondevice::CompileResult compileOne (const std::string& sourcePath,
                                    const std::string& outputPath,
                                    const std::string& resourceDir,
                                    const std::string& sysroot,
                                    bool simulator,
                                    const std::vector<std::string>& extraArgs)
{
    ondevice::CompileRequest request;
    request.sourcePath = sourcePath;
    request.outputPath = outputPath;
    request.simulator = simulator;
    request.triple = std::string ("arm64-apple-ios17.0") + (simulator ? "-simulator" : "");
    request.resourceDir = resourceDir;
    request.sysroot = sysroot;
    request.extraArgs = extraArgs;
    return ondevice::compileToObject (request);
}
} // namespace

int main()
{
    const std::string sysroot = requireEnv ("ONDEVICE_SYSROOT");
    const std::string resourceDir = requireEnv ("ONDEVICE_RESOURCE_DIR");
    const std::string builtins = requireEnv ("ONDEVICE_BUILTINS");
    const char* simulatorValue = std::getenv ("ONDEVICE_SIMULATOR");
    const bool simulator = simulatorValue != nullptr && std::strcmp (simulatorValue, "1") == 0;

    const auto stddefPath = std::filesystem::path (resourceDir) / "include" / "stddef.h";

    if (! std::filesystem::is_regular_file (stddefPath))
        fail ("ONDEVICE_RESOURCE_DIR does not contain include/stddef.h: " + resourceDir);

    if (! std::filesystem::is_regular_file (builtins))
        fail ("ONDEVICE_BUILTINS is not a regular file: " + builtins);

#ifndef ONDEVICE_FIXTURE_UIKIT
#error ONDEVICE_FIXTURE_UIKIT must be defined to the uikit.mm fixture path
#endif
#ifndef ONDEVICE_FIXTURE_LINK_MAIN
#error ONDEVICE_FIXTURE_LINK_MAIN must be defined to the link_main.cpp fixture path
#endif
    const std::string uikitPath = ONDEVICE_FIXTURE_UIKIT;
    const std::string mainPath = ONDEVICE_FIXTURE_LINK_MAIN;

    if (! std::filesystem::is_regular_file (uikitPath))
        fail ("fixture missing: " + uikitPath);

    if (! std::filesystem::is_regular_file (mainPath))
        fail ("fixture missing: " + mainPath);

    const auto pid = std::to_string (::getpid());
    const auto tmp = std::filesystem::temp_directory_path();
    const auto uikitObj = tmp / ("ondevice-link-uikit-" + pid + ".o");
    const auto mainObj = tmp / ("ondevice-link-main-" + pid + ".o");
    const auto exePath = tmp / ("ondevice-link-exe-" + pid);

    std::filesystem::remove (uikitObj);
    std::filesystem::remove (mainObj);
    std::filesystem::remove (exePath);

    const auto compiledUikit = compileOne (uikitPath, uikitObj.string(), resourceDir, sysroot, simulator,
                                           { "-x", "objective-c++", "-std=c++17" });

    if (! compiledUikit.diagnostics.empty())
        std::cout << "clang (uikit.mm):\n" << compiledUikit.diagnostics << '\n';

    if (compiledUikit.diagnostics.find ("'UIKit/UIKit.h' file not found") != std::string::npos
        || compiledUikit.diagnostics.find ("UIKit/UIKit.h' file not found") != std::string::npos)
    {
        fail ("UIKit/UIKit.h not found — iframework / sysroot flags are wrong");
    }

    if (! compiledUikit.success)
        fail ("compileToObject failed for uikit.mm");

    const auto compiledMain = compileOne (mainPath, mainObj.string(), resourceDir, sysroot, simulator,
                                          { "-std=c++17" });

    if (! compiledMain.diagnostics.empty())
        std::cout << "clang (link_main.cpp):\n" << compiledMain.diagnostics << '\n';

    if (! compiledMain.success)
        fail ("compileToObject failed for link_main.cpp");

    ondevice::LinkRequest link;
    link.objectFiles = { uikitObj.string(), mainObj.string() };
    link.outputPath = exePath.string();
    link.simulator = simulator;
    link.sysroot = sysroot;
    link.libraries = { "System", "c++" };
    link.frameworks = { "UIKit", "Foundation" };
    link.builtinsArchive = builtins;

    const auto result = ondevice::linkObjects (link);

    if (! result.diagnostics.empty())
        std::cout << "lld diagnostics:\n" << result.diagnostics << '\n';

    if (result.diagnostics.find ("___divdc3") != std::string::npos)
        fail ("undefined ___divdc3 — ONDEVICE_BUILTINS path is wrong; do not drop the archive");

    if (! result.success)
        fail ("linkObjects failed");

    if (! result.canRunAgain)
        std::cout << "warning: lld reported canRunAgain=false\n";

    if (result.outputBytes == 0)
        fail ("link reported success but output is empty");

    const auto info = ondevice::inspectMachO (exePath.string());
    std::cout << "executable: " << info.describe() << '\n';

    const auto isExpectedExecutable = simulator
                                        ? info.isIosSimulatorArm64Executable()
                                        : info.isIOSArm64Executable();

    if (! isExpectedExecutable)
        fail ("produced file is not the expected arm64 iOS executable: " + info.describe());

    std::filesystem::remove (uikitObj);
    std::filesystem::remove (mainObj);
    std::filesystem::remove (exePath);
    std::cout << "PASS: linked output is arm64 " << (simulator ? "iOS Simulator" : "iOS") << " MH_EXECUTE\n";
    return 0;
}
