/*
 * Odin4 - Samsung Firmware Flashing Tool for Linux
 * Tar - TAR archive handling implementation
 */

#include "Tar.h"
#include "Log.h"
#include <cstring>
#include <cstdio>

namespace Odin {

const std::string Tar::TAG = "Tar";

// TAR header structure (POSIX ustar format)
struct TarHeader {
    char name[100];       // File name
    char mode[8];         // File mode
    char uid[8];          // User ID
    char gid[8];          // Group ID
    char size[12];        // File size (octal)
    char mtime[12];       // Modification time (octal)
    char checksum[8];     // Header checksum
    char typeflag;        // Type flag
    char linkname[100];   // Link name
    char magic[6];        // USTAR magic
    char version[2];      // USTAR version
    char uname[32];       // User name
    char gname[32];       // Group name
    char devmajor[8];     // Device major number
    char devminor[8];     // Device minor number
    char prefix[155];     // Filename prefix
    char padding[12];     // Padding to 512 bytes
};

static_assert(sizeof(TarHeader) == 512, "TarHeader must be 512 bytes");

Tar::Tar(const std::string& path)
    : path_(path)
    , file_(nullptr)
    , isOpen_(false)
{
}

Tar::~Tar() {
    close();
}

bool Tar::open() {
    if (isOpen_) {
        return true;
    }
    
    file_ = fopen(path_.c_str(), "rb");
    if (!file_) {
        Log::error(TAG, "Failed to open: " + path_);
        return false;
    }
    
    isOpen_ = true;
    entries_.clear();
    
    // Parse TAR entries
    TarHeader header;
    size_t currentOffset = 0;
    
    while (fread(&header, sizeof(header), 1, file_) == 1) {
        // Check for end of archive (two zero blocks)
        bool isEmpty = true;
        for (size_t i = 0; i < sizeof(header); i++) {
            if (reinterpret_cast<char*>(&header)[i] != 0) {
                isEmpty = false;
                break;
            }
        }
        
        if (isEmpty) {
            break;
        }
        
        TarEntry entry;
        if (!parseHeader(reinterpret_cast<char*>(&header), entry)) {
            Log::error(TAG, "Failed to parse TAR header");
            break;
        }
        
        // Record the data offset (after this header)
        entry.offset = currentOffset + sizeof(TarHeader);
        
        // Add to entries list
        if (entry.isFile && entry.size > 0) {
            entries_.push_back(entry);
        }
        
        // Skip to next header (align to 512 bytes)
        size_t dataBlocks = (entry.size + 511) / 512;
        size_t skipBytes = dataBlocks * 512;
        
        fseek(file_, static_cast<long>(skipBytes), SEEK_CUR);
        currentOffset += sizeof(TarHeader) + skipBytes;
    }
    
    Log::info(TAG, "Parsed " + std::to_string(entries_.size()) + " entries");
    return true;
}

void Tar::close() {
    if (file_) {
        fclose(file_);
        file_ = nullptr;
    }
    isOpen_ = false;
}

bool Tar::parseHeader(const char* data, TarEntry& entry) {
    const TarHeader* header = reinterpret_cast<const TarHeader*>(data);
    
    // Check for USTAR magic
    if (memcmp(header->magic, "ustar", 5) != 0) {
        // Try old tar format
        if (header->name[0] == '\0') {
            return false;
        }
    }
    
    // Get name (may include prefix)
    if (header->prefix[0] != '\0') {
        entry.name = std::string(header->prefix, strnlen(header->prefix, 155));
        entry.name += "/";
        entry.name += std::string(header->name, strnlen(header->name, 100));
    } else {
        entry.name = std::string(header->name, strnlen(header->name, 100));
    }
    
    // Get size (octal)
    entry.size = octalToDecimal(header->size, 12);
    
    // Get mode
    entry.mode = static_cast<uint32_t>(octalToDecimal(header->mode, 8));
    
    // Get mtime
    entry.mtime = static_cast<uint32_t>(octalToDecimal(header->mtime, 12));
    
    // Determine type
    entry.isFile = false;
    entry.isDirectory = false;
    
    switch (header->typeflag) {
        case '\0':
        case '0':
            entry.isFile = true;
            break;
        case '5':
            entry.isDirectory = true;
            break;
        default:
            // Other types (links, special files, etc.)
            break;
    }
    
    return true;
}

size_t Tar::octalToDecimal(const char* str, size_t len) {
    size_t result = 0;
    
    for (size_t i = 0; i < len && str[i] != '\0' && str[i] != ' '; i++) {
        if (str[i] >= '0' && str[i] <= '7') {
            result = (result << 3) + (str[i] - '0');
        }
    }
    
    return result;
}

const TarEntry* Tar::findEntry(const std::string& name) const {
    for (const auto& entry : entries_) {
        if (entry.name == name) {
            return &entry;
        }
        
        // Also try matching just the filename
        size_t lastSlash = entry.name.find_last_of('/');
        if (lastSlash != std::string::npos) {
            if (entry.name.substr(lastSlash + 1) == name) {
                return &entry;
            }
        }
    }
    
    return nullptr;
}

bool Tar::readEntry(const TarEntry& entry, char* buffer, size_t bufferSize) const {
    if (!file_ || !isOpen_) {
        return false;
    }
    
    if (bufferSize < entry.size) {
        Log::error(TAG, "Buffer too small");
        return false;
    }
    
    // Seek to data offset
    if (fseek(file_, static_cast<long>(entry.offset), SEEK_SET) != 0) {
        Log::error(TAG, "Seek failed");
        return false;
    }
    
    // Read data
    size_t bytesRead = fread(buffer, 1, entry.size, file_);
    
    if (bytesRead != entry.size) {
        Log::error(TAG, "Read failed: " + std::to_string(bytesRead) + "/" + 
                   std::to_string(entry.size));
        return false;
    }
    
    return true;
}

void Tar::forEach(EntryCallback callback) const {
    for (const auto& entry : entries_) {
        if (!callback(entry)) {
            break;
        }
    }
}

} // namespace Odin
