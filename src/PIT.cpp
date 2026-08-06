/*
 * Odin4 - Samsung Firmware Flashing Tool for Linux
 * PIT - Samsung Partition Information Table implementation
 */

#include "PIT.h"
#include "Log.h"
#include <cstring>
#include <algorithm>

namespace Odin {

// PIT file structure - must be packed to match binary format
//
// The header is 28 bytes, matching libpit from Heimdall (PitData::kHeaderDataSize).
// It was previously declared as 24 bytes, which made every entry below it be
// read four bytes too early and garbled the whole table.
#pragma pack(push, 1)
struct PITHeader {
    uint32_t magic;           // 0x12349876
    uint32_t entryCount;
    uint32_t reserved1;
    uint32_t reserved2;
    uint16_t reserved3;
    uint16_t reserved4;
    uint16_t reserved5;
    uint16_t reserved6;
    uint16_t reserved7;
    uint16_t reserved8;
    // Total: 4 + 4 + 4 + 4 + 6*2 = 28 bytes
};

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
    // Total: 9*4 + 32*3 = 36 + 96 = 132 bytes
};
#pragma pack(pop)

static_assert(sizeof(PITHeader) == 28, "PITHeader size mismatch");
static_assert(sizeof(PITRawEntry) == PIT_ENTRY_SIZE, "PITRawEntry size mismatch");

PIT::PIT()
    : headerCount_(0)
{
}

PIT::~PIT() {
}

bool PIT::parse(const char* data, size_t size) {
    entries_.clear();
    headerCount_ = 0;

    if (!data || size < sizeof(PITHeader)) {
        return false;
    }

    // The buffer comes straight off the wire and carries no alignment
    // guarantees, so copy the header out instead of casting over it.
    PITHeader header;
    std::memcpy(&header, data, sizeof(header));

    // Verify magic
    if (header.magic != PIT_MAGIC) {
        return false;
    }

    headerCount_ = header.entryCount;

    // Bound the entry count by what the buffer can actually hold; the count is
    // attacker/device controlled and must not drive the loop on its own.
    size_t available = (size - sizeof(PITHeader)) / sizeof(PITRawEntry);
    if (headerCount_ > available) {
        Log::error("PIT", "PIT claims " + std::to_string(headerCount_) +
                   " entries but only " + std::to_string(available) + " fit");
        headerCount_ = static_cast<uint32_t>(available);
    }

    // Parse entries
    entries_.reserve(headerCount_);
    const char* entryPtr = data + sizeof(PITHeader);

    for (uint32_t i = 0; i < headerCount_; i++) {
        PITRawEntry raw;
        std::memcpy(&raw, entryPtr, sizeof(raw));
        const PITRawEntry* rawEntry = &raw;

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

// Copy at most fieldSize bytes of a name into a fixed-width, zero-padded field.
// strncpy would leave the field unterminated on an exact fit and reads past the
// std::string when it is shorter, so do the copy explicitly.
static void writeField(char* field, size_t fieldSize, const std::string& value) {
    std::memset(field, 0, fieldSize);
    std::memcpy(field, value.data(), std::min(value.size(), fieldSize));
}

std::vector<char> PIT::serialize() const {
    size_t totalSize = sizeof(PITHeader) + entries_.size() * sizeof(PITRawEntry);
    std::vector<char> buffer(totalSize, 0);

    // Write header
    PITHeader header;
    std::memset(&header, 0, sizeof(header));
    header.magic = PIT_MAGIC;
    header.entryCount = static_cast<uint32_t>(entries_.size());
    std::memcpy(buffer.data(), &header, sizeof(header));

    // Write entries
    char* entryPtr = buffer.data() + sizeof(PITHeader);

    for (const auto& entry : entries_) {
        PITRawEntry raw;
        std::memset(&raw, 0, sizeof(raw));

        raw.binaryType = static_cast<uint32_t>(entry.binaryType);
        raw.deviceType = static_cast<uint32_t>(entry.deviceType);
        raw.partitionId = entry.partitionId;
        raw.attributes = entry.attributes;
        raw.updateAttributes = entry.updateAttributes;
        raw.blockSizeOrOffset = entry.blockSizeOrOffset;
        raw.blockCount = entry.blockCount;
        raw.fileOffset = entry.fileOffset;
        raw.fileSize = entry.fileSize;
        writeField(raw.partitionName, sizeof(raw.partitionName), entry.partitionName);
        writeField(raw.flashFilename, sizeof(raw.flashFilename), entry.flashFilename);
        writeField(raw.fotaFilename, sizeof(raw.fotaFilename), entry.fotaFilename);

        std::memcpy(entryPtr, &raw, sizeof(raw));
        entryPtr += sizeof(PITRawEntry);
    }

    return buffer;
}

void PIT::print() const {
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
