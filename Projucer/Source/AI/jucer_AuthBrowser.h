#pragma once

#include <juce_core/juce_core.h>

#include <functional>

/*  認可ページをユーザーへ見せる。

    iOS では外部 Safari へ切り替えるとアプリがバックグラウンドへ回り、
    ループバックの待ち受けが止まってコールバックを取りこぼす。そのため
    ASWebAuthenticationSession でアプリ内に提示し、前面を保つ。
    macOS では通常のブラウザで開く。
*/
/** keepAppInteractive が true のとき、iPad ではシートにして裏の貼り付け欄を触れるようにする。 */
void presentAuthPage (const juce::String& authorizeUrl, bool keepAppInteractive = false);

/** 提示中のページを閉じる。コールバックを受け取った後に呼ぶ。 */
void dismissAuthPage();

/*  サインインの間だけ、バックグラウンドでも動き続ける許可を OS へ求める。

    iPad では ChatGPT アプリ側で承認する経路があり、そのときアプリは
    バックグラウンドへ回る。何もしないとループバックの待ち受けが止まり、
    Safari が戻ってきたときにリダイレクトを受け取れない。

    iOS 以外では何もしない。
*/
void beginAuthBackgroundTask();
void endAuthBackgroundTask();

/*  iOS の SFSafariViewController 表示中は JUCE の callAsync が回らない。
    システムのメインキューへ直接載せる。 */
void runOnAppMainThread (std::function<void()> fn);
