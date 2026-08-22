#pragma once

#include <juce_core/juce_core.h>

/*  ブラウザ OAuth 用の PKCE。

    デバイスコード経路ではサーバーがペアを生成して返すが、ブラウザ経路では
    クライアントが作る。方式は S256。
*/
struct PkceCodes
{
    juce::String codeVerifier;
    juce::String codeChallenge;
};

/** 64 バイトの乱数から検証子と challenge を作る。 */
PkceCodes generatePkce();

/** CSRF 対策の state。32 バイトの乱数。 */
juce::String generateOAuthState();
