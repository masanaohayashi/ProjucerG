/*
  ==============================================================================

   This file is part of the JUCE framework.

  ==============================================================================
*/

#include "../Application/jucer_Headers.h"
#include "../ProjectSaving/jucer_ProjectExporter.h"
#include "jucer_OnDeviceBuildController.h"

#if JUCE_IOS

#define Point CarbonDummyPointName
#import <UIKit/UIKit.h>
#undef Point
#import "jucer_LoopbackServer.h"

#include "../../../OnDeviceBuild/include/OnDeviceBuild/Compiler.h"
#include "../../../OnDeviceBuild/include/OnDeviceBuild/Engine.h"
#include "../../../OnDeviceBuild/include/OnDeviceBuild/Pkcs12.h"
#include "../../../OnDeviceBuild/include/OnDeviceBuild/ZipStore.h"

#include <algorithm>

namespace
{
constexpr uint16_t kLoopbackPort = 8443;
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

static File firstMatchingFile (const File& folder, const String& pattern, bool skipModernP12)
{
    if (! folder.isDirectory())
        return {};

    for (const auto& entry : RangedDirectoryIterator (folder, false, pattern, File::findFiles))
    {
        const auto file = entry.getFile();

        if (skipModernP12 && file.getFileName().contains (".modern.p12"))
            continue;

        return file;
    }

    return {};
}

static String readPassword (const File& signingDir, const File& p12)
{
    const auto sidecar = File (p12.getFullPathName().upToLastOccurrenceOf (".p12", false, false) + ".password");

    if (sidecar.existsAsFile())
        return sidecar.loadFileAsString().trim();

    const auto shared = signingDir.getChildFile ("password.txt");

    if (shared.existsAsFile())
        return shared.loadFileAsString().trim();

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

class OnDeviceBuildController final
{
public:
    static OnDeviceBuildController& get()
    {
        static OnDeviceBuildController instance;
        return instance;
    }

    void start (ProjectExporter& exporter)
    {
        if (building)
        {
            appendLine ("build already running");
            return;
        }

        building = true;
        previousIdleTimerDisabled = UIApplication.sharedApplication.idleTimerDisabled;
        UIApplication.sharedApplication.idleTimerDisabled = YES;

        auto* panel = new OnDeviceProgressPanel();
        panelPtr = panel;
        panel->setStartTime (Time::getMillisecondCounterHiRes() / 1000.0);

        DialogWindow::LaunchOptions options;
        options.content.setOwned (panel);
        options.dialogTitle = "Build & Install";
        options.escapeKeyTriggersCloseButton = false;
        options.useNativeTitleBar = true;
        options.resizable = true;
        options.componentToCentreAround = nullptr;
        dialog.reset (options.launchAsync());

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
            runBuild (documents, projectRoot, manifestFile, projectName);
        }).detach();
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
        auto p12 = firstMatchingFile (signingDir, "*.p12", true);

        if (! p12.existsAsFile())
            p12 = firstMatchingFile (signingDir, "*.p12", false);

        const auto provision = firstMatchingFile (signingDir, "*.mobileprovision", false);
        const auto sdkZip = documents.getChildFile ("iPhoneOS.sdk.zip");

        if (! sdkZip.existsAsFile())
            missing.add (sdkZip.getFullPathName());
        if (! stddefFile.existsAsFile())
            missing.add (stddefFile.getFullPathName());
        if (! builtins.existsAsFile())
            missing.add (builtins.getFullPathName());
        if (! p12.existsAsFile())
            missing.add (signingDir.getChildFile ("*.p12").getFullPathName());
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

        const auto password = readPassword (signingDir, p12).toStdString();
        const auto modernP12 = p12.getFileName().contains (".modern.p12")
                                   ? p12
                                   : p12.getSiblingFile (p12.getFileNameWithoutExtension() + ".modern.p12");
        std::string reencodeError;
        appendLine ("re-encoding PKCS#12 to AES-256-CBC");

        if (! ondevice::reencodePkcs12Aes (stdStringFromFile (p12),
                                           stdStringFromFile (modernP12),
                                           password,
                                           reencodeError))
        {
            appendLine ("PKCS#12 re-encode failed: " + juceStringFromStd (reencodeError));
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

            if (! [server startOnPort: kLoopbackPort])
            {
                appendLine ("loopback HTTPS server failed to start");
                return;
            }

            appendLine ("tap Install when prompted (itms-services)");

            const auto encoded = [[base stringByAppendingString: @"/manifest.plist"]
                stringByAddingPercentEncodingWithAllowedCharacters: NSCharacterSet.alphanumericCharacterSet];
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

    std::unique_ptr<DialogWindow> dialog;
    Component::SafePointer<OnDeviceProgressPanel> panelPtr;
    LoopbackServer* server = nil;
    id backgroundObserver = nil;
    BOOL previousIdleTimerDisabled = NO;
    bool building = false;
    bool linkerDisabled = false;
};
} // namespace

void startOnDeviceBuild (ProjectExporter& exporter)
{
    OnDeviceBuildController::get().start (exporter);
}

#else

void startOnDeviceBuild (ProjectExporter& exporter)
{
    juce::ignoreUnused (exporter);
}

#endif
