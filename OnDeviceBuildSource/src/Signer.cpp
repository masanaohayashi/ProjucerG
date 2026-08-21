#include "OnDeviceBuild/Signer.h"

#include "bundle.h"
#include "common/archive.h"
#include "openssl.h"
#include "common/log.h"

#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <cstdio>
#include <cstdlib>

namespace
{
/** Redirects stdout to a temporary file for as long as it is alive.

    zsign reports everything - including why a signature was rejected - through
    ZLog, which writes straight to stdout and offers no way to intercept it. On
    a device there is no console to read, so the descriptor is swapped for the
    duration of the call and the text handed back to the caller instead.
*/
class CapturedStdout
{
public:
    explicit CapturedStdout (std::string temporaryPath) : path (std::move (temporaryPath))
    {
        std::fflush (stdout);
        savedDescriptor = dup (STDOUT_FILENO);
        const int file = open (path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);

        if (file >= 0)
        {
            dup2 (file, STDOUT_FILENO);
            close (file);
        }
    }

    ~CapturedStdout()
    {
        std::fflush (stdout);

        if (savedDescriptor >= 0)
        {
            dup2 (savedDescriptor, STDOUT_FILENO);
            close (savedDescriptor);
        }
    }

    std::string read() const
    {
        std::fflush (stdout);
        std::string text;

        if (auto* file = std::fopen (path.c_str(), "rb"))
        {
            char buffer[4096];

            while (const auto n = std::fread (buffer, 1, sizeof (buffer), file))
                text.append (buffer, n);

            std::fclose (file);
        }

        return text;
    }

private:
    std::string path;
    int savedDescriptor = -1;
};
} // namespace

namespace ondevice {

SignResult signAppFolder (const SignRequest& request)
{
    SignResult result;

    ZLog::SetLogLever (ZLog::E_INFO);

    // Not next to the bundle: anything left beside the .app ends up inside
    // Payload/ when the ipa is built, and installd will not accept a Payload
    // directory with a stray file in it.
    const char* temporaryDirectory = getenv ("TMPDIR");
    CapturedStdout captured (std::string (temporaryDirectory != nullptr ? temporaryDirectory : "/tmp")
                                 + "/zsign-output.txt");

    ZSignAsset asset;

    const bool initialised = asset.Init (/*cert*/       std::string(),
                                         /*privateKey*/ request.p12Path,
                                         /*provision*/  request.provisionPath,
                                         /*entitlements, taken from the profile*/ std::string(),
                                         request.password,
                                         /*adhoc*/       false,
                                         /*sha256Only*/  false,
                                         /*singleBinary*/false);

    if (initialised)
    {
        ZBundle bundle;
        const std::vector<std::string> noDylibs;

        result.success = bundle.SignFolder (&asset,
                                            request.appFolder,
                                            request.bundleId,
                                            /*bundleVersion*/ std::string(),
                                            /*displayName*/   std::string(),
                                            noDylibs,
                                            noDylibs,
                                            /*force*/         true,
                                            /*weakInject*/    false,
                                            /*enableCache*/   false);
    }

    result.log = captured.read();

    if (! initialised)
        result.log += "\nZSignAsset::Init failed - certificate, key, password or profile is wrong\n";

    return result;
}

bool extractZip (const std::string& zipFile, const std::string& outputFolder)
{
    return Zip::Extract (zipFile.c_str(), outputFolder.c_str());
}

bool archiveFolder (const std::string& folder, const std::string& zipFile)
{
    // Level 0: an ipa is opened once by installd and thrown away, so the time
    // spent compressing it is pure cost.
    return Zip::Archive (folder, zipFile, 0);
}

} // namespace ondevice
