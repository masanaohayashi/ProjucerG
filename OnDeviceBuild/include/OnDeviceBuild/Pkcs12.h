#pragma once

#include <string>

namespace ondevice {

/** Rewrite a PKCS#12 so key and cert bags use AES-256-CBC (and SHA-256 MAC).

    Input that is already AES still produces an AES output and counts as success.
    Uses OpenSSL APIs; does not shell out. */
bool reencodePkcs12Aes (const std::string& inPath,
                        const std::string& outPath,
                        const std::string& password,
                        std::string& error);

} // namespace ondevice
