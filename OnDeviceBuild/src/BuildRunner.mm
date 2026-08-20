#include "BuildRunner.h"
#include "OnDeviceBuild/Compiler.h"

#import <Foundation/Foundation.h>

#include <atomic>
#include <algorithm>

namespace ondevice {

namespace
{
std::string toUtf8 (id object)
{
    if ([object isKindOfClass: NSString.class])
        return std::string ([(NSString*) object UTF8String]);

    return {};
}

NSDictionary* dictionaryFromJson (const std::string& json, std::string& error)
{
    if (json.empty())
    {
        error = "manifest JSON is empty";
        return nil;
    }

    NSData* data = [NSData dataWithBytes: json.data() length: json.size()];
    NSError* jsonError = nil;
    id object = [NSJSONSerialization JSONObjectWithData: data options: 0 error: &jsonError];

    if (! [object isKindOfClass: NSDictionary.class])
    {
        error = jsonError != nil
                    ? std::string ("manifest JSON parse failed: ") + jsonError.localizedDescription.UTF8String
                    : std::string ("manifest JSON is not an object");
        return nil;
    }

    return (NSDictionary*) object;
}

std::vector<std::string> stringArray (NSDictionary* dictionary, NSString* key)
{
    std::vector<std::string> values;
    id object = dictionary[key];

    if (! [object isKindOfClass: NSArray.class])
        return values;

    for (id item in (NSArray*) object)
    {
        const auto text = toUtf8 (item);

        if (! text.empty())
            values.push_back (text);
    }

    return values;
}
} // namespace

ManifestInfo parseManifestJson (const std::string& json)
{
    ManifestInfo info;

    @autoreleasepool
    {
        std::string error;
        NSDictionary* dictionary = dictionaryFromJson (json, error);

        if (dictionary == nil)
        {
            info.error = error;
            return info;
        }

        info.name = toUtf8 (dictionary[@"name"]);
        info.bundleId = toUtf8 (dictionary[@"bundleId"]);
        info.minimumOSVersion = toUtf8 (dictionary[@"minimumOSVersion"]);

        if (info.minimumOSVersion.empty())
            info.minimumOSVersion = "17.0";

        info.frameworks = stringArray (dictionary, @"frameworks");
        info.libraries = stringArray (dictionary, @"libraries");

        if (info.name.empty() || info.bundleId.empty())
        {
            info.error = "manifest is missing name or bundleId";
            return info;
        }

        info.ok = true;
        return info;
    }
}

CompileManifestResult compileManifest (const CompileManifestRequest& request)
{
    CompileManifestResult outcome;

    @autoreleasepool
    {
        std::string error;
        NSDictionary* manifest = dictionaryFromJson (request.manifestJson, error);

        if (manifest == nil)
        {
            outcome.failureMessage = error;
            return outcome;
        }

        NSArray* sources = manifest[@"sources"];

        if (! [sources isKindOfClass: NSArray.class] || sources.count == 0)
        {
            outcome.failureMessage = "manifest has no sources";
            return outcome;
        }

        outcome.objectFiles.resize ((size_t) sources.count);

        std::atomic<bool> failedFlag { false };
        std::atomic<unsigned long long> peakValue { 0 };
        std::string failureText;

        // Blocks capture by const copy, and neither an atomic nor a vector can be
        // copied, so shared state is reached through pointers. The stack frame
        // holding them outlives every block: dispatch_group_wait does not return
        // until all of them have finished.
        auto* failed = &failedFlag;
        auto* peak = &peakValue;
        auto* results = &outcome.objectFiles;
        auto* failureMessage = &failureText;

        dispatch_queue_t reporting = dispatch_queue_create ("ondevice.build.report", DISPATCH_QUEUE_SERIAL);
        dispatch_semaphore_t limit = dispatch_semaphore_create (std::max (1, request.threads));
        dispatch_group_t group = dispatch_group_create();

        NSString* root = @(request.projectRoot.c_str());
        NSString* work = @(request.workDirectory.c_str());
        const std::string sysroot = request.sysroot, resourceDir = request.resourceDir;
        const std::string minimumOSVersion = request.minimumOSVersion.empty() ? "17.0" : request.minimumOSVersion;
        auto onProgress = request.onProgress;

        const auto start = CFAbsoluteTimeGetCurrent();

        for (NSUInteger i = 0; i < sources.count; ++i)
        {
            dispatch_semaphore_wait (limit, DISPATCH_TIME_FOREVER);

            if (*failed)
            {
                dispatch_semaphore_signal (limit);
                break;
            }

            dispatch_group_async (group, dispatch_get_global_queue (QOS_CLASS_USER_INITIATED, 0), ^
            {
                id sourceObject = sources[i];
                NSDictionary* source = [sourceObject isKindOfClass: NSDictionary.class]
                                           ? (NSDictionary*) sourceObject : nil;
                NSString* relative = source[@"file"];
                NSString* language = source[@"language"];

                if (relative.length == 0)
                {
                    dispatch_sync (reporting, ^
                    {
                        *failed = true;
                        *failureMessage = "source entry is missing file";
                    });
                    dispatch_semaphore_signal (limit);
                    return;
                }

                const std::string languageUtf8 = language.length > 0 ? language.UTF8String : "c++";

                CompileRequest compile;
                compile.sourcePath = [root stringByAppendingPathComponent: relative].UTF8String;
                compile.outputPath = [work stringByAppendingPathComponent:
                                      [NSString stringWithFormat: @"%03lu.o", (unsigned long) i]].UTF8String;
                compile.resourceDir = resourceDir;
                compile.sysroot = sysroot;
                compile.minimumOSVersion = minimumOSVersion;
                compile.extraArgs = { "-x", languageUtf8, "-O3" };

                // The C sources JUCE vendors - zlib, libpng, libjpg, Sheenbidi,
                // lunasvg - must not be handed a C++ standard.
                if (languageUtf8 != "c" && languageUtf8 != "objective-c")
                    compile.extraArgs.push_back ("-std=gnu++17");

                for (NSString* define in manifest[@"defines"])
                {
                    if (! [define isKindOfClass: NSString.class])
                        continue;

                    compile.extraArgs.push_back ("-D");
                    compile.extraArgs.push_back (define.UTF8String);
                }

                for (NSString* include in manifest[@"includes"])
                {
                    if (! [include isKindOfClass: NSString.class])
                        continue;

                    compile.extraArgs.push_back ("-I");
                    compile.extraArgs.push_back ([root stringByAppendingPathComponent: include].UTF8String);
                }

                const auto unitStart = CFAbsoluteTimeGetCurrent();
                const auto compiled = compileToObject (compile);
                const auto ms = (CFAbsoluteTimeGetCurrent() - unitStart) * 1000.0;

                const auto rss = getResidentMemoryBytes();
                auto previous = peak->load();

                while (rss > previous && ! peak->compare_exchange_weak (previous, rss)) {}

                dispatch_sync (reporting, ^
                {
                    char line[512];

                    if (! compiled.success)
                    {
                        *failed = true;
                        *failureMessage = std::string (relative.lastPathComponent.UTF8String)
                                            + "\n" + compiled.diagnostics;
                        std::snprintf (line, sizeof (line), "[%02lu] %s FAILED",
                                       (unsigned long) i, relative.lastPathComponent.UTF8String);
                    }
                    else
                    {
                        (*results)[i] = compile.outputPath;
                        std::snprintf (line, sizeof (line), "[%02lu] %-32s %6.0f ms  %5llu KB  rss %.0f MB",
                                       (unsigned long) i, relative.lastPathComponent.UTF8String, ms,
                                       compiled.outputBytes / 1024, rss / 1048576.0);
                    }

                    if (onProgress)
                        onProgress (line);
                });

                dispatch_semaphore_signal (limit);
            });
        }

        dispatch_group_wait (group, DISPATCH_TIME_FOREVER);

        outcome.seconds = CFAbsoluteTimeGetCurrent() - start;
        outcome.peakResidentBytes = peakValue.load();
        outcome.success = ! failedFlag.load();
        outcome.failureMessage = failureText;

        return outcome;
    }
}

} // namespace ondevice
