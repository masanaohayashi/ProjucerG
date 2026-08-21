#pragma once

#include <functional>
#include <string>

namespace ondevice {

struct EngineRequest {
    std::string projectRoot;
    std::string manifestJson;       // 中身。パスではない
    std::string workDirectory;
    /** Build for arm64 iOS Simulator instead of arm64 iOS device. */
    bool simulator = false;
    std::string sysroot;
    std::string resourceDir;
    std::string builtinsArchive;
    std::string p12Path;
    std::string p12Password;
    std::string provisionPath;
    int threads = 1;
    std::function<void (const std::string& line)> onProgress;
    /** Optional cooperative cancellation hook checked between build stages. */
    std::function<bool()> shouldCancel;
};

struct EngineResult {
    bool success = false;
    std::string appFolder;
    std::string ipaPath;
    std::string failureMessage;
    double compileSeconds = 0;
    unsigned long long peakResidentBytes = 0;
    bool linkerCanRunAgain = true;
    bool cancelled = false;
};

EngineResult buildSignedIpa (const EngineRequest&);

} // namespace ondevice
