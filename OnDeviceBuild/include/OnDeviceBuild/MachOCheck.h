#pragma once

#include <string>

namespace ondevice {

constexpr unsigned PLATFORM_IOS = 2;
constexpr unsigned PLATFORM_IOSSIMULATOR = 7;

/** What a produced object file actually is.

    Producing *a* file proves nothing - the Gate is only PASS if the bytes are a
    64-bit arm64 Mach-O relocatable object whose LC_BUILD_VERSION says iOS. A
    host-targeted build produces something that looks almost identical apart
    from that one load command, so it is the check that matters most here.
*/
struct MachOInfo
{
    bool parsed = false;            ///< the file was readable and had a Mach-O magic
    bool is64Bit = false;
    bool isArm64 = false;
    bool isObjectFile = false;      ///< MH_OBJECT
    bool isExecutable = false;      ///< MH_EXECUTE
    bool hasBuildVersion = false;   ///< an LC_BUILD_VERSION / LC_VERSION_MIN_* was present
    unsigned platform = 0;          ///< PLATFORM_IOS == 2, PLATFORM_IOSSIMULATOR == 7
    std::string platformName;
    std::string minOSVersion;
    unsigned long long fileSize = 0;
    std::string error;

    /** The success conditions, in one place so the host harness and the iOS app
        cannot disagree about what counts as a pass. */
    bool isIOSArm64Object() const      { return isIOSArm64() && isObjectFile; }
    bool isIOSArm64Executable() const  { return isIOSArm64() && isExecutable; }
    bool isIosSimulatorArm64Object() const
    {
        return parsed && is64Bit && isArm64 && hasBuildVersion
            && platform == PLATFORM_IOSSIMULATOR && isObjectFile;
    }
    bool isIosSimulatorArm64Executable() const
    {
        return parsed && is64Bit && isArm64 && hasBuildVersion
            && platform == PLATFORM_IOSSIMULATOR && isExecutable;
    }

private:
    bool isIOSArm64() const
    {
        return parsed && is64Bit && isArm64 && hasBuildVersion && platform == PLATFORM_IOS;
    }

public:

    std::string describe() const;
};

MachOInfo inspectMachO (const std::string& path);

} // namespace ondevice
