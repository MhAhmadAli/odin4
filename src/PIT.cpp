/*
 * Odin4 - Samsung Firmware Flashing Tool for Linux
 * PIT - Samsung Partition Information Table implementation
 */

#include "PIT.h"
#include "Log.h"
#include <cstring>
#include <algorithm>

namespace Odin {

// PIT file structure
struct PITHeader {
    uint32_t magic;           // 0x12349876
    uint32_t entryCount;
    char gangName[8];         // "COM_TAR2" usually
    char projectName[8];
    uint32_t reserved[2];     // Usually 0
};

static_assert(sizeof(PITHeader) == 28, "PITHeader size mismatch");

struct PITRawEntry {
    uint32_t binaryType;
    uint32_t deviceType;
    uint32_t partitionId;
    uint32_t attributes;
    uint32_t updateAttributes;
    uint32_t blockSizeOrOffset;
    uint32_t blockCount;
    uint32_t fileOffset;
    uint32_t fileSize;
    char partitionName[32];
    char flashFilename[32];
    char fotaFilename[32];
};

static_assert(sizeof(PITRawEntry) == 132, "PITRawEntry size mismatch");

PIT::PIT()
    : headerCount_(0)
{
}

PIT::~PIT() {
}

bool PIT::parse(const char* data, size_t size) {
    if (size < sizeof(PITHeader)) {
        return false;
    }
    
    const PITHeader* header = reinterpret_cast<const PITHeader*>(data);
    
    // Verify magic
    if (header->magic != PIT_MAGIC) {
        return false;
    }
    
    headerCount_ = header->entryCount;
    gangName_ = std::string(header->gangName, strnlen(header->gangName, 8));
    projectName_ = std::string(header->projectName, strnlen(header->projectName, 8));
    
    // Parse entries
    entries_.clear();
    const char* entryPtr = data + sizeof(PITHeader);
    
    for (uint32_t i = 0; i < headerCount_; i++) {
        if (entryPtr + sizeof(PITRawEntry) > data + size) {
            break;
        }
        
        const PITRawEntry* rawEntry = reinterpret_cast<const PITRawEntry*>(entryPtr);
        
        PITEntry entry;
        entry.binaryType = static_cast<PITBinaryType>(rawEntry->binaryType);
        entry.deviceType = static_cast<PITDeviceType>(rawEntry->deviceType);
        entry.partitionId = rawEntry->partitionId;
        entry.attributes = rawEntry->attributes;
        entry.updateAttributes = rawEntry->updateAttributes;
        entry.blockSizeOrOffset = rawEntry->blockSizeOrOffset;
        entry.blockCount = rawEntry->blockCount;
        entry.fileOffset = rawEntry->fileOffset;
        entry.fileSize = rawEntry->fileSize;
        entry.partitionName = std::string(rawEntry->partitionName, 
                                          strnlen(rawEntry->partitionName, 32));
        entry.flashFilename = std::string(rawEntry->flashFilename,
                                          strnlen(rawEntry->flashFilename, 32));
        entry.fotaFilename = std::string(rawEntry->fotaFilename,
                                         strnlen(rawEntry->fotaFilename, 32));
        
        entries_.push_back(entry);
        entryPtr += sizeof(PITRawEntry);
    }
    
    return true;
}

const PITEntry* PIT::findEntry(const std::string& partitionName) const {
    for (const auto& entry : entries_) {
        if (entry.partitionName == partitionName) {
            return &entry;
        }
    }
    return nullptr;
}

const PITEntry* PIT::findEntryByFilename(const std::string& filename) const {
    for (const auto& entry : entries_) {
        if (entry.flashFilename == filename) {
            return &entry;
        }
        
        // Also check FOTA filename
        if (entry.fotaFilename == filename) {
            return &entry;
        }
    }
    return nullptr;
}

std::vector<char> PIT::serialize() const {
    size_t totalSize = sizeof(PITHeader) + entries_.size() * sizeof(PITRawEntry);
    std::vector<char> buffer(totalSize, 0);
    
    // Write header
    PITHeader* header = reinterpret_cast<PITHeader*>(buffer.data());
    header->magic = PIT_MAGIC;
    header->entryCount = static_cast<uint32_t>(entries_.size());
    strncpy(header->gangName, gangName_.c_str(), 8);
    strncpy(header->projectName, projectName_.c_str(), 8);
    
    // Write entries
    char* entryPtr = buffer.data() + sizeof(PITHeader);
    
    for (const auto& entry : entries_) {
        PITRawEntry* rawEntry = reinterpret_cast<PITRawEntry*>(entryPtr);
        
        rawEntry->binaryType = static_cast<uint32_t>(entry.binaryType);
        rawEntry->deviceType = static_cast<uint32_t>(entry.deviceType);
        rawEntry->partitionId = entry.partitionId;
        rawEntry->attributes = entry.attributes;
        rawEntry->updateAttributes = entry.updateAttributes;
        rawEntry->blockSizeOrOffset = entry.blockSizeOrOffset;
        rawEntry->blockCount = entry.blockCount;
        rawEntry->fileOffset = entry.fileOffset;
        rawEntry->fileSize = entry.fileSize;
        strncpy(rawEntry->partitionName, entry.partitionName.c_str(), 32);
        strncpy(rawEntry->flashFilename, entry.flashFilename.c_str(), 32);
        strncpy(rawEntry->fotaFilename, entry.fotaFilename.c_str(), 32);
        
        entryPtr += sizeof(PITRawEntry);
    }
    
    return buffer;
}

void PIT::print() const {
    Log::print("PIT", "Gang: " + gangName_ + ", Project: " + projectName_);
    Log::print("PIT", "Entries: " + std::to_string(entries_.size()));
    
    for (size_t i = 0; i < entries_.size(); i++) {
        const auto& e = entries_[i];
        Log::print("PIT", "  [" + std::to_string(i) + "] " + e.partitionName + 
                   " -> " + e.flashFilename +
                   " (ID=" + std::to_string(e.partitionId) + 
                   ", Size=" + std::to_string(e.blockCount) + " blocks)");
    }
}

} // namespace Odin
