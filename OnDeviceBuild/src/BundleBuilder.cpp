#include "OnDeviceBuild/BundleBuilder.h"
#include "OnDeviceBuild/Signer.h"

#include "unzip.h"

#include <climits>
#include <sys/syslimits.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace ondevice {

namespace
{
namespace fs = std::filesystem;

std::string xmlEscape (const std::string& text)
{
    std::string out;
    out.reserve (text.size());

    for (const char c : text)
    {
        switch (c)
        {
            case '&':  out += "&amp;"; break;
            case '<':  out += "&lt;"; break;
            case '>':  out += "&gt;"; break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c; break;
        }
    }

    return out;
}

std::string leafName (const std::string& path)
{
    return fs::path (path).filename().string();
}

bool writeFile (const fs::path& path, const std::string& contents, std::string& error)
{
    std::ofstream out (path, std::ios::binary | std::ios::trunc);

    if (! out)
    {
        error = "cannot write " + path.string();
        return false;
    }

    out << contents;
    return true;
}

bool zipEntriesValid (const std::string& zipFile, std::string& error)
{
    unzFile zip = unzOpen64 (zipFile.c_str());

    if (zip == nullptr)
    {
        error = "cannot open zip: " + zipFile;
        return false;
    }

    unz_global_info64 global {};

    if (unzGetGlobalInfo64 (zip, &global) != UNZ_OK)
    {
        unzClose (zip);
        error = "unzGetGlobalInfo64 failed: " + zipFile;
        return false;
    }

    bool anyPayload = false;

    for (ZPOS64_T i = 0; i < global.number_entry; ++i)
    {
        char name[PATH_MAX] = {};
        unz_file_info64 info {};

        if (unzGetCurrentFileInfo64 (zip, &info, name, sizeof (name), nullptr, 0, nullptr, 0) != UNZ_OK)
        {
            unzClose (zip);
            error = "unzGetCurrentFileInfo64 failed: " + zipFile;
            return false;
        }

        const std::string entry (name);
        const bool underPayload = entry == "Payload"
                                  || entry == "Payload/"
                                  || entry.rfind ("Payload/", 0) == 0;

        if (! underPayload)
        {
            unzClose (zip);
            error = "zip entry is not under Payload/: " + entry;
            return false;
        }

        if (entry.rfind ("Payload/", 0) == 0)
            anyPayload = true;

        if (i + 1 < global.number_entry && unzGoToNextFile (zip) != UNZ_OK)
        {
            unzClose (zip);
            error = "unzGoToNextFile failed: " + zipFile;
            return false;
        }
    }

    unzClose (zip);

    if (! anyPayload)
    {
        error = "zip has no entries starting with Payload/";
        return false;
    }

    return true;
}
} // namespace

bool writeAppBundle (const BundleRequest& request, std::string& error)
{
    error.clear();

    if (request.appFolder.empty() || request.name.empty() || request.bundleId.empty())
    {
        error = "appFolder, name and bundleId are required";
        return false;
    }

    if (request.name.find ('/') != std::string::npos)
    {
        error = "name must be a single path component";
        return false;
    }

    if (request.executablePath.empty() || ! fs::is_regular_file (request.executablePath))
    {
        error = "executable is missing: " + request.executablePath;
        return false;
    }

    std::error_code ec;
    fs::create_directories (request.appFolder, ec);

    if (ec)
    {
        error = "cannot create app folder: " + request.appFolder + " (" + ec.message() + ")";
        return false;
    }

    const auto executableDest = fs::path (request.appFolder) / request.name;
    fs::copy_file (request.executablePath, executableDest, fs::copy_options::overwrite_existing, ec);

    if (ec)
    {
        error = "cannot copy executable: " + ec.message();
        return false;
    }

    if (::chmod (executableDest.c_str(), 0755) != 0)
    {
        error = "chmod 0755 failed on " + executableDest.string() + ": " + std::strerror (errno);
        return false;
    }

    const auto minOS = request.minimumOSVersion.empty() ? "17.0" : request.minimumOSVersion;
    const auto escapedName = xmlEscape (request.name);
    const auto escapedId = xmlEscape (request.bundleId);

    std::ostringstream plist;
    plist <<
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "\t<key>CFBundleIdentifier</key>\n"
        "\t<string>" << escapedId << "</string>\n"
        "\t<key>CFBundleExecutable</key>\n"
        "\t<string>" << escapedName << "</string>\n"
        "\t<key>CFBundleName</key>\n"
        "\t<string>" << escapedName << "</string>\n"
        "\t<key>CFBundleDisplayName</key>\n"
        "\t<string>" << escapedName << "</string>\n"
        "\t<key>CFBundleVersion</key>\n"
        "\t<string>1</string>\n"
        "\t<key>CFBundleShortVersionString</key>\n"
        "\t<string>1.0</string>\n"
        "\t<key>CFBundlePackageType</key>\n"
        "\t<string>APPL</string>\n"
        "\t<key>CFBundleInfoDictionaryVersion</key>\n"
        "\t<string>6.0</string>\n"
        "\t<key>MinimumOSVersion</key>\n"
        "\t<string>" << xmlEscape (minOS) << "</string>\n"
        "\t<key>UIDeviceFamily</key>\n"
        "\t<array>\n"
        "\t\t<integer>1</integer>\n"
        "\t\t<integer>2</integer>\n"
        "\t</array>\n"
        "\t<key>CFBundleSupportedPlatforms</key>\n"
        "\t<array>\n"
        "\t\t<string>iPhoneOS</string>\n"
        "\t</array>\n"
        "\t<key>DTPlatformName</key>\n"
        "\t<string>iphoneos</string>\n"
        "\t<key>UILaunchScreen</key>\n"
        "\t<dict/>\n"
        "\t<key>UIRequiredDeviceCapabilities</key>\n"
        "\t<array>\n"
        "\t\t<string>arm64</string>\n"
        "\t</array>\n"
        "</dict>\n"
        "</plist>\n";

    if (! writeFile (fs::path (request.appFolder) / "Info.plist", plist.str(), error))
        return false;

    return true;
}

bool writeIpa (const std::string& appFolder, const std::string& ipaPath, std::string& error)
{
    error.clear();

    if (appFolder.empty() || ipaPath.empty())
    {
        error = "appFolder and ipaPath are required";
        return false;
    }

    if (! fs::is_directory (appFolder))
    {
        error = "app folder is missing: " + appFolder;
        return false;
    }

    const auto appName = leafName (appFolder);

    if (appName.size() < 5 || appName.substr (appName.size() - 4) != ".app")
    {
        error = "appFolder must end in .app: " + appFolder;
        return false;
    }

    const auto ipaAbs = fs::absolute (ipaPath);
    const auto appAbs = fs::absolute (appFolder);

    if (ipaAbs.string().rfind (appAbs.string() + "/", 0) == 0)
    {
        error = "ipaPath must be outside the app folder being packaged";
        return false;
    }

    std::error_code ec;
    fs::create_directories (ipaAbs.parent_path(), ec);

    if (ec)
    {
        error = "cannot create ipa parent directory: " + ec.message();
        return false;
    }

    const auto stageRoot = fs::temp_directory_path()
                               / ("ondevice-ipa-" + std::to_string (::getpid()) + "-" + appName);

    fs::remove_all (stageRoot, ec);
    const auto payloadDir = stageRoot / "Payload";
    fs::create_directories (payloadDir, ec);

    if (ec)
    {
        error = "cannot create Payload staging directory: " + ec.message();
        return false;
    }

    const auto stagedApp = payloadDir / appName;
    fs::copy (appAbs, stagedApp, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);

    if (ec)
    {
        fs::remove_all (stageRoot);
        error = "cannot copy app into Payload/: " + ec.message();
        return false;
    }

    const auto stagedExec = stagedApp / appName.substr (0, appName.size() - 4);

    if (fs::is_regular_file (stagedExec))
        ::chmod (stagedExec.c_str(), 0755);

    const auto ipaAbsStr = ipaAbs.string();
    const auto stageStr = stageRoot.string();

    if (ipaAbsStr.rfind (stageStr + "/", 0) == 0 || ipaAbsStr == stageStr)
    {
        fs::remove_all (stageRoot);
        error = "ipaPath must be outside the tree being zipped";
        return false;
    }

    fs::remove (ipaAbs, ec);

    if (! archiveFolder (stageStr, ipaAbsStr))
    {
        fs::remove_all (stageRoot);
        error = "archiveFolder failed for " + stageStr;
        return false;
    }

    fs::remove_all (stageRoot);

    if (! zipEntriesValid (ipaAbsStr, error))
    {
        fs::remove (ipaAbs);
        return false;
    }

    return true;
}

} // namespace ondevice
