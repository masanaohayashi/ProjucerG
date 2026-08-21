#pragma once

#include <string>
#include <vector>
#include <functional>

namespace ondevice {

/** Unpacks a zip into the app's data container, once, and reports where it went.

    Both the iOS SDK and the project being built arrive this way: an ordinary zip
    dropped into Documents by the Files app, by iCloud, by a download or over
    USB. Neither tree contains a single symlink, so a zip round-trip cannot
    quietly lose anything - that was checked before the format was chosen, not
    after, and it is the reason nothing more elaborate is warranted.
*/
class ZipStore
{
public:
    /** @param documentsPath  the app's Documents directory.
        @param zipName        e.g. "iPhoneOS.sdk.zip", expected in Documents.
        @param folderName     directory under Documents to unpack into.
        @param sentinels      paths relative to that directory which must exist
                              afterwards. Checked rather than merely testing
                              that the directory is there, so that a
                              half-finished extraction is not mistaken for a
                              good one. */
    ZipStore (std::string documentsPath,
              std::string zipName,
              std::string folderName,
              std::vector<std::string> sentinels);

    std::string getExpectedZipPath() const;
    std::string getRoot() const;

    bool isZipPresent() const;

    /** True only if the extracted tree is complete *and* was produced from the
        zip currently sitting in Documents. Pushing a newer zip over an older
        one is the normal way to iterate, and silently continuing to use the
        stale extraction is a very quiet way to waste a build. */
    bool isExtracted() const;

    bool ensureExtracted (const std::function<void (const std::string&)>& progress,
                          std::string& error);

    unsigned long long getExtractedSize() const;

private:
    std::string getStampPath() const;

    std::string documents, zip, folder;
    std::vector<std::string> sentinelPaths;
};

/** The iOS SDK, by its usual name and layout. */
ZipStore makeSdkStore (const std::string& documentsPath, bool simulator = false);

} // namespace ondevice
