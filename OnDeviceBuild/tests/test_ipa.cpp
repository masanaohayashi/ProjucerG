#include "OnDeviceBuild/BundleBuilder.h"
#include "OnDeviceBuild/Compiler.h"
#include "OnDeviceBuild/Linker.h"
#include "OnDeviceBuild/Pkcs12.h"
#include "OnDeviceBuild/Signer.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
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

std::string envOr (const char* name, const std::string& fallback)
{
    const char* value = std::getenv (name);
    return (value != nullptr && value[0] != '\0') ? std::string (value) : fallback;
}

std::string homePath()
{
    const char* home = std::getenv ("HOME");
    return home != nullptr ? std::string (home) : std::string();
}

bool executableBitSet (const fs::path& path)
{
    struct stat s = {};
    return ::stat (path.c_str(), &s) == 0 && (s.st_mode & S_IXUSR) != 0;
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

    const int status = pclose (pipe);

    if (status != 0)
        fail ("unzip -Z1 failed on " + zipPath);

    return entries;
}

ondevice::CompileResult compileOne (const std::string& sourcePath,
                                    const std::string& outputPath,
                                    const std::string& resourceDir,
                                    const std::string& sysroot,
                                    const std::vector<std::string>& extraArgs)
{
    ondevice::CompileRequest request;
    request.sourcePath = sourcePath;
    request.outputPath = outputPath;
    request.resourceDir = resourceDir;
    request.sysroot = sysroot;
    request.extraArgs = extraArgs;
    return ondevice::compileToObject (request);
}

std::string linkIosExecutable (const fs::path& work)
{
    const std::string sysroot = requireEnv ("ONDEVICE_SYSROOT");
    const std::string resourceDir = requireEnv ("ONDEVICE_RESOURCE_DIR");
    const std::string builtins = requireEnv ("ONDEVICE_BUILTINS");

#ifndef ONDEVICE_FIXTURE_UIKIT
#error ONDEVICE_FIXTURE_UIKIT must be defined to the uikit.mm fixture path
#endif
#ifndef ONDEVICE_FIXTURE_LINK_MAIN
#error ONDEVICE_FIXTURE_LINK_MAIN must be defined to the link_main.cpp fixture path
#endif
    const std::string uikitPath = ONDEVICE_FIXTURE_UIKIT;
    const std::string mainPath = ONDEVICE_FIXTURE_LINK_MAIN;
    const auto uikitObj = work / "uikit.o";
    const auto mainObj = work / "main.o";
    const auto exePath = work / "HelloIpa";

    const auto compiledUikit = compileOne (uikitPath, uikitObj.string(), resourceDir, sysroot,
                                           { "-x", "objective-c++", "-std=c++17" });

    if (! compiledUikit.success)
        fail ("compileToObject failed for uikit.mm: " + compiledUikit.diagnostics);

    const auto compiledMain = compileOne (mainPath, mainObj.string(), resourceDir, sysroot,
                                          { "-std=c++17" });

    if (! compiledMain.success)
        fail ("compileToObject failed for link_main.cpp: " + compiledMain.diagnostics);

    ondevice::LinkRequest link;
    link.objectFiles = { uikitObj.string(), mainObj.string() };
    link.outputPath = exePath.string();
    link.sysroot = sysroot;
    link.frameworks = { "UIKit", "Foundation" };
    link.builtinsArchive = builtins;

    const auto linked = ondevice::linkObjects (link);

    if (! linked.success)
        fail ("linkObjects failed: " + linked.diagnostics);

    return exePath.string();
}

void assertPayloadOnly (const std::vector<std::string>& entries)
{
    if (entries.empty())
        fail ("zip listing is empty");

    bool anyPayloadFile = false;

    for (const auto& entry : entries)
    {
        std::cout << "zip: " << entry << '\n';
        const bool under = entry == "Payload"
                           || entry == "Payload/"
                           || entry.rfind ("Payload/", 0) == 0;

        if (! under)
            fail ("zip entry is not under Payload/: " + entry);

        if (entry.rfind ("Payload/", 0) == 0)
            anyPayloadFile = true;
    }

    if (! anyPayloadFile)
        fail ("no zip entry starts with Payload/");
}

bool opensslCanReadWithoutLegacy (const std::string& p12Path, const std::string& password)
{
    const std::string command = "openssl pkcs12 -in \"" + p12Path
                                  + "\" -passin pass:" + password
                                  + " -nokeys -noout >/dev/null 2>&1";
    return std::system (command.c_str()) == 0;
}
} // namespace

int main()
{
    const auto pid = std::to_string (::getpid());
    const auto work = fs::temp_directory_path() / ("ondevice-ipa-" + pid);
    fs::remove_all (work);
    fs::create_directories (work);

    const auto exePath = linkIosExecutable (work);
    const auto appFolder = work / "HelloIpa.app";
    const std::string bundleId = "tokyo.studio-r.helloinstall";

    ondevice::BundleRequest bundle;
    bundle.appFolder = appFolder.string();
    bundle.executablePath = exePath;
    bundle.bundleId = bundleId;
    bundle.name = "HelloIpa";

    std::string error;

    if (! ondevice::writeAppBundle (bundle, error))
        fail ("writeAppBundle: " + error);

    const auto bundledExec = appFolder / "HelloIpa";

    if (! executableBitSet (bundledExec))
        fail ("executable bit lost on " + bundledExec.string() + " before zip");

    ondevice::BundleRequest simulatorBundle = bundle;
    simulatorBundle.appFolder = (work / "HelloIpa-simulator.app").string();
    simulatorBundle.simulator = true;

    if (! ondevice::writeAppBundle (simulatorBundle, error))
        fail ("writeAppBundle (simulator): " + error);

    std::ifstream simulatorPlist (work / "HelloIpa-simulator.app" / "Info.plist");
    const std::string simulatorPlistText ((std::istreambuf_iterator<char> (simulatorPlist)), {});

    if (simulatorPlistText.find ("<string>iPhoneSimulator</string>") == std::string::npos
        || simulatorPlistText.find ("<string>iphonesimulator</string>") == std::string::npos)
        fail ("Simulator bundle has device platform metadata");

    std::cout << "PASS: Simulator bundle metadata\n";

    const auto unsignedIpa = work.parent_path() / ("ondevice-unsigned-" + pid + ".ipa");
    fs::remove (unsignedIpa);

    if (! ondevice::writeIpa (appFolder.string(), unsignedIpa.string(), error))
        fail ("writeIpa: " + error);

    assertPayloadOnly (zipListing (unsignedIpa.string()));

    const auto extracted = work / "extracted-unsigned";
    fs::create_directories (extracted);

    if (! ondevice::extractZip (unsignedIpa.string(), extracted.string()))
        fail ("extractZip failed");

    const auto extractedApp = extracted / "Payload" / "HelloIpa.app";

    if (! fs::is_directory (extractedApp))
        fail ("extracted ipa has no Payload/HelloIpa.app");

    std::cout << "PASS: unsigned IPA is Payload-rooted\n";

    const auto defaultP12 = homePath() + "/Documents/src/PocFullChain/assets/identities-modern.p12";
    const auto defaultProv = homePath() + "/Documents/src/PocFullChain/assets/dev.mobileprovision";
    const auto p12 = envOr ("ONDEVICE_P12", defaultP12);
    const auto provision = envOr ("ONDEVICE_PROVISION", defaultProv);
    const auto password = envOr ("ONDEVICE_P12_PASSWORD", "nagasaku");
    const bool haveSigningAssets = fs::is_regular_file (p12) && fs::is_regular_file (provision);

    if (haveSigningAssets)
    {
        const auto modernP12 = work / "roundtrip.p12";

        if (! ondevice::reencodePkcs12Aes (p12, modernP12.string(), password, error))
            fail ("reencodePkcs12Aes (modern fixture): " + error);

        if (! opensslCanReadWithoutLegacy (modernP12.string(), password))
            fail ("reencoded p12 is not readable without the OpenSSL legacy provider");

        std::cout << "PASS: PKCS#12 AES round-trip\n";

        const auto rc2Default = homePath() + "/Documents/src/PocSignIOS/assets/identities.p12";
        const auto rc2Path = envOr ("ONDEVICE_RC2_P12", rc2Default);

        if (fs::is_regular_file (rc2Path))
        {
            const auto fromRc2 = work / "from-rc2.p12";

            if (! ondevice::reencodePkcs12Aes (rc2Path, fromRc2.string(), password, error))
                fail ("reencodePkcs12Aes (RC2 fixture): " + error);

            if (! opensslCanReadWithoutLegacy (fromRc2.string(), password))
                fail ("RC2 reencode is not readable without the OpenSSL legacy provider");

            std::cout << "PASS: RC2 PKCS#12 reencoded to AES\n";
        }
        else
        {
            std::cout << "NOTE: no Keychain-style RC2 p12 on disk (" << rc2Path
                      << "); skipping RC2 reencode case\n";
        }

        ondevice::SignRequest sign;
        sign.appFolder = appFolder.string();
        sign.p12Path = p12;
        sign.password = password;
        sign.provisionPath = provision;
        sign.bundleId = bundleId;

        const auto signedResult = ondevice::signAppFolder (sign);
        std::cout << signedResult.log << '\n';

        if (! signedResult.success)
            fail ("signAppFolder failed while ONDEVICE_P12 / ONDEVICE_PROVISION exist");

        const auto signedIpa = work.parent_path() / ("ondevice-signed-" + pid + ".ipa");
        fs::remove (signedIpa);

        if (! ondevice::writeIpa (appFolder.string(), signedIpa.string(), error))
            fail ("writeIpa after sign: " + error);

        assertPayloadOnly (zipListing (signedIpa.string()));
        fs::remove (signedIpa);
        std::cout << "PASS: signed IPA is Payload-rooted\n";
    }
    else
    {
        std::cout << "NOTE: signing assets missing (looked for " << p12 << " and "
                  << provision << "); skipping sign\n";
    }

    fs::remove (unsignedIpa);
    fs::remove_all (work);
    std::cout << "PASS: bundle / zip / IPA layout\n";
    return 0;
}
