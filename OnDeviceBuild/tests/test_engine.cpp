#include "OnDeviceBuild/Engine.h"
#include "OnDeviceBuild/MachOCheck.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace
{
namespace fs = std::filesystem;

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

std::string readFile (const fs::path& path)
{
    std::ifstream in (path, std::ios::binary);

    if (! in)
        fail ("cannot read " + path.string());

    return std::string (std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>());
}

std::vector<std::string> zipListing (const std::string& zipPath)
{
    std::vector<std::string> entries;
    const std::string command = "unzip -Z1 \"" + zipPath + "\"";
    FILE* pipe = popen (command.c_str(), "r");

    if (pipe == nullptr)
        fail ("popen unzip -Z1 failed");

    char line[1024];

    while (std::fgets (line, sizeof (line), pipe) != nullptr)
    {
        std::string entry (line);

        while (! entry.empty() && (entry.back() == '\n' || entry.back() == '\r'))
            entry.pop_back();

        if (! entry.empty())
            entries.push_back (entry);
    }

    if (pclose (pipe) != 0)
        fail ("unzip -Z1 failed on " + zipPath);

    return entries;
}
} // namespace

int main()
{
    const std::string sysroot = requireEnv ("ONDEVICE_SYSROOT");
    const std::string resourceDir = requireEnv ("ONDEVICE_RESOURCE_DIR");
    const std::string builtins = requireEnv ("ONDEVICE_BUILTINS");

#ifndef ONDEVICE_FIXTURE_DIR
#error ONDEVICE_FIXTURE_DIR must be defined to tests/fixtures
#endif
    const fs::path fixtures = ONDEVICE_FIXTURE_DIR;
    const auto manifestPath = fixtures / "mini-manifest.json";

    if (! fs::is_regular_file (manifestPath))
        fail ("missing " + manifestPath.string());

    const auto work = fs::temp_directory_path() / ("ondevice-engine-" + std::to_string (::getpid()));
    fs::remove_all (work);

    ondevice::EngineRequest request;
    request.projectRoot = fixtures.string();
    request.manifestJson = readFile (manifestPath);
    request.workDirectory = work.string();
    request.sysroot = sysroot;
    request.resourceDir = resourceDir;
    request.builtinsArchive = builtins;
    request.threads = 2;
    request.onProgress = [] (const std::string& line)
    {
        std::cout << line << '\n';
    };

    const auto built = ondevice::buildSignedIpa (request);

    std::cout << "compile " << built.compileSeconds << " s, peak rss "
              << (built.peakResidentBytes / 1048576ull) << " MB\n";

    if (! built.success)
        fail ("buildSignedIpa failed: " + built.failureMessage);

    const auto execPath = fs::path (built.appFolder) / "MiniEngine";
    const auto info = ondevice::inspectMachO (execPath.string());

    std::cout << "linked: " << info.describe() << '\n';

    if (! info.isIOSArm64Executable())
        fail ("not an arm64 iOS executable: " + info.describe());

    bool found = false;

    for (const auto& entry : zipListing (built.ipaPath))
    {
        if (entry == "Payload/MiniEngine.app/MiniEngine")
            found = true;
    }

    if (! found)
        fail ("IPA missing Payload/MiniEngine.app/MiniEngine");

    fs::remove_all (work);
    std::cout << "PASS: mini engine skip-sign IPA\n";
    return 0;
}
