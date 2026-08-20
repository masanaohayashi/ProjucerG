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
    std::vector<std::string> frameworks;
    std::vector<std::string> libraries;
};

ManifestInfo parseManifestJson (const std::string& json);

struct CompileManifestRequest
{
    std::string projectRoot;
    std::string manifestJson;
    std::string workDirectory;
    std::string sysroot;
    std::string resourceDir;
    std::string minimumOSVersion = "17.0";
    int threads = 1;
    std::function<void (const std::string& line)> onProgress;
};

struct CompileManifestResult
{
    bool success = false;
    std::vector<std::string> objectFiles;
    double seconds = 0.0;
    unsigned long long peakResidentBytes = 0;
    std::string failureMessage;
};

CompileManifestResult compileManifest (const CompileManifestRequest&);

} // namespace ondevice
