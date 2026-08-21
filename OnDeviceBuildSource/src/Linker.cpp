#include "OnDeviceBuild/Linker.h"

#include "lld/Common/Driver.h"
#include "llvm/Support/raw_ostream.h"

#include <sys/stat.h>

// Names the one driver this build links in. Without it lldMain() has no Mach-O
// backend to dispatch to.
LLD_HAS_DRIVER (macho)

namespace ondevice {

namespace
{
unsigned long long fileSizeOrZero (const std::string& path)
{
    struct stat s = {};
    return ::stat (path.c_str(), &s) == 0 ? (unsigned long long) s.st_size : 0;
}
} // namespace

LinkResult linkObjects (const LinkRequest& request)
{
    LinkResult result;

    std::vector<std::string> argStorage { "ld64.lld",
                                          "-arch", request.architecture,
                                          "-o", request.outputPath };

    // lld needs the platform spelled out; without it the output carries no
    // LC_BUILD_VERSION and is not recognisably an iOS binary at all - the same
    // distinction Gate 01 turned on.
    argStorage.push_back ("-platform_version");
    argStorage.push_back (request.simulator ? "ios-simulator" : "ios");
    argStorage.push_back (request.minimumOSVersion);
    argStorage.push_back (request.sdkVersion);

    if (! request.sysroot.empty())
    {
        argStorage.push_back ("-syslibroot");
        argStorage.push_back (request.sysroot);
    }

    for (const auto& object : request.objectFiles)
        argStorage.push_back (object);

    for (const auto& library : request.libraries)
        argStorage.push_back ("-l" + library);

    for (const auto& framework : request.frameworks)
    {
        argStorage.push_back ("-framework");
        argStorage.push_back (framework);
    }

    if (! request.builtinsArchive.empty())
        argStorage.push_back (request.builtinsArchive);

    for (const auto& extra : request.extraArgs)
        argStorage.push_back (extra);

    std::vector<const char*> args;
    args.reserve (argStorage.size());

    for (const auto& arg : argStorage)
        args.push_back (arg.c_str());

    std::string output;
    llvm::raw_string_ostream stream (output);

    const auto linkResult = lld::lldMain (args, stream, stream,
                                          { { lld::Darwin, &lld::macho::link } });

    stream.flush();

    result.success = linkResult.retCode == 0;
    result.canRunAgain = linkResult.canRunAgain;
    result.diagnostics = output;

    if (result.success)
        result.outputBytes = fileSizeOrZero (request.outputPath);

    return result;
}

} // namespace ondevice
