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

bool isCancelled (const EngineRequest& request)
{
    return request.shouldCancel && request.shouldCancel();
}

void cancel (EngineResult& result)
{
    result.cancelled = true;
    result.failureMessage = "build cancelled";
}
} // namespace

EngineResult buildSignedIpa (const EngineRequest& request)
{
    EngineResult result;

    if (isCancelled (request))
    {
        cancel (result);
        return result;
    }

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
    compile.simulator = request.simulator;
    compile.sysroot = request.sysroot;
    compile.resourceDir = request.resourceDir;
    compile.minimumOSVersion = manifest.minimumOSVersion;
    compile.threads = threads;
    compile.onProgress = request.onProgress;
    compile.shouldCancel = request.shouldCancel;

    progress (request, "compiling on " + std::to_string (threads) + " thread(s)");

    const auto compiled = compileManifest (compile);
    result.compileSeconds = compiled.seconds;
    result.peakResidentBytes = compiled.peakResidentBytes;

    if (! compiled.success)
    {
        if (compiled.cancelled || isCancelled (request))
        {
            cancel (result);
            return result;
        }

        result.failureMessage = compiled.failureMessage.empty() ? "compile failed" : compiled.failureMessage;
        return result;
    }

    if (isCancelled (request))
    {
        cancel (result);
        return result;
    }

    progress (request, "compiled in " + std::to_string (compiled.seconds)
                          + " s, peak rss "
                          + std::to_string (compiled.peakResidentBytes / 1048576ull) + " MB");

    const auto linkedPath = (fs::path (request.workDirectory) / manifest.name).string();

    LinkRequest link;
    for (const auto& object : compiled.objectFiles)
        if (! object.empty())
            link.objectFiles.push_back (object);
    link.outputPath = linkedPath;
    link.simulator = request.simulator;
    link.sysroot = request.sysroot;
    link.minimumOSVersion = manifest.minimumOSVersion;
    link.sdkVersion = manifest.minimumOSVersion;
    link.frameworks = manifest.frameworks;
    link.builtinsArchive = request.builtinsArchive;

    if (! manifest.libraries.empty())
        link.libraries = manifest.libraries;

    progress (request, "linking " + manifest.name);

    if (isCancelled (request))
    {
        cancel (result);
        return result;
    }

    const auto linked = linkObjects (link);
    result.linkerCanRunAgain = linked.canRunAgain;
    notePeak (result);

    if (isCancelled (request))
    {
        cancel (result);
        return result;
    }

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
    bundle.simulator = request.simulator;

    progress (request, "writing app bundle");

    std::string error;

    if (! writeAppBundle (bundle, error))
    {
        result.failureMessage = error;
        return result;
    }

    result.appFolder = appFolder;

    if (isCancelled (request))
    {
        cancel (result);
        return result;
    }

    const auto bundledExec = (fs::path (appFolder) / manifest.name).string();
    const auto macho = inspectMachO (bundledExec);

    const auto isExpectedExecutable = request.simulator
                                        ? macho.isIosSimulatorArm64Executable()
                                        : macho.isIOSArm64Executable();

    if (! isExpectedExecutable)
    {
        result.failureMessage = "linked binary is not an arm64 iOS executable: " + macho.describe();
        return result;
    }

    progress (request, std::string ("linked: ") + macho.describe());

    const bool skipSign = request.simulator
                       || request.p12Path.empty()
                       || request.provisionPath.empty();

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
        progress (request, request.simulator ? "skipping sign (simulator)" : "skipping sign");
    }

    if (isCancelled (request))
    {
        cancel (result);
        return result;
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

    const auto isExpectedIpaExecutable = request.simulator
                                           ? afterIpa.isIosSimulatorArm64Executable()
                                           : afterIpa.isIOSArm64Executable();

    if (! isExpectedIpaExecutable)
    {
        result.failureMessage = "executable is not arm64 iOS after IPA: " + afterIpa.describe();
        return result;
    }

    result.success = true;
    return result;
}

} // namespace ondevice
