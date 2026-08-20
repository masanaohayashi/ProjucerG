#pragma once

#include <string>

namespace ondevice {

struct BundleRequest
{
    std::string appFolder;          // ends in .app
    std::string executablePath;     // linked binary to copy in
    std::string bundleId;
    std::string name;
    std::string minimumOSVersion = "17.0";
};

bool writeAppBundle (const BundleRequest&, std::string& error);

/** Zip the parent of a Payload/<Name>.app tree. ipaPath must be outside that parent. */
bool writeIpa (const std::string& appFolder,
               const std::string& ipaPath,
               std::string& error);

} // namespace ondevice
