#pragma once

#include <string>

namespace ondevice {

struct SignRequest
{
    std::string appFolder;
    std::string p12Path;
    std::string password;
    std::string provisionPath;
    std::string bundleId;
};

struct SignResult
{
    bool success = false;
    std::string log;
};

SignResult signAppFolder (const SignRequest&);

bool extractZip (const std::string& zipFile, const std::string& outputFolder);
bool archiveFolder (const std::string& folder, const std::string& zipFile);

} // namespace ondevice
