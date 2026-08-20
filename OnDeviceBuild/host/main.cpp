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

void usage (const char* argv0)
{
    std::cerr <<
        "usage: " << argv0 << " \\\n"
        "  --manifest <json> \\\n"
        "  --root <projectRoot> \\\n"
        "  --sysroot <sdk> \\\n"
        "  --resource-dir <clang-resource> \\\n"
        "  --builtins <libclang_rt.ios.a> \\\n"
        "  [--p12 <modern.p12> --password <pw> --provision <mobileprovision>] \\\n"
        "  [--work <dir>] [--threads N] [--skip-sign]\n";
}

bool takeValue (int argc, char** argv, int& i, const char* flag, std::string& out)
{
    if (std::strcmp (argv[i], flag) != 0)
        return false;

    if (i + 1 >= argc)
    {
        std::cerr << flag << " requires a value\n";
        std::exit (2);
    }

    out = argv[++i];
    return true;
}

std::string readFile (const std::string& path)
{
    std::ifstream in (path, std::ios::binary);

    if (! in)
        return {};

    return std::string (std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>());
}

std::vector<std::string> zipListing (const std::string& zipPath)
{
    std::vector<std::string> entries;
    const std::string command = "unzip -Z1 \"" + zipPath + "\"";
    FILE* pipe = popen (command.c_str(), "r");

    if (pipe == nullptr)
        return entries;

    char line[1024];

    while (std::fgets (line, sizeof (line), pipe) != nullptr)
    {
        std::string entry (line);

        while (! entry.empty() && (entry.back() == '\n' || entry.back() == '\r'))
            entry.pop_back();

        if (! entry.empty())
            entries.push_back (entry);
    }

    pclose (pipe);
    return entries;
}

std::string appNameFromFolder (const std::string& appFolder)
{
    auto name = fs::path (appFolder).filename().string();

    if (name.size() > 4 && name.substr (name.size() - 4) == ".app")
        name.resize (name.size() - 4);

    return name;
}
} // namespace

int main (int argc, char** argv)
{
    std::string manifestPath, root, sysroot, resourceDir, builtins;
    std::string p12, password, provision, work;
    std::string threadsText;
    bool skipSign = false;

    for (int i = 1; i < argc; ++i)
    {
        if (takeValue (argc, argv, i, "--manifest", manifestPath)) continue;
        if (takeValue (argc, argv, i, "--root", root)) continue;
        if (takeValue (argc, argv, i, "--sysroot", sysroot)) continue;
        if (takeValue (argc, argv, i, "--resource-dir", resourceDir)) continue;
        if (takeValue (argc, argv, i, "--builtins", builtins)) continue;
        if (takeValue (argc, argv, i, "--p12", p12)) continue;
        if (takeValue (argc, argv, i, "--password", password)) continue;
        if (takeValue (argc, argv, i, "--provision", provision)) continue;
        if (takeValue (argc, argv, i, "--work", work)) continue;
        if (takeValue (argc, argv, i, "--threads", threadsText)) continue;

        if (std::strcmp (argv[i], "--skip-sign") == 0)
        {
            skipSign = true;
            continue;
        }

        std::cerr << "unknown argument: " << argv[i] << '\n';
        usage (argv[0]);
        return 2;
    }

    if (manifestPath.empty() || root.empty() || sysroot.empty()
        || resourceDir.empty() || builtins.empty())
    {
        usage (argv[0]);
        return 2;
    }

    if (work.empty())
        work = (fs::temp_directory_path() / ("ondevice-host-" + std::to_string (::getpid()))).string();

    const auto manifestJson = readFile (manifestPath);

    if (manifestJson.empty())
    {
        std::cerr << "could not read manifest: " << manifestPath << '\n';
        return 1;
    }

    ondevice::EngineRequest request;
    request.projectRoot = root;
    request.manifestJson = manifestJson;
    request.workDirectory = work;
    request.sysroot = sysroot;
    request.resourceDir = resourceDir;
    request.builtinsArchive = builtins;
    request.threads = threadsText.empty() ? 1 : std::atoi (threadsText.c_str());
    request.onProgress = [] (const std::string& line)
    {
        std::printf ("%s\n", line.c_str());
        std::fflush (stdout);
    };

    if (! skipSign)
    {
        request.p12Path = p12;
        request.p12Password = password;
        request.provisionPath = provision;
    }

    const auto built = ondevice::buildSignedIpa (request);

    std::printf ("compile %.1f s, peak rss %.0f MB\n",
                 built.compileSeconds, built.peakResidentBytes / 1048576.0);
    std::fflush (stdout);

    if (! built.success)
    {
        std::printf ("BUILD FAILED\n%s\n", built.failureMessage.c_str());

        if (! built.linkerCanRunAgain)
            std::printf ("linkerCanRunAgain=false\n");

        return 1;
    }

    const auto name = appNameFromFolder (built.appFolder);
    const auto execPath = (fs::path (built.appFolder) / name).string();
    const auto info = ondevice::inspectMachO (execPath);

    std::printf ("linked: %s\n", info.describe().c_str());

    if (! info.isIOSArm64Executable())
    {
        std::printf ("FAIL: not an arm64 iOS executable\n");
        return 1;
    }

    const auto expected = std::string ("Payload/") + name + ".app/" + name;
    bool found = false;

    for (const auto& entry : zipListing (built.ipaPath))
    {
        if (entry == expected)
            found = true;
    }

    if (! found)
    {
        std::printf ("FAIL: IPA is missing %s\n", expected.c_str());
        return 1;
    }

    std::printf ("PASS: %s  %s\n", execPath.c_str(), built.ipaPath.c_str());
    return 0;
}
