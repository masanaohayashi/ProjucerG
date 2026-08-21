#include "OnDeviceBuild/MachOCheck.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
[[noreturn]] void fail (const std::string& message, const ondevice::MachOInfo* info = nullptr)
{
    std::cerr << "FAIL: " << message << '\n';
    if (info != nullptr)
        std::cerr << "  " << info->describe() << '\n';
    std::exit (1);
}

void requireParsedHostBinary (const std::string& path)
{
    const auto info = ondevice::inspectMachO (path);

    if (! info.parsed)
        fail ("host Mach-O did not parse: " + path, &info);

    if (! info.is64Bit)
        fail ("host Mach-O is not 64-bit: " + path, &info);

    if (info.isIOSArm64Executable())
        fail ("host Mach-O must not report isIOSArm64Executable(): " + path, &info);

    std::cout << "OK host: " << path << " -> " << info.describe() << '\n';
}

void requireSimulatorPredicate()
{
    ondevice::MachOInfo info;
    info.parsed = true;
    info.is64Bit = true;
    info.isArm64 = true;
    info.isObjectFile = true;
    info.isExecutable = true;
    info.hasBuildVersion = true;
    info.platform = ondevice::PLATFORM_IOSSIMULATOR;

    if (! info.isIosSimulatorArm64Executable())
        fail ("iOS Simulator arm64 executable predicate rejected a valid shape", &info);

    if (! info.isIosSimulatorArm64Object())
        fail ("iOS Simulator arm64 object predicate rejected a valid shape", &info);

    info.platform = ondevice::PLATFORM_IOS;

    if (info.isIosSimulatorArm64Executable())
        fail ("iOS device executable was accepted as a Simulator executable", &info);

    std::cout << "OK iOS Simulator predicate\n";
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 1 || argv[0] == nullptr || argv[0][0] == '\0')
        fail ("argv[0] unavailable; cannot locate the test binary");

    requireParsedHostBinary (argv[0]);
    requireSimulatorPredicate();

    if (const char* fixture = std::getenv ("ONDEVICE_FIXTURE_IOS_EXEC"))
    {
        if (fixture[0] == '\0')
            fail ("ONDEVICE_FIXTURE_IOS_EXEC is set but empty");

        const auto info = ondevice::inspectMachO (fixture);

        if (! info.parsed)
            fail ("iOS fixture did not parse: " + std::string (fixture), &info);

        if (! info.isIOSArm64Executable())
            fail ("iOS fixture is not isIOSArm64Executable(): " + std::string (fixture), &info);

        std::cout << "OK ios fixture: " << fixture << " -> " << info.describe() << '\n';
    }

    std::cout << "PASS\n";
    return 0;
}
