/*
  ==============================================================================

   This file is part of the JUCE framework.

  ==============================================================================
*/

#include "../Application/jucer_Headers.h"
#include "../ProjectSaving/jucer_ProjectExporter.h"
#include "jucer_OnDeviceBuildController.h"

#include <TargetConditionals.h>

// JUCE_IOS is true for the simulator as well, but the build engine is not: the
// toolchain archives it links - LLVM, OpenSSL, zsign - are built for
// iphoneos only, and there is nothing for an iphonesimulator slice to be built
// from. The simulator therefore gets the same stub as every non-iOS platform,
// and Projucer still builds and runs there.
#if JUCE_IOS && ! TARGET_OS_SIMULATOR

#define Point CarbonDummyPointName
#import <UIKit/UIKit.h>
#undef Point
#import "jucer_LoopbackServer.h"

#include "../../../OnDeviceBuild/include/OnDeviceBuild/Compiler.h"
#include "../../../OnDeviceBuild/include/OnDeviceBuild/Engine.h"
#include "../../../OnDeviceBuild/include/OnDeviceBuild/Pkcs12.h"
#include "../../../OnDeviceBuild/include/OnDeviceBuild/ZipStore.h"

#include <algorithm>
#include <exception>

namespace
{
constexpr uint16_t kLoopbackPort = 8443;
constexpr double kLoopbackReadyTimeoutSeconds = 5.0;
constexpr const char* kBackloopPassphrase = "pocselfinstall";

static String formatBytes (unsigned long long bytes)
{
    return String ((double) bytes / 1048576.0, 0) + " MB";
}

static String juceStringFromStd (const std::string& text)
{
    return String (text.c_str(), (size_t) text.size());
}

static std::string stdStringFromFile (const File& file)
{
    return file.getFullPathName().toStdString();
}

static File documentsDirectory()
{
    const auto* paths = NSSearchPathForDirectoriesInDomains (NSDocumentDirectory, NSUserDomainMask, YES);
    const NSString* path = paths.firstObject;
    return File (path.UTF8String);
}

static File clangResourceDir()
{
    const NSString* path = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent: @"clang-resource"];
    return File (path.UTF8String);
}

static bool isModernP12 (const File& file)
{
    return file.getFileName().contains (".modern.p12");
}

static File firstMatchingFile (const File& folder, const String& pattern)
{
    if (! folder.isDirectory())
        return {};

    for (const auto& entry : RangedDirectoryIterator (folder, false, pattern, File::findFiles))
        return entry.getFile();

    return {};
}

static File chooseP12 (const File& signingDir)
{
    const auto modern = firstMatchingFile (signingDir, "*.modern.p12");

    if (modern.existsAsFile())
        return modern;

    if (! signingDir.isDirectory())
        return {};

    for (const auto& entry : RangedDirectoryIterator (signingDir, false, "*.p12", File::findFiles))
    {
        const auto file = entry.getFile();

        if (isModernP12 (file))
            continue;

        return file;
    }

    return {};
}

static File sidecarPasswordFile (const File& p12)
{
    return File (p12.getFullPathName().upToLastOccurrenceOf (".p12", false, false) + ".password");
}

static File unmodernSidecarPasswordFile (const File& p12)
{
    const auto stem = p12.getFullPathName().upToLastOccurrenceOf (".p12", false, false);

    if (! stem.endsWith (".modern"))
        return {};

    return File (stem.upToLastOccurrenceOf (".modern", false, false) + ".password");
}

static File passwordFileFor (const File& signingDir, const File& p12)
{
    const auto sidecar = sidecarPasswordFile (p12);

    if (sidecar.existsAsFile())
        return sidecar;

    const auto unmodern = unmodernSidecarPasswordFile (p12);

    if (unmodern.existsAsFile())
        return unmodern;

    const auto shared = signingDir.getChildFile ("password.txt");

    if (shared.existsAsFile())
        return shared;

    return {};
}

class OnDeviceProgressPanel final : public Component,
                                    private Timer
{
public:
    OnDeviceProgressPanel()
    {
        log.setMultiLine (true, true);
        log.setReadOnly (true);
        log.setScrollbarsShown (true);
        log.setFont (FontOptions (Font::getDefaultMonospacedFontName(), 13.0f, Font::plain));
        addAndMakeVisible (log);

        timeLabel.setText ("time: 0.0 s", dontSendNotification);
        rssLabel.setText ("rss: --", dontSendNotification);
        addAndMakeVisible (timeLabel);
        addAndMakeVisible (rssLabel);

        setSize (640, 420);
        startTimerHz (4);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (8);
        auto footer = bounds.removeFromBottom (24);
        timeLabel.setBounds (footer.removeFromLeft (footer.getWidth() / 2));
        rssLabel.setBounds (footer);
        log.setBounds (bounds.withTrimmedBottom (6));
    }

    void appendLine (const String& line)
    {
        log.moveCaretToEnd();
        log.insertTextAtCaret (line + "\n");
        log.moveCaretToEnd();
    }

    void markFinished()
    {
        finished = true;
        stopTimer();
        updateStats();
    }

    void setStartTime (double seconds) { startSeconds = seconds; }

private:
    void timerCallback() override { updateStats(); }

    void updateStats()
    {
        const auto elapsed = Time::getMillisecondCounterHiRes() / 1000.0 - startSeconds;
        timeLabel.setText ("time: " + String (elapsed, 1) + " s", dontSendNotification);
        rssLabel.setText ("rss: " + formatBytes (ondevice::getResidentMemoryBytes())
                              + (finished ? " (done)" : ""),
                          dontSendNotification);
    }

    TextEditor log;
    Label timeLabel, rssLabel;
    double startSeconds = Time::getMillisecondCounterHiRes() / 1000.0;
    bool finished = false;
};

class OnDeviceBuildDialog final : public DialogWindow
{
public:
    OnDeviceBuildDialog()
        : DialogWindow ("Build & Install", Colours::lightgrey, false)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
    }

    void closeButtonPressed() override
    {
        exitModalState (0);
    }
};

class OnDeviceBuildController final
{
public:
    static OnDeviceBuildController& get()
    {
        static OnDeviceBuildController instance;
        return instance;
    }

    bool start (ProjectExporter& exporter)
    {
        if (building)
            return false;

        building = true;
        previousIdleTimerDisabled = UIApplication.sharedApplication.idleTimerDisabled;
        UIApplication.sharedApplication.idleTimerDisabled = YES;

        auto* panel = new OnDeviceProgressPanel();
        panelPtr = panel;
        panel->setStartTime (Time::getMillisecondCounterHiRes() / 1000.0);

        auto* window = new OnDeviceBuildDialog();
        window->setContentOwned (panel, true);
        window->centreWithSize (window->getWidth(), window->getHeight());
        window->enterModalState (true, nullptr, true);
        dialogPtr = window;

        if (backgroundObserver == nil)
        {
            backgroundObserver = [NSNotificationCenter.defaultCenter
                addObserverForName: UIApplicationDidEnterBackgroundNotification
                            object: nil
                             queue: nil
                        usingBlock: ^(NSNotification*)
            {
                appendLine ("app entered background — I/O is now throttled");
            }];
        }

        const auto documents = documentsDirectory();
        const auto projectRoot = exporter.getProject().getProjectFolder();
        const auto manifestFile = exporter.getTargetFolder().getChildFile ("manifest.json");
        const auto projectName = projectRoot.getFileName();

        std::thread ([this, documents, projectRoot, manifestFile, projectName]() mutable
        {
            try
            {
                runBuild (documents, projectRoot, manifestFile, projectName);
            }
            catch (const std::exception& e)
            {
                appendLine ("exception: " + String (e.what()));
                finish (true);
            }
            catch (...)
            {
                appendLine ("unknown exception during on-device build");
                finish (true);
            }
        }).detach();

        return true;
    }

private:
    void appendLine (const String& line)
    {
        if (! MessageManager::getInstance()->isThisTheMessageThread())
        {
            MessageManager::callAsync ([this, line]
            {
                appendLine (line);
            });
            return;
        }

        if (auto* panel = panelPtr.getComponent())
            panel->appendLine (line);
    }

    void finish (bool restoreIdle)
    {
        auto complete = [this, restoreIdle]
        {
            if (auto* panel = panelPtr.getComponent())
                panel->markFinished();

            if (restoreIdle)
                UIApplication.sharedApplication.idleTimerDisabled = previousIdleTimerDisabled;

            building = false;
        };

        if (! MessageManager::getInstance()->isThisTheMessageThread())
        {
            MessageManager::callAsync (complete);
            return;
        }

        complete();
    }

    void runBuild (File documents, File projectRoot, File manifestFile, String projectName)
    {
        juce::Array<String> missing;
        const auto resourceDir = clangResourceDir();
        const auto stddefFile = resourceDir.getChildFile ("include").getChildFile ("stddef.h");
        const auto builtins = resourceDir.getChildFile ("lib").getChildFile ("darwin")
                                         .getChildFile ("libclang_rt.ios.a");
        NSString* backloopPath = [NSBundle.mainBundle pathForResource: @"backloop" ofType: @"p12"];
        const auto backloop = backloopPath != nil ? File (backloopPath.UTF8String) : File();
        const auto signingDir = documents.getChildFile ("OnDeviceSigning");
        const auto p12 = chooseP12 (signingDir);
        const auto provision = firstMatchingFile (signingDir, "*.mobileprovision");
        const auto sdkZip = documents.getChildFile ("iPhoneOS.sdk.zip");
        const auto passwordFile = p12.existsAsFile() ? passwordFileFor (signingDir, p12) : File();

        if (! sdkZip.existsAsFile())
            missing.add (sdkZip.getFullPathName());
        if (! stddefFile.existsAsFile())
            missing.add (stddefFile.getFullPathName());
        if (! builtins.existsAsFile())
            missing.add (builtins.getFullPathName());
        if (! p12.existsAsFile())
            missing.add (signingDir.getChildFile ("*.p12").getFullPathName());
        else if (! passwordFile.existsAsFile())
        {
            missing.add (sidecarPasswordFile (p12).getFullPathName());

            const auto unmodern = unmodernSidecarPasswordFile (p12);

            if (unmodern != File())
                missing.add (unmodern.getFullPathName());

            missing.add (signingDir.getChildFile ("password.txt").getFullPathName());
        }
        if (! provision.existsAsFile())
            missing.add (signingDir.getChildFile ("*.mobileprovision").getFullPathName());
        if (! backloop.existsAsFile())
            missing.add ("bundle backloop.p12");
        if (! manifestFile.existsAsFile())
            missing.add (manifestFile.getFullPathName());

        if (! missing.isEmpty())
        {
            appendLine ("missing assets — not starting clang:");

            for (const auto& path : missing)
                appendLine ("  " + path);

            finish (true);
            return;
        }

        if (linkerDisabled)
        {
            appendLine ("linker previously reported canRunAgain=false; refusing further builds");
            finish (true);
            return;
        }

        const auto password = passwordFile.loadFileAsString().trim().toStdString();
        auto modernP12 = p12;

        if (! isModernP12 (p12))
        {
            modernP12 = p12.getSiblingFile (p12.getFileNameWithoutExtension() + ".modern.p12");
            std::string reencodeError;
            appendLine ("re-encoding PKCS#12 to AES-256-CBC");

            if (! ondevice::reencodePkcs12Aes (stdStringFromFile (p12),
                                               stdStringFromFile (modernP12),
                                               password,
                                               reencodeError))
            {
                appendLine ("PKCS#12 re-encode failed: " + juceStringFromStd (reencodeError));
                appendLine ("Place an AES-256-CBC p12 named *.modern.p12 (or a re-encodable *.p12) in OnDeviceSigning.");
                appendLine ("AES-256-CBC の p12（*.modern.p12）を OnDeviceSigning に置いてください。再エンコードに失敗したため clang を起動しません。");
                finish (true);
                return;
            }
        }

        auto sdk = ondevice::makeSdkStore (stdStringFromFile (documents));
        std::string extractError;
        appendLine ("unpacking iOS SDK if needed");

        if (! sdk.ensureExtracted ([this] (const std::string& line)
                                   {
                                       appendLine (juceStringFromStd (line));
                                   },
                                   extractError))
        {
            appendLine ("SDK extract failed: " + juceStringFromStd (extractError));
            finish (true);
            return;
        }

        const auto work = documents.getChildFile ("OnDeviceWork").getChildFile (projectName);
        work.deleteRecursively();
        work.createDirectory();

        const auto manifestJson = manifestFile.loadFileAsString().toStdString();
        const auto cores = (int) NSProcessInfo.processInfo.activeProcessorCount;
        const int threads = std::min (4, std::max (1, cores / 2));

        ondevice::EngineRequest request;
        request.projectRoot = stdStringFromFile (projectRoot);
        request.manifestJson = manifestJson;
        request.workDirectory = stdStringFromFile (work);
        request.sysroot = sdk.getRoot();
        request.resourceDir = stdStringFromFile (resourceDir);
        request.builtinsArchive = stdStringFromFile (builtins);
        request.p12Path = stdStringFromFile (modernP12);
        request.p12Password = password;
        request.provisionPath = stdStringFromFile (provision);
        request.threads = threads;
        request.onProgress = [this] (const std::string& line)
        {
            appendLine (juceStringFromStd (line));
        };

        appendLine ("building on " + String (threads) + " thread(s)");

        const auto result = ondevice::buildSignedIpa (request);

        if (! result.linkerCanRunAgain)
            linkerDisabled = true;

        if (! result.success)
        {
            appendLine ("BUILD FAILED");
            appendLine (juceStringFromStd (result.failureMessage));
            appendLine ("peak rss " + formatBytes (result.peakResidentBytes));
            finish (true);
            return;
        }

        appendLine ("IPA ready: " + juceStringFromStd (result.ipaPath));
        appendLine ("compile " + String (result.compileSeconds, 1) + " s, peak rss "
                    + formatBytes (result.peakResidentBytes));

        serveAndInstall (result.ipaPath, manifestJson);
        finish (true);
    }

    void serveAndInstall (const std::string& ipaPath, const std::string& manifestJson)
    {
        NSData* ipa = [NSData dataWithContentsOfFile: [NSString stringWithUTF8String: ipaPath.c_str()]];

        if (ipa == nil)
        {
            appendLine ("could not read IPA");
            return;
        }

        NSString* bundleId = @"unknown.bundle";
        NSString* title = @"App";

        if (NSData* json = [NSData dataWithBytes: manifestJson.data() length: manifestJson.size()])
        {
            id object = [NSJSONSerialization JSONObjectWithData: json options: 0 error: nil];

            if ([object isKindOfClass: NSDictionary.class])
            {
                NSDictionary* dict = object;
                if ([dict[@"bundleId"] isKindOfClass: NSString.class])
                    bundleId = dict[@"bundleId"];
                if ([dict[@"name"] isKindOfClass: NSString.class])
                    title = dict[@"name"];
            }
        }

        NSData* identity = [NSData dataWithContentsOfFile: [NSBundle.mainBundle pathForResource: @"backloop" ofType: @"p12"]];
        __block NSString* manifestURL = nil;
        __block BOOL started = NO;

        dispatch_sync (dispatch_get_main_queue(), ^
        {
            [server stop];
            server = [[LoopbackServer alloc] initWithIdentityData: identity
                                                       passphrase: @(kBackloopPassphrase)
                                                           logger: ^(NSString* line)
            {
                appendLine (String (line.UTF8String));
            }];

            const auto host = [NSString stringWithFormat: @"%@.backloop.dev",
                               NSUUID.UUID.UUIDString.lowercaseString];
            const auto base = [NSString stringWithFormat: @"https://%@:%u", host, kLoopbackPort];
            manifestURL = [base stringByAppendingString: @"/manifest.plist"];

            NSDictionary* plist = @{
                @"items": @[@{
                    @"assets": @[@{ @"kind": @"software-package",
                                    @"url": [base stringByAppendingString: @"/payload.ipa"] }],
                    @"metadata": @{ @"bundle-identifier": bundleId,
                                    @"bundle-version": @"1.0",
                                    @"kind": @"software",
                                    @"title": title }
                }]
            };

            [server serveData: [NSPropertyListSerialization dataWithPropertyList: plist
                                                                          format: NSPropertyListXMLFormat_v1_0
                                                                         options: 0
                                                                           error: nil]
                       atPath: @"/manifest.plist"
                  contentType: @"text/xml"];
            [server serveData: ipa
                       atPath: @"/payload.ipa"
                  contentType: @"application/octet-stream"];

            started = [server startOnPort: kLoopbackPort];
        });

        if (! started)
        {
            appendLine ("loopback HTTPS server failed to start");
            return;
        }

        if (! [server waitUntilReadyWithTimeout: kLoopbackReadyTimeoutSeconds])
        {
            appendLine ("loopback listener did not become ready; not opening itms-services");
            dispatch_sync (dispatch_get_main_queue(), ^{ [server stop]; });
            return;
        }

        dispatch_sync (dispatch_get_main_queue(), ^
        {
            appendLine ("tap Install when prompted (itms-services)");

            const auto encoded = [manifestURL stringByAddingPercentEncodingWithAllowedCharacters:
                                  NSCharacterSet.alphanumericCharacterSet];
            const auto url = [NSURL URLWithString: [NSString stringWithFormat:
                @"itms-services://?action=download-manifest&url=%@", encoded]];

            [UIApplication.sharedApplication openURL: url
                                             options: @{}
                                   completionHandler: ^(BOOL ok)
            {
                appendLine (ok ? "install requested — answer the prompt"
                               : "openURL refused the itms-services URL");
            }];
        });
    }

    Component::SafePointer<DialogWindow> dialogPtr;
    Component::SafePointer<OnDeviceProgressPanel> panelPtr;
    LoopbackServer* server = nil;
    id backgroundObserver = nil;
    BOOL previousIdleTimerDisabled = NO;
    bool building = false;
    bool linkerDisabled = false;
};
} // namespace

bool startOnDeviceBuild (ProjectExporter& exporter)
{
    return OnDeviceBuildController::get().start (exporter);
}

#else

bool startOnDeviceBuild (ProjectExporter& exporter)
{
    juce::ignoreUnused (exporter);
    return false;
}

#endif
