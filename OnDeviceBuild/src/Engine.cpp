#include "OnDeviceBuild/Engine.h"
#include "OnDeviceBuild/BundleBuilder.h"
#include "OnDeviceBuild/Compiler.h"
#include "OnDeviceBuild/Linker.h"
#include "OnDeviceBuild/MachOCheck.h"
#include "OnDeviceBuild/Signer.h"
#include "BuildRunner.h"

#include <algorithm>
#include <filesystem>
#include <thread>

namespace ondevice {

namespace
{
namespace fs = std::filesystem;

int defaultThreadCount()
{
    const unsigned cores = std::thread::hardware_concurrency();
    const int half = cores == 0 ? 1 : (int) cores / 2;
    return std::max (1, std::min (4, half));
}

void progress (const EngineRequest& request, const std::string& line)
{
    if (request.onProgress)
        request.onProgress (line);
}

void notePeak (EngineResult& result)
{
    result.peakResidentBytes = std::max (result.peakResidentBytes, getResidentMemoryBytes());
}
} // namespace

EngineResult buildSignedIpa (const EngineRequest& request)
{
    EngineResult result;

    if (request.projectRoot.empty() || request.workDirectory.empty())
    {
        result.failureMessage = "projectRoot and workDirectory are required";
        return result;
    }

    const auto manifest = parseManifestJson (request.manifestJson);

    if (! manifest.ok)
    {
        result.failureMessage = manifest.error.empty() ? "manifest parse failed" : manifest.error;
        return result;
    }

    std::error_code ec;
    fs::create_directories (request.workDirectory, ec);

    if (ec)
    {
        result.failureMessage = "cannot create work directory: " + ec.message();
        return result;
    }

    const int threads = request.threads <= 0 ? defaultThreadCount() : request.threads;

    CompileManifestRequest compile;
    compile.projectRoot = request.projectRoot;
    compile.manifestJson = request.manifestJson;
    compile.workDirectory = request.workDirectory;
    compile.sysroot = request.sysroot;
    compile.resourceDir = request.resourceDir;
    compile.minimumOSVersion = manifest.minimumOSVersion;
    compile.threads = threads;
    compile.onProgress = request.onProgress;

    progress (request, "compiling on " + std::to_string (threads) + " thread(s)");

    const auto compiled = compileManifest (compile);
    result.compileSeconds = compiled.seconds;
    result.peakResidentBytes = compiled.peakResidentBytes;

    if (! compiled.success)
    {
        result.failureMessage = compiled.failureMessage.empty() ? "compile failed" : compiled.failureMessage;
        return result;
    }

    progress (request, "compiled in " + std::to_string (compiled.seconds)
                          + " s, peak rss "
                          + std::to_string (compiled.peakResidentBytes / 1048576ull) + " MB");

    const auto linkedPath = (fs::path (request.workDirectory) / manifest.name).string();

    LinkRequest link;
    link.objectFiles = compiled.objectFiles;
    link.outputPath = linkedPath;
    link.sysroot = request.sysroot;
    link.minimumOSVersion = manifest.minimumOSVersion;
    link.sdkVersion = manifest.minimumOSVersion;
    link.frameworks = manifest.frameworks;
    link.builtinsArchive = request.builtinsArchive;

    if (! manifest.libraries.empty())
        link.libraries = manifest.libraries;

    progress (request, "linking " + manifest.name);

    const auto linked = linkObjects (link);
    result.linkerCanRunAgain = linked.canRunAgain;
    notePeak (result);

    if (! linked.canRunAgain)
    {
        result.failureMessage = linked.diagnostics.empty()
                                    ? "linker cannot run again"
                                    : linked.diagnostics;
        return result;
    }

    if (! linked.success)
    {
        result.failureMessage = linked.diagnostics.empty() ? "link failed" : linked.diagnostics;
        return result;
    }

    const auto appFolder = (fs::path (request.workDirectory) / (manifest.name + ".app")).string();

    BundleRequest bundle;
    bundle.appFolder = appFolder;
    bundle.executablePath = linkedPath;
    bundle.bundleId = manifest.bundleId;
    bundle.name = manifest.name;
    bundle.minimumOSVersion = manifest.minimumOSVersion;

    progress (request, "writing app bundle");

    std::string error;

    if (! writeAppBundle (bundle, error))
    {
        result.failureMessage = error;
        return result;
    }

    result.appFolder = appFolder;

    const auto bundledExec = (fs::path (appFolder) / manifest.name).string();
    const auto macho = inspectMachO (bundledExec);

    if (! macho.isIOSArm64Executable())
    {
        result.failureMessage = "linked binary is not an arm64 iOS executable: " + macho.describe();
        return result;
    }

    progress (request, std::string ("linked: ") + macho.describe());

    const bool skipSign = request.p12Path.empty() || request.provisionPath.empty();

    if (! skipSign)
    {
        SignRequest sign;
        sign.appFolder = appFolder;
        sign.p12Path = request.p12Path;
        sign.password = request.p12Password;
        sign.provisionPath = request.provisionPath;
        sign.bundleId = manifest.bundleId;

        progress (request, "signing");

        const auto signedResult = signAppFolder (sign);
        notePeak (result);

        if (! signedResult.success)
        {
            result.failureMessage = signedResult.log.empty() ? "sign failed" : signedResult.log;
            return result;
        }

        progress (request, "signed");
    }
    else
    {
        progress (request, "skipping sign");
    }

    const auto ipaPath = (fs::path (request.workDirectory) / (manifest.name + ".ipa")).string();
    progress (request, "writing IPA");

    if (! writeIpa (appFolder, ipaPath, error))
    {
        result.failureMessage = error;
        return result;
    }

    result.ipaPath = ipaPath;
    notePeak (result);

    const auto afterIpa = inspectMachO (bundledExec);

    if (! afterIpa.isIOSArm64Executable())
    {
        result.failureMessage = "executable is not arm64 iOS after IPA: " + afterIpa.describe();
        return result;
    }

    result.success = true;
    return result;
}

} // namespace ondevice
