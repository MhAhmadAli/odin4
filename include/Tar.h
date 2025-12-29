/*
 * Odin4 - Samsung Firmware Flashing Tool for Linux
 * Tar - TAR archive handling
 */

#ifndef TAR_H
#define TAR_H

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace Odin {

struct TarEntry {
    std::string name;
    size_t size;
    size_t offset;      // Offset of data in file
    bool isFile;
    bool isDirectory;
    uint32_t mode;
    uint32_t mtime;
};

class Tar {
public:
    static const std::string TAG;
    
    explicit Tar(const std::string& path);
    ~Tar();
    
    // Open and parse TAR
    bool open();
    void close();
    bool isOpen() const { return isOpen_; }
    
    // Get entries
    const std::vector<TarEntry>& getEntries() const { return entries_; }
    
    // Find entry by name
    const TarEntry* findEntry(const std::string& name) const;
    
    // Read entry data
    bool readEntry(const TarEntry& entry, char* buffer, size_t bufferSize) const;
    
    // Iterate over entries
    using EntryCallback = std::function<bool(const TarEntry&)>;
    void forEach(EntryCallback callback) const;
    
private:
    bool parseHeader(const char* header, TarEntry& entry);
    size_t octalToDecimal(const char* str, size_t len);
    
    std::string path_;
    FILE* file_;
    bool isOpen_;
    std::vector<TarEntry> entries_;
};

} // namespace Odin

#endif // TAR_H
