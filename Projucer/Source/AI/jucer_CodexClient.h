#pragma once

#include "jucer_CodexAuth.h"
#include "jucer_SseParser.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <memory>

/* Sends streaming Responses API requests to the Codex backend.

   This is the only class that owns Codex-specific HTTP knowledge. The request
   method blocks while reading the network stream and must not be called from
   the message thread. Events are delivered on the calling thread.
*/
class CodexClient
{
public:
    explicit CodexClient (std::shared_ptr<CodexAuth> authToUse);

    /* Sends a request and delivers each decoded SSE JSON event to onEvent.
       A 401 response refreshes the access token and retries exactly once. */
    bool streamResponse (const juce::var& requestBody,
                         std::atomic<bool>& shouldStop,
                         std::function<void (const juce::var&)> onEvent,
                         juce::String& errorOut);

    void cancelActiveRequest();

    static constexpr const char* baseUrl = "https://chatgpt.com/backend-api/codex";

private:
    class ActiveStream;

    bool attempt (const juce::var& requestBody,
                  std::atomic<bool>& shouldStop,
                  const std::function<void (const juce::var&)>& onEvent,
                  int& statusOut,
                  juce::String& errorOut);

    std::shared_ptr<CodexAuth> auth;
    juce::String sessionId { juce::Uuid().toDashedString() };
    std::shared_ptr<ActiveStream> activeStream;
};
