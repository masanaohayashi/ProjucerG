#include "jucer_OAuthCallbackServer.h"

namespace
{
    constexpr int maxRequestBytes = 8192;

    /*  1 接続あたり、要求行が届くのを待つ上限。 */
    constexpr int requestReadTimeoutMs = 5000;

    /*  ブラウザに返す最小のページ。ここで案内を出さないと、ユーザーは
        認可が終わったのかどうか分からないまま画面を見ることになる。 */
    juce::String makeResponse (bool success)
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
                 + "Content-Type: text/html; charset=utf-8\r\n"
                 + "Content-Length: " + juce::String (body.getNumBytesAsUTF8()) + "\r\n"
                 + "Connection: close\r\n\r\n"
                 + body;
    }

    /*  "GET /auth/callback?code=x&state=y HTTP/1.1" から値を取り出す。 */
    juce::String queryValue (const juce::String& target, const juce::String& key)
    {
        const auto questionMark = target.indexOfChar ('?');

        if (questionMark < 0)
            return {};

        for (const auto& pair : juce::StringArray::fromTokens (target.substring (questionMark + 1), "&", {}))
            if (pair.upToFirstOccurrenceOf ("=", false, false) == key)
                return juce::URL::removeEscapeChars (pair.fromFirstOccurrenceOf ("=", false, false));

        return {};
    }
}

OAuthCallbackServer::~OAuthCallbackServer()
{
    stop();
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
    port = 0;
}

bool OAuthCallbackServer::waitForCode (std::atomic<bool>& shouldStop,
                                       juce::String& codeOut,
                                       juce::String& stateOut,
                                       juce::String& errorOut)
{
    while (! shouldStop.load())
    {
        // 中断要求に応じられるよう、細かく区切って待つ。
        if (listener.waitUntilReady (true, 200) != 1)
            continue;

        std::unique_ptr<juce::StreamingSocket> connection (listener.waitForNextConnection());

        if (connection == nullptr)
            continue;

        DBG ("[AI][oauth] accepted a connection");

        juce::MemoryBlock request;

        /*  接続が成立した時点ではまだ要求のバイト列は届いていない。ここで
            非ブロッキングの read を掛けると 0 が返り、空の要求として扱ってしまう。
            要求行が揃うまで待つこと。 */
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
                break;   // 相手が閉じた

            request.append (buffer, (size_t) bytesRead);

            // 要求行だけ読めればよい。ヘッダの終端までは待たない。
            if (request.toString().containsChar ('\n'))
                break;
        }

        const auto requestLine = request.toString().upToFirstOccurrenceOf ("\r\n", false, false);

        // code の値は伏せ、経路と長さだけ残す。
        DBG ("[AI][oauth] request line: "
             << requestLine.upToFirstOccurrenceOf ("?", false, false)
             << "  (" << request.getSize() << " bytes)");
        const auto target = requestLine.fromFirstOccurrenceOf (" ", false, false)
                                       .upToLastOccurrenceOf (" ", false, false);

        /*  ブラウザは favicon なども取りに来る。目的の経路以外にサインイン失敗の
            ページを返すと、実際には成功しているのに失敗表示になりうる。
            素っ気ない 404 を返して待ち続ける。 */
        if (! target.startsWith ("/auth/callback"))
        {
            const juce::String notFound ("HTTP/1.1 404 Not Found\r\n"
                                         "Content-Length: 0\r\n"
                                         "Connection: close\r\n\r\n");
            connection->write (notFound.toRawUTF8(), (int) notFound.getNumBytesAsUTF8());
            continue;
        }

        codeOut  = queryValue (target, "code");
        stateOut = queryValue (target, "state");

        const auto oauthError = queryValue (target, "error");
        const auto success = oauthError.isEmpty() && codeOut.isNotEmpty();

        const auto response = makeResponse (success);
        connection->write (response.toRawUTF8(), (int) response.getNumBytesAsUTF8());

        if (! success)
        {
            errorOut = oauthError.isNotEmpty() ? "Sign-in was denied (" + oauthError + ")."
                                               : "No authorization code was returned.";
            return false;
        }

        return true;
    }

    errorOut = "Sign-in was cancelled.";
    return false;
}
