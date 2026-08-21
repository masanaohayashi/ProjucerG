#include "OnDeviceBuild/MachOCheck.h"

#include <mach-o/loader.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace ondevice {

namespace
{
std::string platformNameFor (unsigned platform)
{
    switch (platform)
    {
        case PLATFORM_MACOS:           return "macOS";
        case PLATFORM_IOS:             return "iOS";
        case PLATFORM_TVOS:            return "tvOS";
        case PLATFORM_WATCHOS:         return "watchOS";
        case PLATFORM_MACCATALYST:     return "Mac Catalyst";
        case PLATFORM_IOSSIMULATOR:    return "iOS Simulator";
        default:                       return "unknown (" + std::to_string (platform) + ")";
    }
}

/** Mach-O packs a version as xxxx.yy.zz into 32 bits. */
std::string versionString (uint32_t packed)
{
    char buffer[32];
    std::snprintf (buffer, sizeof (buffer), "%u.%u.%u",
                   packed >> 16, (packed >> 8) & 0xff, packed & 0xff);
    return buffer;
}
} // namespace

std::string MachOInfo::describe() const
{
    if (! error.empty())
        return "error: " + error;

    std::string text = std::to_string (fileSize) + " bytes, "
                     + (is64Bit ? "64-bit " : "32-bit ")
                     + (isArm64 ? "arm64" : "non-arm64")
                     + (isObjectFile ? ", MH_OBJECT"
                                     : isExecutable ? ", MH_EXECUTE"
                                                    : ", neither object nor executable");

    if (hasBuildVersion)
        text += ", platform " + platformName + ", min OS " + minOSVersion;
    else
        text += ", NO build version load command";

    return text;
}

MachOInfo inspectMachO (const std::string& path)
{
    MachOInfo info;

    auto* file = std::fopen (path.c_str(), "rb");

    if (file == nullptr)
    {
        info.error = "could not open " + path;
        return info;
    }

    std::fseek (file, 0, SEEK_END);
    const auto size = std::ftell (file);
    std::fseek (file, 0, SEEK_SET);

    std::vector<unsigned char> bytes ((size_t) (size > 0 ? size : 0));

    if (bytes.size() < sizeof (mach_header_64)
        || std::fread (bytes.data(), 1, bytes.size(), file) != bytes.size())
    {
        std::fclose (file);
        info.error = "file too small to be a 64-bit Mach-O";
        return info;
    }

    std::fclose (file);
    info.fileSize = bytes.size();

    mach_header_64 header = {};
    std::memcpy (&header, bytes.data(), sizeof (header));

    if (header.magic != MH_MAGIC_64)
    {
        // Not treating MH_CIGAM_64 as a pass: a byte-swapped object would mean
        // the producer disagreed with the device about endianness, which is a
        // failure worth seeing rather than silently accommodating.
        info.error = "bad magic - not a native 64-bit Mach-O";
        return info;
    }

    info.parsed = true;
    info.is64Bit = true;
    info.isArm64 = header.cputype == CPU_TYPE_ARM64;
    info.isObjectFile = header.filetype == MH_OBJECT;
    info.isExecutable = header.filetype == MH_EXECUTE;

    size_t offset = sizeof (mach_header_64);

    for (uint32_t i = 0; i < header.ncmds; ++i)
    {
        if (offset + sizeof (load_command) > bytes.size())
            break;

        load_command command = {};
        std::memcpy (&command, bytes.data() + offset, sizeof (command));

        if (command.cmdsize < sizeof (load_command) || offset + command.cmdsize > bytes.size())
            break;

        if (command.cmd == LC_BUILD_VERSION && command.cmdsize >= sizeof (build_version_command))
        {
            build_version_command buildVersion = {};
            std::memcpy (&buildVersion, bytes.data() + offset, sizeof (buildVersion));

            info.hasBuildVersion = true;
            info.platform = buildVersion.platform;
            info.platformName = platformNameFor (buildVersion.platform);
            info.minOSVersion = versionString (buildVersion.minos);
            break;
        }

        if (command.cmd == LC_VERSION_MIN_IPHONEOS && command.cmdsize >= sizeof (version_min_command))
        {
            // Older encoding, still worth accepting: it says iOS just as clearly.
            version_min_command versionMin = {};
            std::memcpy (&versionMin, bytes.data() + offset, sizeof (versionMin));

            info.hasBuildVersion = true;
            info.platform = PLATFORM_IOS;
            info.platformName = platformNameFor (PLATFORM_IOS);
            info.minOSVersion = versionString (versionMin.version);
            break;
        }

        offset += command.cmdsize;
    }

    return info;
}

} // namespace ondevice
