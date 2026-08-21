#include "OnDeviceBuild/ZipStore.h"
#include "OnDeviceBuild/Signer.h"

#include <sys/stat.h>
#include <dirent.h>
#include <cstdio>

namespace ondevice {

namespace
{
bool exists (const std::string& path)
{
    struct stat s = {};
    return ::stat (path.c_str(), &s) == 0;
}

unsigned long long directorySize (const std::string& path)
{
    unsigned long long total = 0;

    if (auto* dir = opendir (path.c_str()))
    {
        while (auto* entry = readdir (dir))
        {
            const std::string name = entry->d_name;

            if (name == "." || name == "..")
                continue;

            const auto child = path + "/" + name;
            struct stat s = {};

            if (::lstat (child.c_str(), &s) != 0)
                continue;

            total += S_ISDIR (s.st_mode) ? directorySize (child) : (unsigned long long) s.st_size;
        }

        closedir (dir);
    }

    return total;
}

/** Size and modification time of the zip, as text. Cheap, and enough to notice
    that the zip has been replaced - a hash would mean reading tens of MB on
    every launch to detect something a timestamp already reveals. */
std::string stampFor (const std::string& zipPath)
{
    struct stat s = {};

    if (::stat (zipPath.c_str(), &s) != 0)
        return {};

    return std::to_string ((long long) s.st_size) + ":" + std::to_string ((long long) s.st_mtime);
}

std::string readFile (const std::string& path)
{
    std::string text;

    if (auto* file = fopen (path.c_str(), "rb"))
    {
        char buffer[256];

        while (const auto n = fread (buffer, 1, sizeof (buffer), file))
            text.append (buffer, n);

        fclose (file);
    }

    return text;
}
} // namespace

ZipStore::ZipStore (std::string documentsPath,
                    std::string zipName,
                    std::string folderName,
                    std::vector<std::string> sentinels)
    : documents (std::move (documentsPath)),
      zip (std::move (zipName)),
      folder (std::move (folderName)),
      sentinelPaths (std::move (sentinels))
{
}

std::string ZipStore::getExpectedZipPath() const  { return documents + "/" + zip; }
std::string ZipStore::getRoot() const             { return documents + "/" + folder; }
bool ZipStore::isZipPresent() const               { return exists (getExpectedZipPath()); }

std::string ZipStore::getStampPath() const  { return getRoot() + "/.zipstore-stamp"; }

bool ZipStore::isExtracted() const
{
    if (sentinelPaths.empty())
        return false;

    for (const auto& sentinel : sentinelPaths)
        if (! exists (getRoot() + "/" + sentinel))
            return false;

    // If there is no zip any more, an extraction that looks complete is all we
    // have and is accepted; otherwise it has to match the zip that is there.
    const auto stamp = stampFor (getExpectedZipPath());
    return stamp.empty() || readFile (getStampPath()) == stamp;
}

bool ZipStore::ensureExtracted (const std::function<void (const std::string&)>& progress,
                                std::string& error)
{
    if (isExtracted())
    {
        progress (folder + ": already extracted");
        return true;
    }

    if (! isZipPresent())
    {
        error = "no zip at " + getExpectedZipPath();
        return false;
    }

    progress ("extracting " + zip + " ...");

    // Unpacking over a stale tree rather than clearing it first: every file the
    // new zip contains is overwritten, and a file only the old zip had is
    // harmless here because nothing enumerates the tree - the manifest names
    // what to build.
    if (! extractZip (getExpectedZipPath(), getRoot()))
    {
        error = "extraction of " + zip + " failed";
        return false;
    }

    if (auto* file = fopen (getStampPath().c_str(), "wb"))
    {
        const auto stamp = stampFor (getExpectedZipPath());
        fwrite (stamp.data(), 1, stamp.size(), file);
        fclose (file);
    }

    if (! isExtracted())
    {
        error = zip + " extracted but the result is missing an expected file";
        return false;
    }

    return true;
}

unsigned long long ZipStore::getExtractedSize() const  { return directorySize (getRoot()); }

ZipStore makeSdkStore (const std::string& documentsPath, bool simulator)
{
    return ZipStore (documentsPath,
                     simulator ? "iPhoneSimulator.sdk.zip" : "iPhoneOS.sdk.zip",
                     simulator ? "sdk-simulator" : "sdk",
                     { "usr/lib/libSystem.tbd", "System/Library/Frameworks/UIKit.framework" });
}

} // namespace ondevice
