#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ondevice {

struct ManifestInfo
{
    bool ok = false;
    std::string error;
    std::string name;
    std::string bundleId;
    std::string minimumOSVersion = "17.0";
    /** Full Info.plist XML generated from the .jucer settings. Empty means "use the built-in default". */
    std::string infoPlist;
    std::vector<std::string> frameworks;
    std::vector<std::string> libraries;
    /** .jucer の Extra Linker Flags。 */
    std::vector<std::string> linkerFlags;
    /** .jucer の Library Search Paths。プロジェクトルートからの相対。 */
    std::vector<std::string> libSearchPaths;
};

ManifestInfo parseManifestJson (const std::string& json);

/** .jucer のリンカフラグは clang ドライバ向けに書かれている。オンデバイスビルドは
    lld を直接呼ぶので、ドライバへの受け渡し接頭辞 "-Wl," を外して中身だけ渡す。 */
inline void appendDriverLinkerFlag (const std::string& flag, std::vector<std::string>& out)
{
    if (flag == "-Wl")
        return;

    if (flag.rfind ("-Wl,", 0) != 0)
    {
        out.push_back (flag);
        return;
    }

    for (size_t start = 4; start <= flag.size();)
    {
        const auto comma = flag.find (',', start);
        const auto end = comma == std::string::npos ? flag.size() : comma;

        if (end > start)
            out.push_back (flag.substr (start, end - start));

        start = end + 1;
    }
}

struct CompileManifestRequest
{
    std::string projectRoot;
    std::string manifestJson;
    std::string workDirectory;
    bool simulator = false;
    std::string sysroot;
    std::string resourceDir;
    std::string minimumOSVersion = "17.0";
    int threads = 1;
    std::function<void (const std::string& line)> onProgress;
    std::function<bool()> shouldCancel;
};

struct CompileManifestResult
{
    bool success = false;
    std::vector<std::string> objectFiles;
    double seconds = 0.0;
    unsigned long long peakResidentBytes = 0;
    std::string failureMessage;
    bool cancelled = false;
};

CompileManifestResult compileManifest (const CompileManifestRequest&);

} // namespace ondevice
