#include "jucer_OAuthCallbackServer.h"

#include <utility>

namespace
{
    constexpr int maxRequestBytes = 8192;
    constexpr int requestReadTimeoutMs = 5000;

    bool isAccountsOrigin (const juce::String& origin)
    {
        return origin == "https://accounts.x.ai" || origin == "https://auth.x.ai";
    }

    juce::String headerValue (const juce::String& request, const juce::String& name)
    {
        const auto needle = "\n" + name + ":";
        const auto lowerRequest = request.toLowerCase();
        const auto lowerNeedle = needle.toLowerCase();
        const auto index = lowerRequest.indexOf (lowerNeedle);

        if (index < 0)
            return {};

        auto value = request.substring (index + needle.length()).upToFirstOccurrenceOf ("\n", false, false);
        return value.trim();
    }

    juce::String corsHeaders (bool allowAccountsCors, const juce::String& origin)
    {
        if (! allowAccountsCors || ! isAccountsOrigin (origin))
            return {};

        return "Access-Control-Allow-Origin: " + origin + "\r\n"
             + "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
             + "Access-Control-Allow-Headers: *\r\n"
             + "Access-Control-Allow-Private-Network: true\r\n"
             + "Vary: Origin\r\n";
    }

    juce::String makeResponse (bool success, const juce::String& extraHeaders)
    {
        const juce::String body =
            juce::String ("<!doctype html><meta charset=\"utf-8\">"
                          "<title>Projucer</title>"
                          "<div style=\"font-family:-apple-system,sans-serif;"
                          "text-align:center;margin-top:20vh;font-size:1.2rem\">")
              + (success ? "Signed in. You can return to Projucer."
                         : "Sign-in failed. Return to Projucer and try again.")
              + "</div>";

        return juce::String ("HTTP/1.1 ") + (success ? "200 OK" : "400 Bad Request") + "\r\n"
                 + extraHeaders
                 + "Content-Type: text/html; charset=utf-8\r\n"
                 + "Content-Length: " + juce::String (body.getNumBytesAsUTF8()) + "\r\n"
                 + "Connection: close\r\n\r\n"
                 + body;
    }

    juce::String queryValue (const juce::String& target, const juce::String& key)
    {
        auto query = target;

        if (! query.containsChar ('?') && query.containsChar ('='))
            query = "?" + query;

        const auto questionMark = query.indexOfChar ('?');

        if (questionMark < 0)
            return {};

        for (const auto& pair : juce::StringArray::fromTokens (query.substring (questionMark + 1), "&", {}))
            if (pair.upToFirstOccurrenceOf ("=", false, false) == key)
                return juce::URL::removeEscapeChars (pair.fromFirstOccurrenceOf ("=", false, false));

        return {};
    }
}

OAuthCallbackServer::~OAuthCallbackServer()
{
    stop();
}

void OAuthCallbackServer::setCallbackPath (juce::String path)
{
    callbackPath = std::move (path);
}

void OAuthCallbackServer::setAllowAccountsCors (bool shouldAllow)
{
    allowAccountsCors = shouldAllow;
}

void OAuthCallbackServer::submitPastedInput (const juce::String& input)
{
    const auto trimmed = input.trim();

    if (trimmed.isEmpty())
        return;

    juce::String code, state;

    if (trimmed.containsChar ('=') || trimmed.contains ("http://") || trimmed.contains ("https://"))
    {
        code  = queryValue (trimmed, "code");
        state = queryValue (trimmed, "state");
    }

    if (code.isEmpty())
        code = trimmed;

    {
        const juce::ScopedLock sl (lock);
        pastedCode = code;
        pastedState = state;
        hasPasted = true;
    }

    pastedEvent.signal();
}

bool OAuthCallbackServer::takePastedCode (juce::String& codeOut, juce::String& stateOut)
{
    const juce::ScopedLock sl (lock);

    if (! hasPasted || pastedCode.isEmpty())
        return false;

    codeOut = pastedCode;
    stateOut = pastedState;
    hasPasted = false;
    return true;
}

bool OAuthCallbackServer::start (int preferredPort)
{
    // localhost にだけ bind する。外から到達させない。
    if (listener.createListener (preferredPort, "127.0.0.1"))
    {
        port = preferredPort;
        DBG ("[AI][oauth] listening on 127.0.0.1:" << port);
        return true;
    }

    // 埋まっていたら OS に空きを選ばせる。redirect_uri は実ポートで組み立てる。
    if (listener.createListener (0, "127.0.0.1"))
    {
        port = listener.getBoundPort();
        return port > 0;
    }

    return false;
}

void OAuthCallbackServer::stop()
{
    listener.close();
    pastedEvent.signal();

    const juce::ScopedLock sl (lock);
    hasPasted = false;
    pastedCode.clear();
    pastedState.clear();
    port = 0;
}

bool OAuthCallbackServer::waitForCode (std::atomic<bool>& shouldStop,
                                       juce::String& codeOut,
                                       juce::String& stateOut,
                                       juce::String& errorOut)
{
    while (! shouldStop.load())
    {
        if (takePastedCode (codeOut, stateOut))
            return true;

        /*  iOS では listen socket が ready のまま accept で止まることがある。
            貼り付けは WaitableEvent だけで起こす。ループバックは macOS だけ。 */
       #if ! JUCE_IOS
        if (listener.waitUntilReady (true, 0) == 1)
        {
            std::unique_ptr<juce::StreamingSocket> connection (listener.waitForNextConnection());

        if (connection == nullptr)
            continue;

        DBG ("[AI][oauth] accepted a connection");

        juce::MemoryBlock request;
        const auto readDeadline = juce::Time::getMillisecondCounter() + (juce::uint32) requestReadTimeoutMs;

        while ((int) request.getSize() < maxRequestBytes)
        {
            if (juce::Time::getMillisecondCounter() > readDeadline)
                break;

            if (connection->waitUntilReady (true, 100) != 1)
                continue;

            char buffer[1024];
            const auto bytesRead = connection->read (buffer, sizeof (buffer), false);

            if (bytesRead <= 0)
                break;

            request.append (buffer, (size_t) bytesRead);

            const auto text = request.toString();

            if (text.contains ("\r\n\r\n") || text.contains ("\n\n"))
                break;
        }

        const auto requestText = request.toString();
        const auto requestLine = requestText.upToFirstOccurrenceOf ("\r\n", false, false);
        const auto method = requestLine.upToFirstOccurrenceOf (" ", false, false).toUpperCase();
        const auto origin = headerValue (requestText, "Origin");
        const auto extraHeaders = corsHeaders (allowAccountsCors, origin);

        DBG ("[AI][oauth] request line: "
             << requestLine.upToFirstOccurrenceOf ("?", false, false)
             << "  (" << request.getSize() << " bytes)");

        const auto target = requestLine.fromFirstOccurrenceOf (" ", false, false)
                                       .upToLastOccurrenceOf (" ", false, false);
        const auto pathOnly = target.upToFirstOccurrenceOf ("?", false, false);

        if (method == "OPTIONS")
        {
            const juce::String preflight ("HTTP/1.1 204 No Content\r\n"
                                          + extraHeaders
                                          + "Content-Length: 0\r\n"
                                          + "Connection: close\r\n\r\n");
            connection->write (preflight.toRawUTF8(), (int) preflight.getNumBytesAsUTF8());
            continue;
        }

        if (pathOnly != callbackPath)
        {
            const juce::String notFound ("HTTP/1.1 404 Not Found\r\n"
                                         + extraHeaders
                                         + "Content-Length: 0\r\n"
                                         + "Connection: close\r\n\r\n");
            connection->write (notFound.toRawUTF8(), (int) notFound.getNumBytesAsUTF8());
            continue;
        }

        codeOut  = queryValue (target, "code");
        stateOut = queryValue (target, "state");

        const auto oauthError = queryValue (target, "error");
        const auto success = oauthError.isEmpty() && codeOut.isNotEmpty();

        const auto response = makeResponse (success, extraHeaders);
        connection->write (response.toRawUTF8(), (int) response.getNumBytesAsUTF8());

        if (! success)
        {
            errorOut = oauthError.isNotEmpty() ? "Sign-in was denied (" + oauthError + ")."
                                               : "No authorization code was returned.";
            return false;
        }

        return true;
        }
       #endif

        pastedEvent.wait (100);
    }

    errorOut = "Sign-in was cancelled.";
    return false;
}
