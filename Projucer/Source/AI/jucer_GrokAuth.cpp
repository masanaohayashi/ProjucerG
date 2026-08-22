#include "jucer_GrokAuth.h"

#include "jucer_AuthBrowser.h"
#include "jucer_Keychain.h"
#include "jucer_OAuthCallbackServer.h"
#include "jucer_Pkce.h"

namespace
{
    constexpr const char* keychainService = "com.projucer.ai.grok";
    constexpr const char* keychainAccount = "tokens";
    constexpr int connectionTimeoutMs = 30000;

    constexpr const char* oauthScopes =
        "openid profile email offline_access grok-cli:access api:access "
        "conversations:read conversations:write workspaces:read workspaces:write";
}

class GrokAuth::ActiveStream
{
public:
    ActiveStream (const juce::URL& url, const juce::String& extraHeaders)
        : stream (url, true)
    {
        stream.withExtraHeaders (extraHeaders)
              .withConnectionTimeout (connectionTimeoutMs);
    }

    bool connect()
    {
        if (cancelled.load())
            return false;

        return stream.connect (nullptr);
    }

    juce::String readAll()
    {
        if (cancelled.load())
            return {};

        return stream.readEntireStreamAsString();
    }

    int getStatusCode()
    {
        return stream.getStatusCode();
    }

    void cancel()
    {
        cancelled = true;
        stream.cancel();
    }

private:
    juce::WebInputStream stream;
    std::atomic<bool> cancelled { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ActiveStream)
};

GrokAuth::GrokAuth()
{
    load();
}

GrokAuth::~GrokAuth()
{
    cancelActiveRequest();
}

void GrokAuth::load()
{
    const auto stored = keychainRead (keychainService, keychainAccount);

    if (stored.isEmpty())
        return;

    const auto parsed = juce::JSON::parse (stored);
    const juce::ScopedLock sl (lock);
    tokens.accessToken  = parsed.getProperty ("access_token", {}).toString();
    tokens.refreshToken = parsed.getProperty ("refresh_token", {}).toString();
}

bool GrokAuth::save() const
{
    juce::String serialised;

    {
        const juce::ScopedLock sl (lock);

        auto* object = new juce::DynamicObject();
        object->setProperty ("access_token",  tokens.accessToken);
        object->setProperty ("refresh_token", tokens.refreshToken);
        serialised = juce::JSON::toString (juce::var (object));
    }

    return keychainWrite (keychainService, keychainAccount, serialised);
}

bool GrokAuth::isSignedIn() const
{
    const juce::ScopedLock sl (lock);
    return tokens.accessToken.isNotEmpty();
}

juce::String GrokAuth::getAccessToken() const
{
    const juce::ScopedLock sl (lock);
    return tokens.accessToken;
}

juce::String GrokAuth::extraHeaders() const
{
    return {};
}

void GrokAuth::signOut()
{
    ++stateGeneration;
    cancelActiveRequest();

    {
        const juce::ScopedLock sl (lock);
        tokens = {};
    }

    keychainErase (keychainService, keychainAccount);
}

void GrokAuth::cancelActiveRequest()
{
    std::shared_ptr<ActiveStream> stream;

    {
        const juce::ScopedLock sl (lock);
        stream = activeStream;
    }

    if (stream != nullptr)
        stream->cancel();
}

juce::String GrokAuth::postForm (const juce::String& urlString,
                                 const juce::String& body,
                                 int& statusOut,
                                 std::atomic<bool>* shouldStop)
{
    statusOut = 0;

    if (shouldStop != nullptr && shouldStop->load())
        return {};

    const auto url = juce::URL (urlString).withPOSTData (body);
    auto stream = std::make_shared<ActiveStream> (url, "Content-Type: application/x-www-form-urlencoded");

    {
        const juce::ScopedLock sl (lock);
        activeStream = stream;
    }

    if (shouldStop != nullptr && shouldStop->load())
        stream->cancel();

    juce::String response;

    if (stream->connect())
    {
        statusOut = stream->getStatusCode();
        response = stream->readAll();
    }

    DBG ("[AI][grok-auth] POST " << juce::URL (urlString).getSubPath()
         << "  -> HTTP " << statusOut
         << (statusOut >= 200 && statusOut < 300
               ? juce::String()
               : "\n           body: " + response.substring (0, 500)));

    {
        const juce::ScopedLock sl (lock);

        if (activeStream == stream)
            activeStream.reset();
    }

    return response;
}

bool GrokAuth::adoptTokenResponse (const juce::String& response,
                                   std::uint64_t generationAtStart,
                                   juce::String& errorOut)
{
    const auto parsed = juce::JSON::parse (response);
    const auto accessToken  = parsed.getProperty ("access_token", {}).toString();
    const auto refreshToken = parsed.getProperty ("refresh_token", {}).toString();

    if (accessToken.isEmpty())
    {
        errorOut = "Could not read the token response.";
        return false;
    }

    if (stateGeneration.load() != generationAtStart)
    {
        errorOut = "Sign-in was cancelled.";
        return false;
    }

    {
        const juce::ScopedLock sl (lock);
        tokens.accessToken = accessToken;

        if (refreshToken.isNotEmpty())
            tokens.refreshToken = refreshToken;
    }

    if (! save())
    {
        errorOut = "Could not save the tokens to the keychain.";
        return false;
    }

    return true;
}

bool GrokAuth::signInWithBrowser (std::atomic<bool>& shouldStop, juce::String& errorOut)
{
    const auto generationAtStart = stateGeneration.load();

    OAuthCallbackServer server;
    server.setCallbackPath ("/callback");
    server.setAllowAccountsCors (true);

    if (! server.start (0))
    {
        errorOut = "Could not start the local listener for the sign-in callback.";
        return false;
    }

    {
        const juce::ScopedLock sl (lock);
        pendingCallback = &server;
    }

    const auto redirectUri = "http://127.0.0.1:" + juce::String (server.getPort()) + "/callback";
    const auto pkce = generatePkce();
    const auto expectedState = generateOAuthState();
    const auto nonce = generateOAuthState();

    juce::String query;

    const auto add = [&query] (const juce::String& key, const juce::String& value)
    {
        if (query.isNotEmpty())
            query << "&";

        query << key << "=" << juce::URL::addEscapeChars (value, false);
    };

    add ("response_type", "code");
    add ("client_id", clientId);
    add ("redirect_uri", redirectUri);
    add ("scope", oauthScopes);
    add ("code_challenge", pkce.codeChallenge);
    add ("code_challenge_method", "S256");
    add ("state", expectedState);
    add ("nonce", nonce);
    add ("referrer", "grok-build");

    beginAuthBackgroundTask();
    presentAuthPage (juce::String (issuer) + "/oauth2/authorize?" + query);

    juce::String code, returnedState;
    const auto gotCode = server.waitForCode (shouldStop, code, returnedState, errorOut);

    {
        const juce::ScopedLock sl (lock);
        pendingCallback = nullptr;
    }

    dismissAuthPage();
    endAuthBackgroundTask();
    server.stop();

    if (! gotCode)
        return false;

    /*  ブラウザに出たコードを貼ったときは state が空。Grok Build と同じく、
        その場合は CSRF 照合を飛ばす。 */
    if (returnedState.isNotEmpty() && returnedState != expectedState)
    {
        errorOut = "The sign-in response did not match this request. Please try again.";
        return false;
    }

    const auto form = juce::String ("grant_type=authorization_code")
                        + "&code=" + juce::URL::addEscapeChars (code, false)
                        + "&redirect_uri=" + juce::URL::addEscapeChars (redirectUri, false)
                        + "&client_id=" + juce::URL::addEscapeChars (clientId, false)
                        + "&code_verifier=" + juce::URL::addEscapeChars (pkce.codeVerifier, false);

    int status = 0;
    const auto response = postForm (juce::String (issuer) + "/oauth2/token", form, status, &shouldStop);

    if (shouldStop.load())
    {
        errorOut = "Sign-in was cancelled.";
        return false;
    }

    if (status < 200 || status >= 300)
    {
        errorOut = "Could not obtain tokens (HTTP " + juce::String (status) + ").";
        return false;
    }

    return adoptTokenResponse (response, generationAtStart, errorOut);
}

bool GrokAuth::submitPastedInput (const juce::String& input)
{
    const juce::ScopedLock sl (lock);

    if (pendingCallback == nullptr)
        return false;

    pendingCallback->submitPastedInput (input);
    return true;
}

bool GrokAuth::refresh (juce::String& errorOut, std::atomic<bool>* shouldStop)
{
    const juce::ScopedLock refreshSl (refreshLock);
    const auto generationAtStart = stateGeneration.load();

    juce::String currentRefreshToken;

    {
        const juce::ScopedLock sl (lock);
        currentRefreshToken = tokens.refreshToken;
    }

    if (currentRefreshToken.isEmpty())
    {
        errorOut = "You need to sign in.";
        return false;
    }

    const auto form = juce::String ("grant_type=refresh_token")
                        + "&refresh_token=" + juce::URL::addEscapeChars (currentRefreshToken, false)
                        + "&client_id=" + juce::URL::addEscapeChars (clientId, false);

    int status = 0;
    const auto response = postForm (juce::String (issuer) + "/oauth2/token", form, status, shouldStop);

    if (shouldStop != nullptr && shouldStop->load())
    {
        errorOut = "Cancelled.";
        return false;
    }

    if (status < 200 || status >= 300)
    {
        errorOut = "Could not refresh the session. Please sign in again.";
        return false;
    }

    return adoptTokenResponse (response, generationAtStart, errorOut);
}
