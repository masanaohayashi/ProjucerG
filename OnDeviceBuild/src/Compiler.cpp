#include "OnDeviceBuild/Compiler.h"

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"

#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include <mach/mach.h>
#include <mutex>
#include <sys/stat.h>
#include <utility>

namespace ondevice {

namespace
{
// Clang's frontend parses LLVM backend options through a process-global
// llvm::cl registry.  BackendUtil.cpp explicitly documents that parser as
// non-thread-safe, while the engine dispatches source files concurrently.
// Serialise the complete in-process frontend invocation, not just target
// registration, so option registration and parsing cannot race between files.
std::mutex compilerMutex;

void initialiseTargetsOnce()
{
    // Registering the backend is what makes an arm64 triple resolvable. If the
    // library were ever built without the AArch64 target this is the first
    // thing that would fail, so it stays explicit rather than hiding inside
    // InitializeAllTargets().
    static const bool done = []
    {
        LLVMInitializeAArch64TargetInfo();
        LLVMInitializeAArch64Target();
        LLVMInitializeAArch64TargetMC();
        LLVMInitializeAArch64AsmParser();
        LLVMInitializeAArch64AsmPrinter();
        return true;
    }();

    (void) done;
}

unsigned long long fileSizeOrZero (const std::string& path)
{
    struct stat s = {};
    return ::stat (path.c_str(), &s) == 0 ? (unsigned long long) s.st_size : 0;
}
} // namespace

CompileResult compileToObject (const CompileRequest& request)
{
    std::lock_guard<std::mutex> lock (compilerMutex);

    if (request.shouldCancel && request.shouldCancel())
    {
        CompileResult result;
        result.diagnostics = "build cancelled";
        return result;
    }

    initialiseTargetsOnce();

    CompileResult result;
    llvm::raw_string_ostream diagnosticStream (result.diagnostics);

    std::vector<std::string> argStorage { "-triple", request.triple,
                                          "-emit-obj",
                                          "-o", request.outputPath };

    if (! request.resourceDir.empty())
    {
        argStorage.push_back ("-resource-dir");
        argStorage.push_back (request.resourceDir);
    }

    if (! request.sysroot.empty())
    {
        argStorage.push_back ("-isysroot");
        argStorage.push_back (request.sysroot);
    }

    // Turning a sysroot into search paths is the *driver's* job, and the driver
    // is exactly what cannot be used here. These are the paths it would have
    // added, taken from `clang -### -isysroot <sdk> -x objective-c++ ...`.
    //
    // The order is not cosmetic. libc++ ships its own <stddef.h> which must be
    // found before clang's builtin one; putting the resource directory first
    // makes <cstddef> fail with a message about header search paths that says
    // nothing about which path is wrong. So c++/v1 leads, and the resource
    // directory sits where the driver puts it - third.
    //
    // Omitted deliberately: the Xcode toolchain's own /usr/include, which does
    // not exist on a device.
    std::vector<std::pair<const char*, std::string>> searchPaths;

    if (! request.sysroot.empty())
    {
        searchPaths.emplace_back ("-internal-isystem", request.sysroot + "/usr/include/c++/v1");
        searchPaths.emplace_back ("-internal-isystem", request.sysroot + "/usr/local/include");
    }

    if (! request.resourceDir.empty())
        searchPaths.emplace_back ("-internal-isystem", request.resourceDir + "/include");

    if (! request.sysroot.empty())
    {
        searchPaths.emplace_back ("-internal-externc-isystem", request.sysroot + "/usr/include");
        searchPaths.emplace_back ("-internal-iframework", request.sysroot + "/System/Library/Frameworks");
        searchPaths.emplace_back ("-internal-iframework", request.sysroot + "/System/Library/SubFrameworks");
        searchPaths.emplace_back ("-internal-iframework", request.sysroot + "/Library/Frameworks");
    }

    for (const auto& [flag, path] : searchPaths)
    {
        argStorage.push_back (flag);
        argStorage.push_back (path);
    }

    if (! request.sysroot.empty())
    {
        // Three things the driver sets that Apple's headers refuse to work
        // without, each failing in a way that names something other than the
        // missing flag:
        //
        //  -fgnuc-version   clang only defines __GNUC__ when the driver asks it
        //                   to. Without it sys/cdefs.h reports "Unsupported
        //                   compiler", TargetConditionals.h reports "unknown
        //                   compiler", and <stdarg.h> collides with the SDK's
        //                   own __darwin_va_list.
        //  -fdefine-target-os-macros  supplies the TARGET_OS_* macros that
        //                   TargetConditionals.h expects the compiler to define.
        //  -fobjc-runtime   selects the modern Objective-C ABI; the default is
        //                   not the one Apple platforms use.
        // The Apple target, spelled out. The driver derives all of this from
        // the triple; -cc1 does not, and the omission does not surface until a
        // header reaches for an intrinsic - <simd/matrix.h> calling vzip1q_f64
        // reports "use of undeclared identifier", naming NEON nowhere.
        //
        // apple-a7 is what the driver picks for a bare arm64-apple-ios triple:
        // the baseline every 64-bit iOS device satisfies.
        argStorage.push_back ("-target-cpu");
        argStorage.push_back ("apple-a7");
        argStorage.push_back ("-target-abi");
        argStorage.push_back ("darwinpcs");

        for (const char* feature : { "+v8a", "+aes", "+fp-armv8", "+neon", "+perfmon", "+sha2" })
        {
            argStorage.push_back ("-target-feature");
            argStorage.push_back (feature);
        }

        argStorage.push_back ("-fgnuc-version=4.2.1");
        argStorage.push_back ("-fdefine-target-os-macros");
        argStorage.push_back ("-fobjc-runtime=ios-" + request.minimumOSVersion);
        argStorage.push_back ("-fobjc-exceptions");
        argStorage.push_back ("-fblocks");

        // C++ exceptions are on by default in the driver and off by default in
        // -cc1. JUCE uses `try`, and the error says only "exceptions disabled" -
        // it does not hint that a flag was never passed.
        argStorage.push_back ("-fexceptions");
        argStorage.push_back ("-fcxx-exceptions");
    }

    for (const auto& extra : request.extraArgs)
        argStorage.push_back (extra);

    argStorage.push_back (request.sourcePath);

    std::vector<const char*> args;
    args.reserve (argStorage.size());

    for (const auto& arg : argStorage)
        args.push_back (arg.c_str());

    // CreateFromArgs needs a diagnostics engine before a CompilerInstance
    // exists, so this one is built by hand and then handed to the instance as a
    // non-owned client - both of them write into the same string, which is what
    // the caller gets back whether the failure was in argument parsing or in
    // the compile itself.
    // The exact -cc1 line, always. Reconstructing what the driver would have
    // passed is the fiddly part of driving the frontend directly, and guessing
    // at it from an error message wastes a device round-trip every time.
    for (const auto& arg : argStorage)
        result.diagnostics += arg + " ";

    result.diagnostics += "\n\n";

    clang::DiagnosticOptions diagnosticOptions;
    clang::TextDiagnosticPrinter diagnosticPrinter (diagnosticStream, diagnosticOptions);
    clang::DiagnosticsEngine diagnostics (llvm::makeIntrusiveRefCnt<clang::DiagnosticIDs>(),
                                          diagnosticOptions,
                                          &diagnosticPrinter,
                                          /*ShouldOwnClient=*/ false);

    auto invocation = std::make_shared<clang::CompilerInvocation>();

    if (! clang::CompilerInvocation::CreateFromArgs (*invocation, args, diagnostics))
    {
        diagnosticStream.flush();
        return result;
    }

    clang::CompilerInstance instance (invocation);
    instance.createDiagnostics (&diagnosticPrinter, /*ShouldOwnClient=*/ false);

    clang::EmitObjAction action;
    const bool executed = instance.ExecuteAction (action);
    diagnosticStream.flush();

    result.outputBytes = fileSizeOrZero (request.outputPath);
    result.success = executed
                  && ! instance.getDiagnostics().hasErrorOccurred()
                  && result.outputBytes > 0;

    return result;
}

unsigned long long getResidentMemoryBytes()
{
    // mach_task_basic_info is available on both macOS and iOS, so the host
    // harness and the device app measure the same number the same way.
    mach_task_basic_info info = {};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;

    if (task_info (mach_task_self(), MACH_TASK_BASIC_INFO,
                   (task_info_t) &info, &count) != KERN_SUCCESS)
        return 0;

    return info.resident_size;
}

} // namespace ondevice
