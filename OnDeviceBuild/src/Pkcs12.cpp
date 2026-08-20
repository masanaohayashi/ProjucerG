#include "OnDeviceBuild/Pkcs12.h"

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pkcs12.h>
#include <openssl/provider.h>
#include <openssl/x509.h>

#include <memory>

namespace ondevice {

namespace
{
std::string opensslErrors()
{
    std::string text;
    char buffer[256];

    while (const unsigned long err = ERR_get_error())
    {
        ERR_error_string_n (err, buffer, sizeof (buffer));
        if (! text.empty())
            text += "; ";
        text += buffer;
    }

    return text.empty() ? "unknown OpenSSL error" : text;
}

struct BioCloser { void operator() (BIO* b) const { BIO_free (b); } };
struct Pkcs12Closer { void operator() (PKCS12* p) const { PKCS12_free (p); } };
struct PkeyCloser { void operator() (EVP_PKEY* p) const { EVP_PKEY_free (p); } };
struct X509Closer { void operator() (X509* c) const { X509_free (c); } };

bool parsePkcs12 (PKCS12* p12,
                  const std::string& password,
                  EVP_PKEY** pkey,
                  X509** cert,
                  STACK_OF(X509)** ca)
{
    ERR_clear_error();
    return PKCS12_parse (p12, password.c_str(), pkey, cert, ca) == 1;
}
} // namespace

bool reencodePkcs12Aes (const std::string& inPath,
                        const std::string& outPath,
                        const std::string& password,
                        std::string& error)
{
    error.clear();
    OPENSSL_init_crypto (OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr);

    std::unique_ptr<BIO, BioCloser> in (BIO_new_file (inPath.c_str(), "rb"));

    if (! in)
    {
        error = "cannot open PKCS#12: " + inPath + " (" + opensslErrors() + ")";
        return false;
    }

    std::unique_ptr<PKCS12, Pkcs12Closer> parsed (d2i_PKCS12_bio (in.get(), nullptr));

    if (! parsed)
    {
        error = "d2i_PKCS12 failed: " + opensslErrors();
        return false;
    }

    EVP_PKEY* rawKey = nullptr;
    X509* rawCert = nullptr;
    STACK_OF(X509)* rawCa = nullptr;

    if (! parsePkcs12 (parsed.get(), password, &rawKey, &rawCert, &rawCa))
    {
        // Keychain exports use RC2-40-CBC, which lives in the legacy provider.
        OSSL_PROVIDER_load (nullptr, "legacy");
        OSSL_PROVIDER_load (nullptr, "default");
        ERR_clear_error();

        if (! parsePkcs12 (parsed.get(), password, &rawKey, &rawCert, &rawCa))
        {
            error = "PKCS12_parse failed: " + opensslErrors();
            return false;
        }
    }

    std::unique_ptr<EVP_PKEY, PkeyCloser> pkey (rawKey);
    std::unique_ptr<X509, X509Closer> cert (rawCert);

    if (! pkey || ! cert)
    {
        if (rawCa != nullptr)
            sk_X509_pop_free (rawCa, X509_free);
        error = "PKCS#12 did not contain both a private key and a certificate";
        return false;
    }

    std::unique_ptr<PKCS12, Pkcs12Closer> modern (
        PKCS12_create (password.c_str(),
                       "ondevice",
                       pkey.get(),
                       cert.get(),
                       rawCa,
                       NID_aes_256_cbc,
                       NID_aes_256_cbc,
                       2048,
                       -1,
                       0));

    if (rawCa != nullptr)
        sk_X509_pop_free (rawCa, X509_free);

    if (! modern)
    {
        error = "PKCS12_create (AES-256-CBC) failed: " + opensslErrors();
        return false;
    }

    if (PKCS12_set_mac (modern.get(), password.c_str(), -1, nullptr, 0, 2048, EVP_sha256()) != 1)
    {
        error = "PKCS12_set_mac (SHA-256) failed: " + opensslErrors();
        return false;
    }

    std::unique_ptr<BIO, BioCloser> out (BIO_new_file (outPath.c_str(), "wb"));

    if (! out)
    {
        error = "cannot write PKCS#12: " + outPath + " (" + opensslErrors() + ")";
        return false;
    }

    if (i2d_PKCS12_bio (out.get(), modern.get()) != 1)
    {
        error = "i2d_PKCS12_bio failed: " + opensslErrors();
        return false;
    }

    return true;
}

} // namespace ondevice
