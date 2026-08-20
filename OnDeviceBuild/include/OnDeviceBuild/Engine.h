#pragma once

#include <functional>
#include <string>

namespace ondevice {

struct EngineRequest {
    std::string projectRoot;
    std::string manifestJson;       // 中身。パスではない
    std::string workDirectory;
    std::string sysroot;
    std::string resourceDir;
    std::string builtinsArchive;
    std::string p12Path;
    std::string p12Password;
    std::string provisionPath;
    int threads = 1;
    std::function<void (const std::string& line)> onProgress;
};

struct EngineResult {
    bool success = false;
    std::string appFolder;
    std::string ipaPath;
    std::string failureMessage;
    double compileSeconds = 0;
    unsigned long long peakResidentBytes = 0;
    bool linkerCanRunAgain = true;
};

EngineResult buildSignedIpa (const EngineRequest&);

} // namespace ondevice
