/*
 * Odin4 - Samsung Firmware Flashing Tool for Linux
 * PIT - Samsung Partition Information Table
 * 
 * Based on libpit from Heimdall (MIT License)
 */

#ifndef PIT_H
#define PIT_H

#include <string>
#include <vector>
#include <cstdint>

namespace Odin {

// PIT constants
constexpr uint32_t PIT_MAGIC = 0x12349876;
constexpr int PIT_ENTRY_SIZE = 132;
constexpr int PIT_PARTITION_NAME_LEN = 32;
constexpr int PIT_FLASH_FILENAME_LEN = 32;
constexpr int PIT_FOTA_FILENAME_LEN = 32;

// Partition binary type
enum class PITBinaryType : uint32_t {
    AP = 0,
    CP = 1
};

// Partition device type
enum class PITDeviceType : uint32_t {
    OneNAND = 0,
    NAND = 1,
    MMC = 2,
    All = 3,
    UFS = 4
};

// Partition attributes
enum class PITAttribute : uint32_t {
    Write = 0x01,
    STL = 0x02,
    BML = 0x04
};

struct PITEntry {
    PITBinaryType binaryType;
    PITDeviceType deviceType;
    uint32_t partitionId;
    uint32_t attributes;
    uint32_t updateAttributes;
    uint32_t blockSizeOrOffset;
    uint32_t blockCount;
    uint32_t fileOffset;
    uint32_t fileSize;
    std::string partitionName;
    std::string flashFilename;
    std::string fotaFilename;
    
    PITEntry()
        : binaryType(PITBinaryType::AP)
        , deviceType(PITDeviceType::MMC)
        , partitionId(0)
        , attributes(0)
        , updateAttributes(0)
        , blockSizeOrOffset(0)
        , blockCount(0)
        , fileOffset(0)
        , fileSize(0)
    {}
};

class PIT {
public:
    PIT();
    ~PIT();
    
    // Parse PIT data
    bool parse(const char* data, size_t size);
    
    // Get entries
    const std::vector<PITEntry>& getEntries() const { return entries_; }
    size_t getEntryCount() const { return entries_.size(); }
    
    // Find entry by partition name
    const PITEntry* findEntry(const std::string& partitionName) const;
    
    // Find entry by flash filename
    const PITEntry* findEntryByFilename(const std::string& filename) const;
    
    // Serialize to binary
    std::vector<char> serialize() const;
    
    // Debug output
    void print() const;
    
private:
    std::vector<PITEntry> entries_;
    uint32_t headerCount_;
    std::string gangName_;
    std::string projectName_;
};

} // namespace Odin

#endif // PIT_H
