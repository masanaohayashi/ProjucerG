#pragma once

#include "jucer_CodexClient.h"

#include <juce_core/juce_core.h>

class OAuthCallbackServer;

#include <atomic>
#include <cstdint>
#include <memory>

/*  Grok サブスクによるサインインを扱う。

    Grok Build (xai-org/grok-build) と同じ公開クライアントで、auth.x.ai に
    PKCE のブラウザ認可を出す。トークンはサブスク枠に載る。API キーは使わない。

    ネットワークを叩くメソッドはワーカースレッドから呼ぶこと。
*/
class GrokAuth : public ResponsesAuth
{
public:
    GrokAuth();
    ~GrokAuth() override;

    struct Tokens
    {
        juce::String accessToken;
        juce::String refreshToken;
    };

    bool isSignedIn() const;
    juce::String getAccessToken() const override;
    juce::String extraHeaders() const override;

    bool signInWithBrowser (std::atomic<bool>& shouldStop, juce::String& errorOut);

    /** ブラウザに出たコードを貼る。サインイン待ちのときだけ効く。 */
    bool submitPastedInput (const juce::String& input);

    bool refresh (juce::String& errorOut, std::atomic<bool>* shouldStop = nullptr) override;
    void cancelActiveRequest() override;
    void signOut() override;

    static constexpr const char* clientId = "b1a00492-073a-47ea-816f-4c329264a828";
    static constexpr const char* issuer   = "https://auth.x.ai";

private:
    class ActiveStream;

    void load();
    bool save() const;

    juce::String postForm (const juce::String& url,
                           const juce::String& body,
                           int& statusOut,
                           std::atomic<bool>* shouldStop);

    bool adoptTokenResponse (const juce::String& response,
                             std::uint64_t generationAtStart,
                             juce::String& errorOut);

    Tokens tokens;
    OAuthCallbackServer* pendingCallback = nullptr;
    mutable juce::CriticalSection lock;
    mutable juce::CriticalSection refreshLock;
    std::atomic<std::uint64_t> stateGeneration { 0 };
    std::shared_ptr<ActiveStream> activeStream;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrokAuth)
};
