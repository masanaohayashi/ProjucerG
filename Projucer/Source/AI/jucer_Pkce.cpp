#include "jucer_Pkce.h"

// SHA-256 は juce_core ではなく juce_cryptography にある。
#include <juce_cryptography/juce_cryptography.h>

namespace
{
    /*  URL-safe base64、パディング無し。OAuth のクエリにそのまま載せられる形。 */
    juce::String toBase64Url (const void* data, int numBytes)
    {
        juce::MemoryOutputStream encoded;
        juce::Base64::convertToBase64 (encoded, data, (size_t) numBytes);

        return encoded.toString()
                   .replaceCharacter ('+', '-')
                   .replaceCharacter ('/', '_')
                   .removeCharacters ("=");
    }

    juce::MemoryBlock randomBytes (int numBytes)
    {
        juce::MemoryBlock bytes ((size_t) numBytes);
        auto& random = juce::Random::getSystemRandom();

        for (int i = 0; i < numBytes; ++i)
            bytes[(size_t) i] = (char) (juce::uint8) random.nextInt (256);

        return bytes;
    }
}

PkceCodes generatePkce()
{
    const auto verifierBytes = randomBytes (64);

    PkceCodes codes;
    codes.codeVerifier = toBase64Url (verifierBytes.getData(), (int) verifierBytes.getSize());

    // challenge は検証子の「文字列」の SHA-256。バイト列ではない点に注意。
    const juce::SHA256 hash (codes.codeVerifier.toRawUTF8(),
                             (size_t) codes.codeVerifier.getNumBytesAsUTF8());

    const auto digest = hash.getRawData();
    codes.codeChallenge = toBase64Url (digest.getData(), (int) digest.getSize());

    return codes;
}

juce::String generateOAuthState()
{
    const auto bytes = randomBytes (32);
    return toBase64Url (bytes.getData(), (int) bytes.getSize());
}
