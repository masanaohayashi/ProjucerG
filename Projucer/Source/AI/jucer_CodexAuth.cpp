#include "jucer_CodexAuth.h"

#include "jucer_AuthBrowser.h"
#include "jucer_Keychain.h"
#include "jucer_OAuthCallbackServer.h"
#include "jucer_Pkce.h"

namespace
{
    constexpr const char* keychainService = "com.projucer.ai.codex";
    constexpr const char* keychainAccount = "tokens";

    constexpr int connectionTimeoutMs = 30000;
    constexpr int deviceCodeTimeoutMs = 15 * 60 * 1000;

    /*  interval は数値で返ることも文字列で返ることもある。 */
    int readInterval (const juce::var& value)
    {
        if (value.isVoid())
            return 5;

        const auto seconds = value.isString() ? value.toString().getIntValue() : (int) value;

        return juce::jlimit (1, 60, seconds <= 0 ? 5 : seconds);
    }

    /*  中断要求に素早く応じられるよう、待ち時間を細かく刻む。 */
    bool sleepUnlessStopped (int milliseconds, std::atomic<bool>& shouldStop)
    {
        for (int elapsed = 0; elapsed < milliseconds; elapsed += 100)
        {
            if (shouldStop.load())
                return false;

            juce::Thread::sleep (juce::jmin (100, milliseconds - elapsed));
        }

        return ! shouldStop.load();
    }
}

//==============================================================================
/*  実行中の 1 リクエスト。

    juce::URL::createInputStream ではなく WebInputStream を直に持つのは、
    cancel() を呼べるようにするため。これが無いと、応答が来ないまま
    読み取りでブロックしている間、ユーザーの「中止」がまったく効かない。
*/
class CodexAuth::ActiveStream
{
public:
    ActiveStream (const juce::URL& url, const juce::String& extraHeaders)
        : stream (url, true)
    {
        stream.withExtraHeaders (extraHeaders)
              .withConnectionTimeout (connectionTimeoutMs);
    }

    /** @returns 接続できたら true。中断された場合や失敗した場合は false。 */
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

    /** 別スレッドから呼ばれる。読み取り中のソケットを畳んで戻す。 */
    void cancel()
    {
        cancelled = true;
        stream.cancel();
    }

    bool wasCancelled() const noexcept    { return cancelled.load(); }

private:
    juce::WebInputStream stream;
    std::atomic<bool> cancelled { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ActiveStream)
};

//==============================================================================
CodexAuth::CodexAuth()
{
    load();
}

CodexAuth::~CodexAuth()
{
    cancelActiveRequest();
}

void CodexAuth::load()
{
    const auto stored = keychainRead (keychainService, keychainAccount);

    if (stored.isEmpty())
        return;

    const auto parsed = juce::JSON::parse (stored);

    const juce::ScopedLock sl (lock);

    tokens.accessToken  = parsed.getProperty ("access_token", {}).toString();
    tokens.refreshToken = parsed.getProperty ("refresh_token", {}).toString();
    tokens.accountId    = parsed.getProperty ("account_id", {}).toString();
}

bool CodexAuth::save() const
{
    juce::String serialised;

    {
        const juce::ScopedLock sl (lock);

        auto* object = new juce::DynamicObject();
        object->setProperty ("access_token",  tokens.accessToken);
        object->setProperty ("refresh_token", tokens.refreshToken);
        object->setProperty ("account_id",    tokens.accountId);

        serialised = juce::JSON::toString (juce::var (object));
    }

    return keychainWrite (keychainService, keychainAccount, serialised);
}

bool CodexAuth::isSignedIn() const
{
    const juce::ScopedLock sl (lock);
    return tokens.accessToken.isNotEmpty();
}

juce::String CodexAuth::getAccessToken() const
{
    const juce::ScopedLock sl (lock);
    return tokens.accessToken;
}

juce::String CodexAuth::getAccountId() const
{
    const juce::ScopedLock sl (lock);
    return tokens.accountId;
}

void CodexAuth::signOut()
{
    // 世代を進めて、進行中のサインインが後からトークンを書き戻さないようにする。
    ++stateGeneration;

    cancelActiveRequest();

    {
        const juce::ScopedLock sl (lock);
        tokens = {};
    }

    keychainErase (keychainService, keychainAccount);
}

void CodexAuth::cancelActiveRequest()
{
    std::shared_ptr<ActiveStream> stream;

    {
        const juce::ScopedLock sl (lock);
        stream = activeStream;
    }

    // ロックを持ったまま cancel を呼ばない。cancel はソケットを閉じるまで戻らない
    // ことがあり、その間ほかのスレッドが getAccessToken などで止まってしまう。
    if (stream != nullptr)
        stream->cancel();
}

//==============================================================================
juce::String CodexAuth::post (const juce::String& urlString,
                              const juce::String& body,
                              const juce::String& contentType,
                              int& statusOut,
                              std::atomic<bool>* shouldStop)
{
    statusOut = 0;

    if (shouldStop != nullptr && shouldStop->load())
        return {};

    const auto url = juce::URL (urlString).withPOSTData (body);
    auto stream = std::make_shared<ActiveStream> (url, "Content-Type: " + contentType);

    {
        const juce::ScopedLock sl (lock);
        activeStream = stream;
    }

    // activeStream に載せた後にもう一度見る。載せる前に中止が来ていた場合、
    // ここで畳まないと誰にも止められないリクエストが走り出す。
    if (shouldStop != nullptr && shouldStop->load())
        stream->cancel();

    juce::String response;

    if (stream->connect())
    {
        statusOut = stream->getStatusCode();
        response = stream->readAll();
    }

    /*  どのリクエストが何を返したかを残す。トークンや認可コードは送信側の
        body にしか無く、ここで出しているのは URL のパスと応答本文だけ。
        失敗時の本文には理由が書かれている。 */
    DBG ("[AI][auth] POST " << juce::URL (urlString).getSubPath()
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

juce::String CodexAuth::postJson (const juce::String& url,
                                  const juce::String& body,
                                  int& statusOut,
                                  std::atomic<bool>* shouldStop)
{
    return post (url, body, "application/json", statusOut, shouldStop);
}

juce::String CodexAuth::postForm (const juce::String& url,
                                  const juce::String& body,
                                  int& statusOut,
                                  std::atomic<bool>* shouldStop)
{
    return post (url, body, "application/x-www-form-urlencoded", statusOut, shouldStop);
}

//==============================================================================
bool CodexAuth::adoptTokenResponse (const juce::String& response,
                                    std::uint64_t generationAtStart,
                                    juce::String& errorOut)
{
    const auto parsed = juce::JSON::parse (response);

    const auto accessToken  = parsed.getProperty ("access_token", {}).toString();
    const auto refreshToken = parsed.getProperty ("refresh_token", {}).toString();
    const auto accountId    = extractAccountId (parsed.getProperty ("id_token", {}).toString());

    if (accessToken.isEmpty())
    {
        errorOut = "Could not read the token response.";
        return false;
    }

    // 開始後にサインアウトや中止があったなら、この結果は捨てる。
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

        if (accountId.isNotEmpty())
            tokens.accountId = accountId;
    }

    if (! save())
    {
        errorOut = "Could not save the tokens to the keychain.";
        return false;
    }

    return true;
}

//==============================================================================
bool CodexAuth::signInWithBrowser (std::atomic<bool>& shouldStop, juce::String& errorOut)
{
    const auto generationAtStart = stateGeneration.load();

    OAuthCallbackServer server;

    if (! server.start (1455))
    {
        errorOut = "Could not start the local listener for the sign-in callback.";
        return false;
    }

    const auto redirectUri = "http://localhost:" + juce::String (server.getPort()) + "/auth/callback";
    const auto pkce = generatePkce();
    const auto expectedState = generateOAuthState();

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
    add ("scope", "openid profile email offline_access api.connectors.read api.connectors.invoke");
    add ("code_challenge", pkce.codeChallenge);
    add ("code_challenge_method", "S256");
    add ("id_token_add_organizations", "true");
    add ("codex_cli_simplified_flow", "true");
    add ("state", expectedState);
    add ("originator", "codex_cli_rs");

    /*  iPad では ChatGPT アプリ側で承認する経路があり、そのあいだアプリは
        バックグラウンドへ回る。何もしないと待ち受けが止まり、Safari が
        戻ってきたときにリダイレクトを受け取れない。 */
    beginAuthBackgroundTask();

    presentAuthPage (juce::String (issuer) + "/oauth/authorize?" + query);

    juce::String code, returnedState;
    const auto gotCode = server.waitForCode (shouldStop, code, returnedState, errorOut);

    dismissAuthPage();
    endAuthBackgroundTask();
    server.stop();

    if (! gotCode)
        return false;

    // CSRF 対策。送り出した state と一致しない応答は、こちらが始めたサインインの
    // 結果ではないので使わない。
    if (returnedState != expectedState)
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
    const auto response = postForm (juce::String (issuer) + "/oauth/token", form, status, &shouldStop);

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

//==============================================================================
std::optional<CodexAuth::DeviceCode> CodexAuth::requestDeviceCode (juce::String& errorOut,
                                                                   std::atomic<bool>* shouldStop)
{
    auto* body = new juce::DynamicObject();
    body->setProperty ("client_id", clientId);

    int status = 0;
    const auto response = postJson (juce::String (issuer) + "/api/accounts/deviceauth/usercode",
                                    juce::JSON::toString (juce::var (body)),
                                    status,
                                    shouldStop);

    if (shouldStop != nullptr && shouldStop->load())
    {
        errorOut = "Sign-in was cancelled.";
        return {};
    }

    if (status == 404)
    {
        errorOut = "Device code sign-in is not enabled for this account. Enable it in your ChatGPT "
                   "security settings, or sign in with the browser instead.";
        return {};
    }

    if (status < 200 || status >= 300)
    {
        errorOut = "Could not start sign-in (HTTP " + juce::String (status) + ").";
        return {};
    }

    const auto parsed = juce::JSON::parse (response);

    DeviceCode code;
    code.deviceAuthId    = parsed.getProperty ("device_auth_id", {}).toString();
    code.userCode        = parsed.getProperty ("user_code", {}).toString();
    code.intervalSeconds = readInterval (parsed.getProperty ("interval", {}));
    code.verificationUrl = juce::String (issuer) + "/codex/device";

    if (code.deviceAuthId.isEmpty() || code.userCode.isEmpty())
    {
        errorOut = "Could not read the sign-in response.";
        return {};
    }

    return code;
}

bool CodexAuth::pollForTokens (const DeviceCode& code,
                               std::atomic<bool>& shouldStop,
                               juce::String& errorOut)
{
    const auto generationAtStart = stateGeneration.load();
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) deviceCodeTimeoutMs;

    juce::String authorizationCode, codeVerifier;

    while (! shouldStop.load())
    {
        if (juce::Time::getMillisecondCounter() > deadline)
        {
            errorOut = "Sign-in timed out. Please try again.";
            return false;
        }

        auto* body = new juce::DynamicObject();
        body->setProperty ("device_auth_id", code.deviceAuthId);
        body->setProperty ("user_code",      code.userCode);

        int status = 0;
        const auto response = postJson (juce::String (issuer) + "/api/accounts/deviceauth/token",
                                        juce::JSON::toString (juce::var (body)),
                                        status,
                                        &shouldStop);

        if (shouldStop.load())
            break;

        if (status >= 200 && status < 300)
        {
            const auto parsed = juce::JSON::parse (response);
            authorizationCode = parsed.getProperty ("authorization_code", {}).toString();
            codeVerifier      = parsed.getProperty ("code_verifier", {}).toString();

            if (authorizationCode.isNotEmpty())
                break;
        }
        else if (status == 403 || status == 404)
        {
            /*  承認待ち。Codex の poll_for_token と同じ扱い
                (login/src/device_code_auth.rs)。403 の本文は
                deviceauth_authorization_pending で「まだ承認されていないので
                もう一度試せ」を意味する。ここで諦めてはいけない。 */
        }
        else if (status >= 400)
        {
            // 待っても変わらない類の失敗。
            errorOut = "Sign-in failed (HTTP " + juce::String (status) + ").";
            return false;
        }

        if (! sleepUnlessStopped (code.intervalSeconds * 1000, shouldStop))
            break;
    }

    if (shouldStop.load() || authorizationCode.isEmpty())
    {
        if (errorOut.isEmpty())
            errorOut = "Sign-in was cancelled.";

        return false;
    }

    // この経路では PKCE のペアを認可サービスが生成して返すので、そのまま使う。
    const auto form = juce::String ("grant_type=authorization_code")
                        + "&code=" + juce::URL::addEscapeChars (authorizationCode, false)
                        + "&redirect_uri=" + juce::URL::addEscapeChars (juce::String (issuer) + "/deviceauth/callback", false)
                        + "&client_id=" + juce::URL::addEscapeChars (clientId, false)
                        + "&code_verifier=" + juce::URL::addEscapeChars (codeVerifier, false);

    int status = 0;
    const auto response = postForm (juce::String (issuer) + "/oauth/token", form, status, &shouldStop);

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

//==============================================================================
bool CodexAuth::refresh (juce::String& errorOut, std::atomic<bool>* shouldStop)
{
    // 401 が同時に複数返ってきても交換は 1 回で済ませる。
    const juce::ScopedLock refreshSl (refreshLock);

    const auto generationAtStart = stateGeneration.load();

    juce::String currentRefreshToken, accessTokenBefore;

    {
        const juce::ScopedLock sl (lock);
        currentRefreshToken = tokens.refreshToken;
        accessTokenBefore   = tokens.accessToken;
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
    const auto response = postForm (juce::String (issuer) + "/oauth/token", form, status, shouldStop);

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

//==============================================================================
juce::String CodexAuth::extractAccountId (const juce::String& idToken)
{
    /*  JWT は header.payload.signature。真ん中を base64url としてデコードする。

        署名は検証しない。自分で取得したばかりのトークンから自分の ID を読むだけで、
        信頼の判断には使っていないため。第三者から受け取ったトークンを扱うように
        なった場合は、ここに検証が必要になる。
    */
    const auto firstDot = idToken.indexOfChar ('.');

    if (firstDot < 0)
        return {};

    const auto secondDot = idToken.indexOfChar (firstDot + 1, '.');

    if (secondDot < 0)
        return {};

    auto payload = idToken.substring (firstDot + 1, secondDot)
                       .replaceCharacter ('-', '+')
                       .replaceCharacter ('_', '/');

    while (payload.length() % 4 != 0)
        payload << "=";

    juce::MemoryOutputStream decoded;

    if (! juce::Base64::convertFromBase64 (decoded, payload))
        return {};

    const auto parsed = juce::JSON::parse (decoded.toString());

    // account_id はトップレベルにあることも、Codex 用のクレームの下にあることもある。
    auto accountId = parsed.getProperty ("chatgpt_account_id", {}).toString();

    if (accountId.isEmpty())
        accountId = parsed.getProperty ("https://api.openai.com/auth", {})
                        .getProperty ("chatgpt_account_id", {}).toString();

    return accountId;
}
