/*
 * Odin4 - Samsung Firmware Flashing Tool for Linux
 * Manifest - Hash verification implementation
 */

#include "Manifest.h"
#include "Log.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>

// Crypto++ headers
#ifdef HAVE_CRYPTOPP
#include <cryptopp/sha.h>
#include <cryptopp/md5.h>
#include <cryptopp/hex.h>
#include <cryptopp/files.h>
#else
// Fallback to OpenSSL. The one-shot MD5_*/SHA256_* functions were deprecated
// in OpenSSL 3.0, so go through the EVP interface instead.
#include <openssl/evp.h>
#endif

namespace Odin {

namespace {

constexpr size_t kReadBufferSize = 65536;

std::string toHex(const unsigned char* digest, size_t length) {
    std::stringstream ss;
    for (size_t i = 0; i < length; i++) {
        ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(digest[i]);
    }
    return ss.str();
}

#ifdef HAVE_CRYPTOPP

// Hash at most maxBytes of a file (0 means all of it).
template <typename Hash>
std::string hashFile(const std::string& path, size_t maxBytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    Hash hash;
    std::vector<char> buffer(kReadBufferSize);
    size_t remaining = maxBytes;

    while (file) {
        size_t want = buffer.size();
        if (maxBytes != 0) {
            if (remaining == 0) {
                break;
            }
            want = std::min(want, remaining);
        }

        file.read(buffer.data(), static_cast<std::streamsize>(want));
        size_t bytesRead = static_cast<size_t>(file.gcount());
        if (bytesRead == 0) {
            break;
        }

        hash.Update(reinterpret_cast<const CryptoPP::byte*>(buffer.data()), bytesRead);
        if (maxBytes != 0) {
            remaining -= bytesRead;
        }
    }

    CryptoPP::byte digest[Hash::DIGESTSIZE];
    hash.Final(digest);
    return toHex(digest, Hash::DIGESTSIZE);
}

template <typename Hash>
std::string hashBuffer(const char* data, size_t size) {
    if (!data && size > 0) {
        return "";
    }

    Hash hash;
    hash.Update(reinterpret_cast<const CryptoPP::byte*>(data), size);

    CryptoPP::byte digest[Hash::DIGESTSIZE];
    hash.Final(digest);
    return toHex(digest, Hash::DIGESTSIZE);
}

#else

// RAII wrapper so an early return cannot leak the context.
class DigestContext {
public:
    explicit DigestContext(const EVP_MD* md) : ctx_(EVP_MD_CTX_new()) {
        if (ctx_ && EVP_DigestInit_ex(ctx_, md, nullptr) != 1) {
            EVP_MD_CTX_free(ctx_);
            ctx_ = nullptr;
        }
    }

    ~DigestContext() {
        if (ctx_) {
            EVP_MD_CTX_free(ctx_);
        }
    }

    DigestContext(const DigestContext&) = delete;
    DigestContext& operator=(const DigestContext&) = delete;

    bool valid() const { return ctx_ != nullptr; }

    bool update(const void* data, size_t size) {
        return ctx_ && EVP_DigestUpdate(ctx_, data, size) == 1;
    }

    std::string final() {
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int length = 0;
        if (!ctx_ || EVP_DigestFinal_ex(ctx_, digest, &length) != 1) {
            return "";
        }
        return toHex(digest, length);
    }

private:
    EVP_MD_CTX* ctx_;
};

std::string hashFile(const EVP_MD* md, const std::string& path, size_t maxBytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    DigestContext ctx(md);
    if (!ctx.valid()) {
        return "";
    }

    std::vector<char> buffer(kReadBufferSize);
    size_t remaining = maxBytes;

    while (file) {
        size_t want = buffer.size();
        if (maxBytes != 0) {
            if (remaining == 0) {
                break;
            }
            want = std::min(want, remaining);
        }

        file.read(buffer.data(), static_cast<std::streamsize>(want));
        size_t bytesRead = static_cast<size_t>(file.gcount());
        if (bytesRead == 0) {
            break;
        }

        if (!ctx.update(buffer.data(), bytesRead)) {
            return "";
        }
        if (maxBytes != 0) {
            remaining -= bytesRead;
        }
    }

    return ctx.final();
}

std::string hashBuffer(const EVP_MD* md, const char* data, size_t size) {
    if (!data && size > 0) {
        return "";
    }

    DigestContext ctx(md);
    if (!ctx.valid() || !ctx.update(data, size)) {
        return "";
    }

    return ctx.final();
}

#endif

}  // namespace

Manifest::Manifest(const std::string& path)
    : path_(path)
    , loaded_(false)
{
}

Manifest::~Manifest() {
}

bool Manifest::load() {
    std::ifstream file(path_);
    if (!file.is_open()) {
        return false;
    }

    hashes_.clear();

    std::string line;
    while (std::getline(file, line)) {
        // Tolerate CRLF manifests
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Format: hash filename or hash *filename (binary mode indicator)
        size_t spacePos = line.find(' ');
        if (spacePos == std::string::npos) {
            continue;
        }

        std::string hash = line.substr(0, spacePos);
        std::string filename = line.substr(spacePos + 1);

        // Remove the extra separator and binary mode indicator if present.
        // md5sum/sha256sum write two spaces, so strip any run of them.
        size_t start = filename.find_first_not_of(" \t");
        if (start == std::string::npos) {
            continue;
        }
        filename = filename.substr(start);

        if (filename[0] == '*') {
            filename = filename.substr(1);
        }

        if (filename.empty()) {
            continue;
        }

        hashes_[filename] = hash;
    }

    loaded_ = true;
    return true;
}

bool Manifest::verify(const std::string& filename) const {
    auto it = hashes_.find(filename);
    if (it == hashes_.end()) {
        return false;
    }

    std::string actual = calculateSHA256(path_.substr(0, path_.find_last_of('/') + 1) + filename);
    if (actual.empty()) {
        return false;
    }

    // Case-insensitive comparison
    std::string expected = it->second;
    std::transform(expected.begin(), expected.end(), expected.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    std::transform(actual.begin(), actual.end(), actual.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    return actual == expected;
}

std::string Manifest::getHash(const std::string& filename) const {
    auto it = hashes_.find(filename);
    if (it != hashes_.end()) {
        return it->second;
    }
    return "";
}

std::string Manifest::calculateSHA256(const std::string& path, size_t maxBytes) {
#ifdef HAVE_CRYPTOPP
    return hashFile<CryptoPP::SHA256>(path, maxBytes);
#else
    return hashFile(EVP_sha256(), path, maxBytes);
#endif
}

std::string Manifest::calculateSHA256(const char* data, size_t size) {
#ifdef HAVE_CRYPTOPP
    return hashBuffer<CryptoPP::SHA256>(data, size);
#else
    return hashBuffer(EVP_sha256(), data, size);
#endif
}

std::string Manifest::calculateMD5(const std::string& path, size_t maxBytes) {
#ifdef HAVE_CRYPTOPP
    return hashFile<CryptoPP::MD5>(path, maxBytes);
#else
    return hashFile(EVP_md5(), path, maxBytes);
#endif
}

std::string Manifest::calculateMD5(const char* data, size_t size) {
#ifdef HAVE_CRYPTOPP
    return hashBuffer<CryptoPP::MD5>(data, size);
#else
    return hashBuffer(EVP_md5(), data, size);
#endif
}

} // namespace Odin
