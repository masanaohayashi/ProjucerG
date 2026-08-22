#pragma once

#include <juce_core/juce_core.h>
#include <atomic>

/*  OAuth のリダイレクトを受けるだけの、極小のループバック HTTP 待ち受け。

    ローカルホストにしか bind しない。単一の要求 (/auth/callback) を処理したら
    役目を終える。汎用の HTTP サーバーではない。
*/
class OAuthCallbackServer
{
public:
    ~OAuthCallbackServer();

    /** 待ち受けを開始する。preferredPort が埋まっていれば OS 任せの空きポートを使う。 */
    bool start (int preferredPort);

    /** 実際に待ち受けているポート。redirect_uri の組み立てに使う。 */
    int getPort() const noexcept    { return port; }

    /** ブラウザからのコールバックを待つ。shouldStop で中断できる。

        @returns  code と state が取れたら true。
    */
    bool waitForCode (std::atomic<bool>& shouldStop,
                      juce::String& codeOut,
                      juce::String& stateOut,
                      juce::String& errorOut);

    void stop();

private:
    juce::StreamingSocket listener;
    int port = 0;
};
