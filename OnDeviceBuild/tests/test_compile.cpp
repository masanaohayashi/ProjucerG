#include "OnDeviceBuild/Compiler.h"
#include "OnDeviceBuild/MachOCheck.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

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
    const char* simulatorValue = std::getenv ("ONDEVICE_SIMULATOR");
    const bool simulator = simulatorValue != nullptr && std::strcmp (simulatorValue, "1") == 0;

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
    request.simulator = simulator;
    request.triple = std::string ("arm64-apple-ios17.0") + (simulator ? "-simulator" : "");
    request.resourceDir = resourceDir;
    request.sysroot = sysroot;
    request.extraArgs = { "-x", "objective-c++", "-std=c++17" };

    auto cancelledRequest = request;
    cancelledRequest.shouldCancel = [] { return true; };
    const auto cancelled = ondevice::compileToObject (cancelledRequest);

    if (cancelled.success || cancelled.diagnostics != "build cancelled")
        fail ("compile cancellation was not honoured");

    std::cout << "PASS: compile cancellation\n";

    // LLVM's backend command-line registry is process-global and is not
    // thread-safe.  The production engine used to invoke this function from
    // several dispatch workers, so keep this as a regression test for the
    // exact race that aborts with "info-output-file registered more than once".
    std::atomic<bool> concurrentFailed { false };
    std::mutex failureMutex;
    std::string concurrentFailure;
    constexpr int rounds = 4;
    constexpr int workers = 4;

    for (int round = 0; round < rounds; ++round)
    {
        std::vector<std::thread> jobs;

        for (int worker = 0; worker < workers; ++worker)
        {
            jobs.emplace_back ([&, round, worker]
            {
                auto concurrentRequest = request;
                concurrentRequest.outputPath = (std::filesystem::temp_directory_path()
                    / ("ondevice-concurrent-" + std::to_string (::getpid())
                       + "-" + std::to_string (round)
                       + "-" + std::to_string (worker) + ".o")).string();
                std::filesystem::remove (concurrentRequest.outputPath);

                const auto concurrentResult = ondevice::compileToObject (concurrentRequest);

                if (! concurrentResult.success)
                {
                    concurrentFailed = true;
                    std::lock_guard lock (failureMutex);

                    if (concurrentFailure.empty())
                        concurrentFailure = concurrentResult.diagnostics;
                }

                std::filesystem::remove (concurrentRequest.outputPath);
            });
        }

        for (auto& job : jobs)
            job.join();
    }

    if (concurrentFailed)
        fail ("concurrent compileToObject failed:\n" + concurrentFailure);

    std::cout << "PASS: concurrent compileToObject calls\n";

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

    const auto isExpectedObject = simulator
                                    ? info.isIosSimulatorArm64Object()
                                    : info.isIOSArm64Object();

    if (! isExpectedObject)
        fail ("produced file is not the expected arm64 iOS object: " + info.describe());

    std::filesystem::remove (outputPath);
    std::cout << "PASS: object is arm64 " << (simulator ? "iOS Simulator" : "iOS") << " MH_OBJECT\n";
    return 0;
}
