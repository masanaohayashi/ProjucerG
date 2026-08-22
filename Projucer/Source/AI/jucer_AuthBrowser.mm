#include "jucer_AuthBrowser.h"

#include <juce_events/juce_events.h>

#import <Foundation/Foundation.h>

/*  このプロジェクトは ARC を使わない（jucer_Keychain.mm 参照）。__bridge 系の
    キャストは書かず、alloc したオブジェクトは明示的に release する。
*/

#if JUCE_IOS

#import <SafariServices/SafariServices.h>
#import <UIKit/UIKit.h>

/*  iOS では ASWebAuthenticationSession ではなく SFSafariViewController を使う。

    ASWebAuthenticationSession はコールバックを URL スキームで横取りする仕組みで、
    スキームを渡さないと即座に「cancelled」で終わる。こちらの設計では認可コードを
    ループバックの HTTP 待ち受けで受け取るので、横取りは不要。必要なのは
    「アプリを前面に保ったまま認可ページを見せる」ことだけで、それには
    SFSafariViewController が過不足なく合う。

    外部の Safari へ切り替えてはいけない。アプリがバックグラウンドへ回ると
    待ち受けが止まり、コールバックを取りこぼす。
*/

namespace
{
    SFSafariViewController* activeController = nil;

    UIViewController* findPresentingController()
    {
        for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
        {
            if (! [scene isKindOfClass: [UIWindowScene class]])
                continue;

            for (UIWindow* window in ((UIWindowScene*) scene).windows)
            {
                if (! window.isKeyWindow)
                    continue;

                auto* controller = window.rootViewController;

                // 既に何かを出している場合は、その一番手前に重ねる。
                while (controller.presentedViewController != nil)
                    controller = controller.presentedViewController;

                return controller;
            }
        }

        return nil;
    }

    /*  UIKit はメインスレッドからしか触れない。サインインはワーカースレッドで
        走るので、必ずここを通してメインへ渡す。 */
    void onMainThread (dispatch_block_t block)
    {
        if ([NSThread isMainThread])
            block();
        else
            dispatch_async (dispatch_get_main_queue(), block);
    }
}

namespace
{
    UIBackgroundTaskIdentifier authBackgroundTask = UIBackgroundTaskInvalid;
}

void beginAuthBackgroundTask()
{
    onMainThread (^{
        if (authBackgroundTask != UIBackgroundTaskInvalid)
            return;

        auto* application = UIApplication.sharedApplication;

        authBackgroundTask =
            [application beginBackgroundTaskWithName: @"Projucer sign-in"
                                   expirationHandler: ^{
                // 猶予が尽きた。ここで畳まないと OS にアプリを止められる。
                if (authBackgroundTask != UIBackgroundTaskInvalid)
                {
                    [UIApplication.sharedApplication endBackgroundTask: authBackgroundTask];
                    authBackgroundTask = UIBackgroundTaskInvalid;
                }
            }];
    });
}

void endAuthBackgroundTask()
{
    onMainThread (^{
        if (authBackgroundTask == UIBackgroundTaskInvalid)
            return;

        [UIApplication.sharedApplication endBackgroundTask: authBackgroundTask];
        authBackgroundTask = UIBackgroundTaskInvalid;
    });
}

void presentAuthPage (const juce::String& authorizeUrl, bool keepAppInteractive)
{
    auto* urlString = [NSString stringWithUTF8String: authorizeUrl.toRawUTF8()];
    auto* url = [NSURL URLWithString: urlString];

    if (url == nil)
        return;

    [url retain];

    onMainThread (^{
        if (activeController != nil)
        {
            [activeController dismissViewControllerAnimated: NO completion: nil];
            [activeController release];
            activeController = nil;
        }

        auto* controller = [[SFSafariViewController alloc] initWithURL: url];
        [url release];

        if (keepAppInteractive)
        {
            /*  Grok はコードをアプリへ貼る。全面モーダルだと貼り付け欄に
                届かないので、シートにして裏を触れるようにする。 */
            controller.modalPresentationStyle = UIModalPresentationPageSheet;

            if (auto* sheet = controller.sheetPresentationController)
            {
                sheet.detents = @[
                    [UISheetPresentationControllerDetent mediumDetent],
                    [UISheetPresentationControllerDetent largeDetent]
                ];
                sheet.selectedDetentIdentifier = UISheetPresentationControllerDetentIdentifierMedium;
                sheet.prefersGrabberVisible = YES;
                sheet.largestUndimmedDetentIdentifier = UISheetPresentationControllerDetentIdentifierMedium;
            }
        }

        if (auto* presenter = findPresentingController())
        {
            activeController = controller;
            [presenter presentViewController: controller animated: YES completion: nil];
        }
        else
        {
            // 出す先が見つからないなら、開きっぱなしにせず畳む。
            [controller release];
        }
    });
}

void dismissAuthPage()
{
    onMainThread (^{
        if (activeController == nil)
            return;

        auto* controller = activeController;
        activeController = nil;
        [controller dismissViewControllerAnimated: NO completion: nil];
        [controller release];
    });
}

void runOnAppMainThread (std::function<void()> fn)
{
    auto* heapFn = new std::function<void()> (std::move (fn));

    dispatch_async (dispatch_get_main_queue(), ^{
        (*heapFn)();
        delete heapFn;
    });
}

#else

void presentAuthPage (const juce::String& authorizeUrl, bool)
{
    juce::URL (authorizeUrl).launchInDefaultBrowser();
}

void dismissAuthPage()
{
}

void beginAuthBackgroundTask()
{
}

void endAuthBackgroundTask()
{
}

void runOnAppMainThread (std::function<void()> fn)
{
    juce::MessageManager::callAsync (std::move (fn));
}

#endif
