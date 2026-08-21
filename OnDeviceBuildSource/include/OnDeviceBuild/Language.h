#pragma once

#include <cstring>
#include <string>

namespace ondevice {

// Keep in sync with OnDeviceExporter (jucer_ProjectExport_OnDevice.h).
inline const char* languageForSource (const std::string& path, bool compileAsObjC)
{
    auto ends = [&] (const char* ext)
    {
        return path.size() >= std::strlen (ext)
            && path.compare (path.size() - std::strlen (ext), std::string::npos, ext) == 0;
    };

    if (ends (".c"))
        return "c";

    if (compileAsObjC || ends (".mm") || ends (".m"))
        return "objective-c++";

    return "c++";
}

} // namespace ondevice
