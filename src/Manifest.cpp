/*
 * Odin4 - Samsung Firmware Flashing Tool for Linux
 * Manifest - Hash verification implementation
 */

#include "Manifest.h"
#include "Log.h"
#include <fstream>
#include <sstream>
#include <iomanip>

// Crypto++ headers
#ifdef HAVE_CRYPTOPP
#include <cryptopp/sha.h>
#include <cryptopp/md5.h>
#include <cryptopp/hex.h>
#include <cryptopp/files.h>
#else
// Fallback to OpenSSL-style implementation
#include <openssl/sha.h>
#include <openssl/md5.h>
#endif

namespace Odin {

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
    
    std::string line;
    while (std::getline(file, line)) {
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
        
        // Remove binary mode indicator if present
        if (!filename.empty() && (filename[0] == '*' || filename[0] == ' ')) {
            filename = filename.substr(1);
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
    
    // Case-insensitive comparison
    std::string expected = it->second;
    std::transform(expected.begin(), expected.end(), expected.begin(), ::tolower);
    std::transform(actual.begin(), actual.end(), actual.begin(), ::tolower);
    
    return actual == expected;
}

std::string Manifest::getHash(const std::string& filename) const {
    auto it = hashes_.find(filename);
    if (it != hashes_.end()) {
        return it->second;
    }
    return "";
}

std::string Manifest::calculateSHA256(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }
    
#ifdef HAVE_CRYPTOPP
    CryptoPP::SHA256 hash;
    char buffer[65536];
    
    while (file) {
        file.read(buffer, sizeof(buffer));
        size_t bytesRead = file.gcount();
        if (bytesRead > 0) {
            hash.Update(reinterpret_cast<const CryptoPP::byte*>(buffer), bytesRead);
        }
    }
    
    CryptoPP::byte digest[CryptoPP::SHA256::DIGESTSIZE];
    hash.Final(digest);
    
    std::stringstream ss;
    for (size_t i = 0; i < CryptoPP::SHA256::DIGESTSIZE; i++) {
        ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(digest[i]);
    }
    
    return ss.str();
#else
    // OpenSSL fallback
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    
    char buffer[65536];
    while (file) {
        file.read(buffer, sizeof(buffer));
        size_t bytesRead = file.gcount();
        if (bytesRead > 0) {
            SHA256_Update(&sha256, buffer, bytesRead);
        }
    }
    
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256_Final(digest, &sha256);
    
    std::stringstream ss;
    for (size_t i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(digest[i]);
    }
    
    return ss.str();
#endif
}

std::string Manifest::calculateSHA256(const char* data, size_t size) {
#ifdef HAVE_CRYPTOPP
    CryptoPP::SHA256 hash;
    hash.Update(reinterpret_cast<const CryptoPP::byte*>(data), size);
    
    CryptoPP::byte digest[CryptoPP::SHA256::DIGESTSIZE];
    hash.Final(digest);
    
    std::stringstream ss;
    for (size_t i = 0; i < CryptoPP::SHA256::DIGESTSIZE; i++) {
        ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(digest[i]);
    }
    
    return ss.str();
#else
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data, size);
    
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256_Final(digest, &sha256);
    
    std::stringstream ss;
    for (size_t i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(digest[i]);
    }
    
    return ss.str();
#endif
}

std::string Manifest::calculateMD5(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }
    
#ifdef HAVE_CRYPTOPP
    CryptoPP::MD5 hash;
    char buffer[65536];
    
    while (file) {
        file.read(buffer, sizeof(buffer));
        size_t bytesRead = file.gcount();
        if (bytesRead > 0) {
            hash.Update(reinterpret_cast<const CryptoPP::byte*>(buffer), bytesRead);
        }
    }
    
    CryptoPP::byte digest[CryptoPP::MD5::DIGESTSIZE];
    hash.Final(digest);
    
    std::stringstream ss;
    for (size_t i = 0; i < CryptoPP::MD5::DIGESTSIZE; i++) {
        ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(digest[i]);
    }
    
    return ss.str();
#else
    MD5_CTX md5;
    MD5_Init(&md5);
    
    char buffer[65536];
    while (file) {
        file.read(buffer, sizeof(buffer));
        size_t bytesRead = file.gcount();
        if (bytesRead > 0) {
            MD5_Update(&md5, buffer, bytesRead);
        }
    }
    
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5_Final(digest, &md5);
    
    std::stringstream ss;
    for (size_t i = 0; i < MD5_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(digest[i]);
    }
    
    return ss.str();
#endif
}

std::string Manifest::calculateMD5(const char* data, size_t size) {
#ifdef HAVE_CRYPTOPP
    CryptoPP::MD5 hash;
    hash.Update(reinterpret_cast<const CryptoPP::byte*>(data), size);
    
    CryptoPP::byte digest[CryptoPP::MD5::DIGESTSIZE];
    hash.Final(digest);
    
    std::stringstream ss;
    for (size_t i = 0; i < CryptoPP::MD5::DIGESTSIZE; i++) {
        ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(digest[i]);
    }
    
    return ss.str();
#else
    MD5_CTX md5;
    MD5_Init(&md5);
    MD5_Update(&md5, data, size);
    
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5_Final(digest, &md5);
    
    std::stringstream ss;
    for (size_t i = 0; i < MD5_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(digest[i]);
    }
    
    return ss.str();
#endif
}

} // namespace Odin
