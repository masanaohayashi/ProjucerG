#pragma once

#include "jucer_CodexClient.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>

/*  ChatGPT サブスクによるサインインを扱う。

    ネットワークを叩くメソッドはワーカースレッドから呼ぶこと。メッセージスレッドから
    呼んではいけない。

    サインインの経路は二つある。

    1. signInWithBrowser（既定）
       認可ページをブラウザに出し、ループバックの待ち受けで認可コードを受け取る。
       PKCE はクライアント側で生成する。前提条件は無い。

    2. requestDeviceCode + pollForTokens（代替）
       コードを表示してユーザーに入力してもらう。ローカルの待ち受けが要らない代わりに、
       ChatGPT のセキュリティ設定でデバイスコード認証を有効にしたアカウントでしか使えない。
       この経路では PKCE のペアを認可サービスが生成して返す。

    中断について: shouldStop を渡すと待ち時間の区切りで抜ける。ただしそれだけでは
    ソケットの読み取りでブロックしている間は反応できないため、cancelActiveRequest() が
    実行中のリクエストそのものを畳めるようにしてある。UI の「中止」はこの両方を使う。
*/
class CodexAuth : public ResponsesAuth
{
public:
    CodexAuth();
    ~CodexAuth() override;

    struct Tokens
    {
        juce::String accessToken;
        juce::String refreshToken;
        juce::String accountId;
    };

    struct DeviceCode
    {
        juce::String deviceAuthId;
        juce::String userCode;
        juce::String verificationUrl;
        int intervalSeconds = 5;
    };

    bool isSignedIn() const;
    juce::String getAccessToken() const override;
    juce::String extraHeaders() const override;
    juce::String getAccountId() const;

    /** 既定のサインイン経路。認可ページを提示し、ループバックでコードを受け取り、
        トークンまで交換して保存する。 */
    bool signInWithBrowser (std::atomic<bool>& shouldStop, juce::String& errorOut);

    /** 代替経路の手順 1。ユーザーへ見せるコードと URL を得る。 */
    std::optional<DeviceCode> requestDeviceCode (juce::String& errorOut,
                                                 std::atomic<bool>* shouldStop = nullptr);

    /** 代替経路の手順 2。ユーザーがブラウザで承認するまで待つ。最大 15 分。 */
    bool pollForTokens (const DeviceCode&, std::atomic<bool>& shouldStop, juce::String& errorOut);

    /** access token を更新する。成功したら保存し直す。 */
    bool refresh (juce::String& errorOut, std::atomic<bool>* shouldStop = nullptr) override;

    /** 実行中のリクエストを畳む。読み取りでブロックしていても戻ってくる。 */
    void cancelActiveRequest() override;

    void signOut() override;

    static constexpr const char* clientId = "app_EMoamEEZ73f0CkXaXp7hrann";
    static constexpr const char* issuer = "https://auth.openai.com";

private:
    class ActiveStream;

    void load();
    bool save() const;
    static juce::String extractAccountId (const juce::String& idToken);

    juce::String postJson (const juce::String& url,
                           const juce::String& body,
                           int& statusOut,
                           std::atomic<bool>* shouldStop);
    juce::String postForm (const juce::String& url,
                           const juce::String& body,
                           int& statusOut,
                           std::atomic<bool>* shouldStop);

    juce::String post (const juce::String& url,
                       const juce::String& body,
                       const juce::String& contentType,
                       int& statusOut,
                       std::atomic<bool>* shouldStop);

    /** トークン応答を取り込んで保存する。世代が進んでいたら破棄する。 */
    bool adoptTokenResponse (const juce::String& response,
                             std::uint64_t generationAtStart,
                             juce::String& errorOut);

    Tokens tokens;
    mutable juce::CriticalSection lock;

    /*  更新を直列化する。401 が同時に複数返ってきたときに、同じ refresh token で
        二重に交換して片方を無効化してしまうのを防ぐ。 */
    mutable juce::CriticalSection refreshLock;

    /*  signOut や中止のたびに進む。進行中のサインインが完了しても、開始時より
        世代が進んでいれば結果を捨てる。畳んだはずのサインインが後から
        トークンを書き戻すのを防ぐ。 */
    std::atomic<std::uint64_t> stateGeneration { 0 };

    std::shared_ptr<ActiveStream> activeStream;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CodexAuth)
};
