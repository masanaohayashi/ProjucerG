#pragma once

#include "jucer_SseParser.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <memory>

/*  Responses API に載せる資格情報。ChatGPT と Grok で中身は違うが、
    ストリーム側はトークンと追加ヘッダだけ見ればよい。 */
class ResponsesAuth
{
public:
    virtual ~ResponsesAuth() = default;

    virtual juce::String getAccessToken() const = 0;
    virtual juce::String extraHeaders() const = 0;
    virtual bool refresh (juce::String& errorOut, std::atomic<bool>* shouldStop) = 0;
    virtual void cancelActiveRequest() = 0;
    virtual void signOut() = 0;
};

/* Sends streaming Responses API requests.

   The request method blocks while reading the network stream and must not be
   called from the message thread. Events are delivered on the calling thread.
*/
class CodexClient
{
public:
    CodexClient (std::shared_ptr<ResponsesAuth> authToUse, juce::String baseUrlToUse);

    /* Sends a request and delivers each decoded SSE JSON event to onEvent.
       A 401 response refreshes the access token and retries exactly once. */
    bool streamResponse (const juce::var& requestBody,
                         std::atomic<bool>& shouldStop,
                         std::function<void (const juce::var&)> onEvent,
                         juce::String& errorOut);

    void cancelActiveRequest();

    static constexpr const char* chatgptBaseUrl = "https://chatgpt.com/backend-api/codex";
    static constexpr const char* grokBaseUrl    = "https://api.x.ai/v1";

private:
    class ActiveStream;

    bool attempt (const juce::var& requestBody,
                  std::atomic<bool>& shouldStop,
                  const std::function<void (const juce::var&)>& onEvent,
                  int& statusOut,
                  juce::String& errorOut);

    std::shared_ptr<ResponsesAuth> auth;
    juce::String baseUrl;
    juce::String sessionId { juce::Uuid().toDashedString() };
    std::shared_ptr<ActiveStream> activeStream;
};
