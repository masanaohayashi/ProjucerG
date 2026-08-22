#include "jucer_CodexClient.h"

#include <string>
#include <thread>
#include <utility>

class CodexClient::ActiveStream final
{
public:
    void begin (juce::WebInputStream* streamToUse, bool shouldCancel)
    {
        const juce::ScopedLock scopedLock (lock);
        stream = streamToUse;
        cancelled = shouldCancel;

        if (cancelled && stream != nullptr)
            stream->cancel();
    }

    void clear()
    {
        const juce::ScopedLock scopedLock (lock);
        stream = nullptr;
    }

    void cancel()
    {
        const juce::ScopedLock scopedLock (lock);
        cancelled = true;

        if (stream != nullptr)
            stream->cancel();
    }

private:
    juce::CriticalSection lock;
    juce::WebInputStream* stream = nullptr;
    bool cancelled = false;
};

namespace
{
    bool isTerminalEvent (const juce::var& event)
    {
        if (! event.isObject())
            return false;

        const auto type = event.getProperty ("type", {}).toString();
        return type == "completed"
            || type == "response.completed"
            || type == "response.failed"
            || type == "error";
    }
}

CodexClient::CodexClient (std::shared_ptr<ResponsesAuth> authToUse, juce::String baseUrlToUse)
    : auth (std::move (authToUse)),
      baseUrl (std::move (baseUrlToUse)),
      activeStream (std::make_shared<ActiveStream>())
{
}

void CodexClient::cancelActiveRequest()
{
    activeStream->cancel();
    auth->cancelActiveRequest();
}

bool CodexClient::attempt (const juce::var& requestBody,
                           std::atomic<bool>& shouldStop,
                           const std::function<void (const juce::var&)>& onEvent,
                           int& statusOut,
                           juce::String& errorOut)
{
    statusOut = 0;
    errorOut.clear();

    if (shouldStop.load (std::memory_order_acquire))
        return true;

    juce::String headers;
    headers << "Authorization: Bearer " << auth->getAccessToken() << "\r\n"
            << auth->extraHeaders()
            << "session_id: " << sessionId << "\r\n"
            << "Content-Type: application/json\r\n"
            << "Accept: text/event-stream";

    juce::URL url (juce::String (baseUrl) + "/responses");
    url = url.withPOSTData (juce::JSON::toString (requestBody));

    auto stream = std::make_unique<juce::WebInputStream> (url, true);
    stream->withExtraHeaders (headers)
          .withConnectionTimeout (60000);
    activeStream->begin (stream.get(), shouldStop.load (std::memory_order_acquire));

    const auto connected = stream->connect (nullptr);
    statusOut = stream->getStatusCode();

    if (! connected || stream->isError())
    {
        activeStream->clear();
        errorOut = "Unable to connect to the model service.";
        return false;
    }

    if (statusOut < 200 || statusOut >= 300)
    {
        /*  エラー応答の本文には理由が入っている。捨てるとステータス番号だけが
            残り、どこが悪いのか分からなくなる。読んで errorOut に載せる。
            トークンらしき文字列が混ざっていた場合は、呼び出し側の
            safeErrorMessage が伏せる。 */
        const auto body = stream->readEntireStreamAsString().trim();

        activeStream->clear();

        if (body.isNotEmpty())
        {
            // JSON なら message だけを取り出す。素の本文より読みやすい。
            const auto parsed = juce::JSON::parse (body);
            const auto errorField = parsed.getProperty ("error", {});
            auto detail = errorField.isString() ? errorField.toString()
                                                : errorField.getProperty ("message", {}).toString();

            if (detail.isEmpty())
                detail = parsed.getProperty ("detail", {}).toString();

            if (detail.isEmpty())
                detail = body.substring (0, 400);

            errorOut = "The request failed (HTTP " + juce::String (statusOut) + "): " + detail;
        }

        return false;
    }

    SseParser parser;
    juce::HeapBlock<char> chunk (16384);

    bool sawTerminalEvent = false;

    while (! shouldStop.load (std::memory_order_acquire))
    {
        const auto bytesRead = stream->read (chunk.getData(), 16384);

        if (bytesRead <= 0)
            break;

        for (const auto& payload : parser.feed (std::string (chunk.getData(),
                                                               static_cast<std::size_t> (bytesRead))))
        {
            if (payload == "[DONE]")
            {
                activeStream->clear();
                return true;
            }

            const auto event = juce::JSON::parse (payload);

            if (event.isVoid())
            {
                if (! payload.empty())
                {
                    errorOut = "The model stream contained invalid JSON.";
                    activeStream->clear();
                    return false;
                }

                continue;
            }

            sawTerminalEvent = sawTerminalEvent || isTerminalEvent (event);

            if (! shouldStop.load (std::memory_order_acquire) && onEvent)
                onEvent (event);
        }
    }

    if (shouldStop.load (std::memory_order_acquire))
    {
        activeStream->clear();
        return true;
    }

    if (! sawTerminalEvent)
    {
        errorOut = "The model stream ended before completion.";
        activeStream->clear();
        return false;
    }

    activeStream->clear();
    return true;
}

bool CodexClient::streamResponse (const juce::var& requestBody,
                                  std::atomic<bool>& shouldStop,
                                  std::function<void (const juce::var&)> onEvent,
                                  juce::String& errorOut)
{
    errorOut.clear();

    int status = 0;

    if (shouldStop.load (std::memory_order_acquire))
        return true;

    std::atomic<bool> watcherShouldStop { false };
    std::thread cancellationWatcher ([this, &shouldStop, &watcherShouldStop]
    {
        while (! watcherShouldStop.load (std::memory_order_acquire)
               && ! shouldStop.load (std::memory_order_acquire))
            juce::Thread::sleep (10);

        if (shouldStop.load (std::memory_order_acquire))
            cancelActiveRequest();
    });

    const auto finish = [&watcherShouldStop, &cancellationWatcher]
    {
        watcherShouldStop.store (true, std::memory_order_release);
        if (cancellationWatcher.joinable())
            cancellationWatcher.join();
    };

    if (attempt (requestBody, shouldStop, onEvent, status, errorOut))
    {
        finish();
        return true;
    }

    if (status != 401)
    {
        if (errorOut.isEmpty())
            errorOut = status > 0
                     ? "The request failed (HTTP " + juce::String (status) + ")."
                     : "The request failed.";

        finish();
        return false;
    }

    if (shouldStop.load (std::memory_order_acquire))
    {
        finish();
        return true;
    }

    if (! auth->refresh (errorOut, &shouldStop))
    {
        finish();
        return false;
    }

    if (shouldStop.load (std::memory_order_acquire))
    {
        finish();
        return true;
    }

    if (attempt (requestBody, shouldStop, onEvent, status, errorOut))
    {
        finish();
        return true;
    }

    if (shouldStop.load (std::memory_order_acquire))
    {
        finish();
        return true;
    }

    if (errorOut.isEmpty())
        errorOut = status == 401
                 ? "Authentication is required."
                 : (status > 0
                    ? "The request failed (HTTP " + juce::String (status) + ")."
                    : "The request failed.");

    if (status == 401)
        auth->signOut();

    finish();
    return false;
}
