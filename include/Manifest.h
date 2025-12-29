/*
 * Odin4 - Samsung Firmware Flashing Tool for Linux
 * Manifest - SHA256 manifest verification
 */

#ifndef MANIFEST_H
#define MANIFEST_H

#include <string>
#include <map>

namespace Odin {

class Manifest {
public:
    explicit Manifest(const std::string& path);
    ~Manifest();
    
    // Load manifest from file
    bool load();
    
    // Verify a file against manifest
    bool verify(const std::string& filename) const;
    
    // Get expected hash for a file
    std::string getHash(const std::string& filename) const;
    
    // Calculate SHA256 of a file
    static std::string calculateSHA256(const std::string& path);
    static std::string calculateSHA256(const char* data, size_t size);
    
    // Calculate MD5 of a file
    static std::string calculateMD5(const std::string& path);
    static std::string calculateMD5(const char* data, size_t size);
    
private:
    std::string path_;
    std::map<std::string, std::string> hashes_;  // filename -> hash
    bool loaded_;
};

} // namespace Odin

#endif // MANIFEST_H
