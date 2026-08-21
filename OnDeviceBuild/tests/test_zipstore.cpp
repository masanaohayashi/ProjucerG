#include "OnDeviceBuild/Signer.h"
#include "OnDeviceBuild/ZipStore.h"

#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
namespace fs = std::filesystem;

[[noreturn]] void fail (const std::string& message)
{
    std::cerr << "FAIL: " << message << '\n';
    std::exit (1);
}

void writeFile (const fs::path& path, const std::string& contents)
{
    fs::create_directories (path.parent_path());
    std::ofstream out (path, std::ios::binary | std::ios::trunc);
    out << contents;
}

timespec mtimeOf (const fs::path& path)
{
    struct stat s = {};

    if (::stat (path.c_str(), &s) != 0)
        fail ("stat failed: " + path.string());

    return s.st_mtimespec;
}

bool sameTime (timespec a, timespec b)
{
    return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec;
}

void makeTree (const fs::path& root, bool withExtra)
{
    fs::remove_all (root);
    writeFile (root / "usr" / "lib" / "libSystem.tbd", "tbd v1\n");
    writeFile (root / "System" / "Library" / "Frameworks" / "UIKit.framework" / "UIKit", "uikit\n");

    if (withExtra)
        writeFile (root / "extra.txt", "second zip, larger\n................\n");
}
} // namespace

int main()
{
    const auto pid = std::to_string (::getpid());
    const auto work = fs::temp_directory_path() / ("ondevice-zipstore-" + pid);
    fs::remove_all (work);
    fs::create_directories (work);

    const auto documents = work / "Documents";
    fs::create_directories (documents);

    const auto tree = work / "tree";
    const auto zipPath = documents / "iPhoneOS.sdk.zip";
    makeTree (tree, false);

    if (! ondevice::archiveFolder (tree.string(), zipPath.string()))
        fail ("archiveFolder failed for first zip");

    auto store = ondevice::makeSdkStore (documents.string());

    if (! store.isZipPresent())
        fail ("zip should be present at " + store.getExpectedZipPath());

    if (store.isExtracted())
        fail ("isExtracted should be false before ensureExtracted");

    std::string error;
    const auto noop = [] (const std::string&) {};

    if (! store.ensureExtracted (noop, error))
        fail ("first ensureExtracted: " + error);

    if (! store.isExtracted())
        fail ("isExtracted should be true after a successful extract");

    const auto sentinel = fs::path (store.getRoot()) / "usr" / "lib" / "libSystem.tbd";
    const auto uikit = fs::path (store.getRoot()) / "System" / "Library" / "Frameworks" / "UIKit.framework";

    if (! fs::is_regular_file (sentinel))
        fail ("missing sentinel libSystem.tbd");

    if (! fs::exists (uikit))
        fail ("missing sentinel UIKit.framework");

    const auto firstMtime = mtimeOf (sentinel);
    const auto extraPath = fs::path (store.getRoot()) / "extra.txt";

    if (fs::exists (extraPath))
        fail ("extra.txt should not exist after the first extract");

    if (! store.ensureExtracted (noop, error))
        fail ("second ensureExtracted: " + error);

    if (! sameTime (firstMtime, mtimeOf (sentinel)))
        fail ("second ensureExtracted rewrote a sentinel");

    std::cout << "PASS: second ensureExtracted did not re-extract\n";

    makeTree (tree, true);

    if (! ondevice::archiveFolder (tree.string(), zipPath.string()))
        fail ("archiveFolder failed for rewritten zip");

    if (store.isExtracted())
        fail ("isExtracted should be false after the zip size changes");

    if (! store.ensureExtracted (noop, error))
        fail ("re-extract after zip change: " + error);

    if (! fs::is_regular_file (extraPath))
        fail ("re-extract did not write extra.txt from the new zip");

    if (sameTime (firstMtime, mtimeOf (sentinel)))
        fail ("re-extract did not refresh sentinel mtime");

    const auto simulatorZipPath = documents / "iPhoneSimulator.sdk.zip";

    if (! ondevice::archiveFolder (tree.string(), simulatorZipPath.string()))
        fail ("archiveFolder failed for simulator zip");

    auto simulatorStore = ondevice::makeSdkStore (documents.string(), true);

    if (simulatorStore.getExpectedZipPath() != simulatorZipPath.string())
        fail ("Simulator store selected the wrong zip path");

    if (simulatorStore.getRoot() != (documents / "sdk-simulator").string())
        fail ("Simulator store selected the wrong extraction root");

    if (! simulatorStore.ensureExtracted (noop, error))
        fail ("Simulator ensureExtracted: " + error);

    std::cout << "PASS: Simulator SDK store\n";

    fs::remove_all (work);
    std::cout << "PASS: ZipStore stamp and sentinels\n";
    return 0;
}
