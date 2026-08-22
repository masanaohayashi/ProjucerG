/*
  ==============================================================================

   This file is part of the JUCE framework.

  ==============================================================================
*/

#include "../Application/jucer_Headers.h"
#include "../Application/jucer_Application.h"
#include "../Project/UI/jucer_ProjectContentComponent.h"
#include "../ProjectSaving/jucer_ProjectExporter.h"
#include "jucer_OnDeviceBuildController.h"

#include <TargetConditionals.h>

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
#include <atomic>
#include <exception>
#include <functional>

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
    return String (CharPointer_UTF8 (text.c_str()));
}

static std::string stdStringFromFile (const File& file)
{
    return file.getFullPathName().toStdString();
}

static String bundleIdentifierFromManifest (const std::string& manifestJson)
{
    String bundleId = "unknown.bundle";
    NSData* json = [NSData dataWithBytes: manifestJson.data() length: manifestJson.size()];

    if (id object = [NSJSONSerialization JSONObjectWithData: json options: 0 error: nil])
    {
        if ([object isKindOfClass: NSDictionary.class])
        {
            id value = ((NSDictionary*) object)[@"bundleId"];

            if ([value isKindOfClass: NSString.class])
                bundleId = String (CharPointer_UTF8 (((NSString*) value).UTF8String));
        }
    }

    return bundleId;
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

        closeButton.onClick = [this]
        {
            if (closeRequested)
                closeRequested();
        };
        addAndMakeVisible (closeButton);

        setSize (640, 420);
        startTimerHz (4);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (8);
        auto footer = bounds.removeFromBottom (32);
        closeButton.setBounds (footer.removeFromRight (132).reduced (0, 2));
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
        closeButton.setButtonText ("Close");
        closeButton.setEnabled (true);
        updateStats();
    }

    void markCancelling()
    {
        closeButton.setButtonText ("Stopping...");
        closeButton.setEnabled (false);
    }

    void setCloseRequested (std::function<void()> callback)
    {
        closeRequested = std::move (callback);
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
    TextButton closeButton { "Cancel Build" };
    std::function<void()> closeRequested;
    double startSeconds = Time::getMillisecondCounterHiRes() / 1000.0;
    bool finished = false;
};

class OnDeviceBuildDialog final : public DialogWindow
{
public:
    explicit OnDeviceBuildDialog (std::function<void()> closeRequested)
        : DialogWindow ("Build & Install", Colours::lightgrey, true),
          closeRequested (std::move (closeRequested))
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
    }

    void closeButtonPressed() override
    {
        if (closeRequested)
            closeRequested();
    }

private:
    std::function<void()> closeRequested;
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
        {
            const juce::ScopedLock sl (buildLock);

            if (building)
                return false;

            building = true;
        }

        cancelRequested.store (false);
        closeWhenFinished = false;
        previousIdleTimerDisabled = UIApplication.sharedApplication.idleTimerDisabled;
        UIApplication.sharedApplication.idleTimerDisabled = YES;
        showProgressPanel();

        const auto documents = documentsDirectory();
        const auto projectRoot = exporter.getProject().getProjectFolder();
        const auto manifestFile = exporter.getTargetFolder().getChildFile ("manifest.json");
        const auto projectName = projectRoot.getFileName();
        const bool simulator = TARGET_OS_SIMULATOR != 0;

        std::thread ([this, documents, projectRoot, manifestFile, projectName, simulator]() mutable
        {
            try
            {
                runBuild (documents, projectRoot, manifestFile, projectName, simulator, true);
            }
            catch (const std::exception& e)
            {
                appendLine ("exception: " + String (CharPointer_UTF8 (e.what())));
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

    bool isBuilding() const
    {
        const juce::ScopedLock sl (buildLock);
        return building;
    }

    bool runCapturing (const File& requestedRoot, String& logOut, std::atomic<bool>& cancelled)
    {
        if (isBuilding())
        {
            logOut = "A build is already running.";
            return false;
        }

        lastSuccess.store (false);
        externalCancel = &cancelled;
        captureLog = &logOut;

        juce::WaitableEvent launched;
        String launchError;
        bool launchOk = false;

        auto kickOff = [this, requestedRoot, &launched, &launchError, &launchOk]
        {
            auto* project = findOpenProject (requestedRoot);

            if (project == nullptr)
            {
                launchError = "No open project matches this working directory.";
                launched.signal();
                return;
            }

            ProjectContentComponent* content = nullptr;

            for (auto* window : ProjucerApplication::getApp().mainWindowList.windows)
                if (window->getProject() == project)
                    content = window->getProjectContentComponent();

            if (content == nullptr)
            {
                launchError = "The project window is not open.";
                launched.signal();
                return;
            }

            content->openInSelectedIDE (true, [this, &launched, &launchError, &launchOk] (bool started)
            {
                launchOk = started && isBuilding();
                if (! launchOk)
                    launchError = "Could not start On-Device Build. Choose the On-Device exporter and try again.";
                launched.signal();
            });
        };

        if (MessageManager::getInstance()->isThisTheMessageThread())
            kickOff();
        else
            MessageManager::callAsync (kickOff);

        while (! launched.wait (200))
        {
            if (cancelled.load())
            {
                logOut = "The build was stopped.";
                captureLog = nullptr;
                externalCancel = nullptr;
                return false;
            }
        }

        if (! launchOk)
        {
            logOut = launchError.isNotEmpty() ? launchError : "Could not start On-Device Build.";
            captureLog = nullptr;
            externalCancel = nullptr;
            return false;
        }

        while (isBuilding())
        {
            if (cancelled.load())
                cancelRequested.store (true);

            juce::Thread::sleep (200);
        }

        captureLog = nullptr;
        externalCancel = nullptr;
        return lastSuccess.load();
    }

private:
    static Project* findOpenProject (const File& root)
    {
        const auto wanted = root.getFullPathName();

        for (auto* window : ProjucerApplication::getApp().mainWindowList.windows)
            if (auto* project = window->getProject())
                if (project->getProjectFolder().getFullPathName() == wanted)
                    return project;

        return ProjucerApplication::getApp().mainWindowList.getFrontmostProject();
    }

    void showProgressPanel()
    {
        auto* panel = new OnDeviceProgressPanel();
        panelPtr = panel;
        panel->setStartTime (Time::getMillisecondCounterHiRes() / 1000.0);
        panel->setCloseRequested ([this] { requestClose(); });

        auto* window = new OnDeviceBuildDialog ([this] { requestClose(); });
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
                appendLine ("app entered background - I/O is now throttled");
            }];
        }
    }

    void appendLine (const String& line)
    {
        {
            const juce::ScopedLock sl (logLock);

            if (captureLog != nullptr)
            {
                if (captureLog->isNotEmpty())
                    *captureLog << "\n";

                *captureLog << line;
            }
        }

        if (! MessageManager::getInstance()->isThisTheMessageThread())
        {
            MessageManager::callAsync ([this, line]
            {
                if (auto* panel = panelPtr.getComponent())
                    panel->appendLine (line);
            });
            return;
        }

        if (auto* panel = panelPtr.getComponent())
            panel->appendLine (line);
    }

    void finish (bool restoreIdle)
    {
        const bool waitForMain = panelPtr.getComponent() == nullptr
                              && ! MessageManager::getInstance()->isThisTheMessageThread();
        juce::WaitableEvent done;
        auto complete = [this, restoreIdle, waitForMain, &done]
        {
            if (auto* panel = panelPtr.getComponent())
                panel->markFinished();

            if (restoreIdle)
                UIApplication.sharedApplication.idleTimerDisabled = previousIdleTimerDisabled;

            {
                const juce::ScopedLock sl (buildLock);
                building = false;
            }

            if (closeWhenFinished)
            {
                closeWhenFinished = false;
                closeDialog();
            }

            if (waitForMain)
                done.signal();
        };

        if (MessageManager::getInstance()->isThisTheMessageThread())
        {
            complete();
            return;
        }

        MessageManager::callAsync (complete);

        if (waitForMain)
            done.wait();
    }

    void requestClose()
    {
        if (! MessageManager::getInstance()->isThisTheMessageThread())
        {
            MessageManager::callAsync ([this] { requestClose(); });
            return;
        }

        if (! building)
        {
            closeDialog();
            return;
        }

        if (cancelPromptShown)
            return;

        cancelPromptShown = true;
        auto options = MessageBoxOptions::makeOptionsYesNo (MessageBoxIconType::QuestionIcon,
                                                            "Stop build?",
                                                            "The build is still running. Stop it and close this window?",
                                                            "Stop Build",
                                                            "Keep Building",
                                                            dialogPtr.getComponent());
        cancelPrompt = AlertWindow::showScopedAsync (options, [this] (int result)
        {
            cancelPromptShown = false;

            if (result == 1)
            {
                if (! building)
                {
                    closeDialog();
                    return;
                }

                cancelRequested.store (true);
                closeWhenFinished = true;

                if (auto* panel = panelPtr.getComponent())
                    panel->markCancelling();

                appendLine ("cancelling build...");
            }
        });
    }

    void closeDialog()
    {
        cancelPrompt.close();

        if (auto* dialog = dialogPtr.getComponent())
            dialog->exitModalState (0);
    }

    bool runBuild (File documents, File projectRoot, File manifestFile, String projectName,
                   bool simulator, bool installWhenSuccessful)
    {
        lastSuccess.store (false);
        juce::Array<String> missing;
        const auto resourceDir = clangResourceDir();
        const auto stddefFile = resourceDir.getChildFile ("include").getChildFile ("stddef.h");
        const auto builtins = resourceDir.getChildFile ("lib").getChildFile ("darwin")
                                         .getChildFile (simulator ? "libclang_rt.iossim.a"
                                                                   : "libclang_rt.ios.a");
        NSString* backloopPath = simulator ? nil
                                           : [NSBundle.mainBundle pathForResource: @"backloop" ofType: @"p12"];
        const auto backloop = backloopPath != nil ? File (backloopPath.UTF8String) : File();
        const auto signingDir = documents.getChildFile ("OnDeviceSigning");
        const auto p12 = simulator ? File() : chooseP12 (signingDir);
        const auto provision = simulator ? File() : firstMatchingFile (signingDir, "*.mobileprovision");
        const auto sdkZip = documents.getChildFile (simulator ? "iPhoneSimulator.sdk.zip"
                                                              : "iPhoneOS.sdk.zip");
        const auto passwordFile = p12.existsAsFile() ? passwordFileFor (signingDir, p12) : File();

        if (! sdkZip.existsAsFile())
            missing.add (sdkZip.getFullPathName());
        if (! stddefFile.existsAsFile())
            missing.add (stddefFile.getFullPathName());
        if (! builtins.existsAsFile())
            missing.add (builtins.getFullPathName());
        if (! simulator && ! p12.existsAsFile())
            missing.add (signingDir.getChildFile ("*.p12").getFullPathName());
        else if (! simulator && ! passwordFile.existsAsFile())
        {
            missing.add (sidecarPasswordFile (p12).getFullPathName());

            const auto unmodern = unmodernSidecarPasswordFile (p12);

            if (unmodern != File())
                missing.add (unmodern.getFullPathName());

            missing.add (signingDir.getChildFile ("password.txt").getFullPathName());
        }
        if (! simulator && ! provision.existsAsFile())
            missing.add (signingDir.getChildFile ("*.mobileprovision").getFullPathName());
        if (! simulator && ! backloop.existsAsFile())
            missing.add ("bundle backloop.p12");
        if (! manifestFile.existsAsFile())
            missing.add (manifestFile.getFullPathName());

        if (! missing.isEmpty())
        {
            appendLine ("missing assets - not starting clang:");

            for (const auto& path : missing)
                appendLine ("  " + path);

            finish (true);
            return false;
        }

        if (linkerDisabled)
        {
            appendLine ("linker previously reported canRunAgain=false; refusing further builds");
            finish (true);
            return false;
        }

        const auto password = simulator ? std::string()
                                        : passwordFile.loadFileAsString().trim().toStdString();
        auto modernP12 = p12;

        if (! simulator && ! isModernP12 (p12))
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
                finish (true);
                return false;
            }
        }

        auto sdk = ondevice::makeSdkStore (stdStringFromFile (documents), simulator);
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
            return false;
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
        request.simulator = simulator;
        request.sysroot = sdk.getRoot();
        request.resourceDir = stdStringFromFile (resourceDir);
        request.builtinsArchive = stdStringFromFile (builtins);
        request.p12Path = simulator ? std::string() : stdStringFromFile (modernP12);
        request.p12Password = password;
        request.provisionPath = simulator ? std::string() : stdStringFromFile (provision);
        request.threads = threads;
        request.shouldCancel = [this]
        {
            return cancelRequested.load()
                || (externalCancel != nullptr && externalCancel->load());
        };
        request.onProgress = [this] (const std::string& line)
        {
            appendLine (juceStringFromStd (line));
        };

        appendLine ("building on " + String (threads) + " thread(s)");

        const auto result = ondevice::buildSignedIpa (request);

        if (! result.linkerCanRunAgain)
            linkerDisabled = true;

        if (result.cancelled || cancelRequested.load()
            || (externalCancel != nullptr && externalCancel->load()))
        {
            appendLine ("BUILD CANCELLED");
            finish (true);
            return false;
        }

        if (! result.success)
        {
            appendLine ("BUILD FAILED");
            appendLine (juceStringFromStd (result.failureMessage));
            appendLine ("peak rss " + formatBytes (result.peakResidentBytes));
            finish (true);
            return false;
        }

        appendLine ("BUILD SUCCEEDED");
        appendLine ((simulator ? "Simulator app ready: " : "IPA ready: ")
                    + juceStringFromStd (simulator ? result.appFolder : result.ipaPath));
        appendLine ("compile " + String (result.compileSeconds, 1) + " s, peak rss "
                    + formatBytes (result.peakResidentBytes));

        if (! installWhenSuccessful)
        {
            lastSuccess.store (true);
            finish (true);
            return true;
        }

        if (simulator)
        {
            String installOutput;

            appendLine ("installing Simulator app");

            if (! installSimulatorApp (result.appFolder, manifestJson, installOutput))
            {
                appendLine ("INSTALL FAILED");

                if (installOutput.isNotEmpty())
                    appendLine (installOutput);

                finish (true);
                return false;
            }

            appendLine ("Simulator app installed and launched: "
                        + bundleIdentifierFromManifest (manifestJson));
        }
        else
        {
            serveAndInstall (result.ipaPath, manifestJson);
        }

        lastSuccess.store (true);
        finish (true);
        return true;
    }

    bool installSimulatorApp (const std::string& appPath, const std::string& manifestJson, String& output)
    {
       #if TARGET_OS_SIMULATOR
        const auto bundleId = bundleIdentifierFromManifest (manifestJson);
        NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:
            [NSURL URLWithString: @"http://127.0.0.1:38472/install"]];
        request.HTTPMethod = @"POST";
        request.timeoutInterval = 120.0;
        [request setValue: @"application/json" forHTTPHeaderField: @"Content-Type"];

        NSDictionary* body = @{
            @"appPath": [NSString stringWithUTF8String: appPath.c_str()],
            @"bundleId": [NSString stringWithUTF8String: bundleId.toRawUTF8()]
        };
        request.HTTPBody = [NSJSONSerialization dataWithJSONObject: body options: 0 error: nil];

        __block NSData* responseData = nil;
        __block NSError* requestError = nil;
        dispatch_semaphore_t semaphore = dispatch_semaphore_create (0);
        NSURLSessionDataTask* task = [NSURLSession.sharedSession
            dataTaskWithRequest: request
              completionHandler: ^(NSData* data, NSURLResponse*, NSError* error)
        {
            responseData = data;
            requestError = error;
            dispatch_semaphore_signal (semaphore);
        }];
        [task resume];

        if (dispatch_semaphore_wait (semaphore, dispatch_time (DISPATCH_TIME_NOW, 120 * NSEC_PER_SEC)) != 0)
        {
            [task cancel];
            output = "Simulator install bridge timed out";
            return false;
        }

        if (requestError != nil || responseData == nil)
        {
            output = requestError != nil ? String (CharPointer_UTF8 (requestError.localizedDescription.UTF8String))
                                         : "Simulator install bridge returned no response";
            return false;
        }

        id responseObject = [NSJSONSerialization JSONObjectWithData: responseData options: 0 error: nil];

        if (![responseObject isKindOfClass: NSDictionary.class])
        {
            output = "Invalid response from Simulator install bridge";
            return false;
        }

        NSDictionary* responseDictionary = (NSDictionary*) responseObject;

        if (![responseDictionary[@"success"] boolValue])
        {
            id bridgeOutput = responseDictionary[@"output"];
            output = [bridgeOutput isKindOfClass: NSString.class]
                       ? String (CharPointer_UTF8 (((NSString*) bridgeOutput).UTF8String))
                       : "Simulator install bridge failed";
            return false;
        }

        return true;
       #else
        ignoreUnused (appPath, manifestJson);
        output = "Simulator installation is only available in a Simulator build";
        return false;
       #endif
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
                appendLine (String (CharPointer_UTF8 (line.UTF8String)));
            }];

            const auto host = [NSString stringWithFormat: @"%@.backloop.dev",
                               NSUUID.UUID.UUIDString.lowercaseString];
            const auto base = [NSString stringWithFormat: @"https://%@:%u", host, kLoopbackPort];
            /*  JUCE は MRC。ここでの autorelease 文字列を __block に入れると、
                この dispatch_sync が終わった瞬間に解放される。 */
            manifestURL = [[base stringByAppendingString: @"/manifest.plist"] copy];

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
            [manifestURL release];
            return;
        }

        if (! [server waitUntilReadyWithTimeout: kLoopbackReadyTimeoutSeconds])
        {
            appendLine ("loopback listener did not become ready; not opening itms-services");
            dispatch_sync (dispatch_get_main_queue(), ^{ [server stop]; });
            [manifestURL release];
            return;
        }

        dispatch_sync (dispatch_get_main_queue(), ^
        {
            appendLine ("tap Install when prompted (itms-services)");

            NSString* encoded = [manifestURL stringByAddingPercentEncodingWithAllowedCharacters:
                                 NSCharacterSet.alphanumericCharacterSet];
            NSURL* url = [NSURL URLWithString: [NSString stringWithFormat:
                @"itms-services://?action=download-manifest&url=%@", encoded]];

            [UIApplication.sharedApplication openURL: url
                                             options: @{}
                                   completionHandler: ^(BOOL ok)
            {
                appendLine (ok ? "install requested - answer the prompt"
                               : "openURL refused the itms-services URL");
            }];
        });

        [manifestURL release];
    }

    Component::SafePointer<DialogWindow> dialogPtr;
    Component::SafePointer<OnDeviceProgressPanel> panelPtr;
    ScopedMessageBox cancelPrompt;
    LoopbackServer* server = nil;
    id backgroundObserver = nil;
    BOOL previousIdleTimerDisabled = NO;
    std::atomic<bool> cancelRequested { false };
    std::atomic<bool>* externalCancel = nullptr;
    juce::CriticalSection buildLock;
    juce::CriticalSection logLock;
    String* captureLog = nullptr;
    std::atomic<bool> lastSuccess { false };
    bool building = false;
    bool closeWhenFinished = false;
    bool cancelPromptShown = false;
    bool linkerDisabled = false;
};
} // namespace

bool startOnDeviceBuild (ProjectExporter& exporter)
{
    return OnDeviceBuildController::get().start (exporter);
}

bool runOnDeviceBuildCapturingLog (const File& projectRoot,
                                   String& logOut,
                                   std::atomic<bool>& cancelled)
{
    return OnDeviceBuildController::get().runCapturing (projectRoot, logOut, cancelled);
}

#endif
