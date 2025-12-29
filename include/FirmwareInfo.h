/*
 * Odin4 - Samsung Firmware Flashing Tool for Linux
 * FirmwareInfo - Firmware file information structure
 */

#ifndef FIRMWARE_INFO_H
#define FIRMWARE_INFO_H

#include <string>
#include <memory>
#include <cstdint>

namespace Odin {

// Firmware file types (from decompiled code)
enum class FirmwareType {
    Unknown = 0,
    Bootloader = 1,  // BL
    AP = 2,          // Application Processor (Android)
    CP = 3,          // Communication Processor (Modem)
    CSC = 4,         // Consumer Software Customization
    UMS = 5,         // USB Mass Storage
    PIT = 6          // Partition Information Table
};

// Compression types
enum class CompressionType {
    None = 0,
    LZ4 = 1,
    GZIP = 2
};

struct FirmwareInfo {
    std::string filename;           // Original filename
    std::string partitionName;      // Target partition name
    FirmwareType type;
    
    size_t offset;                  // Offset in archive (for TAR)
    size_t size;                    // Compressed size
    size_t uncompressedSize;        // Uncompressed size (if applicable)
    
    CompressionType compression;
    
    std::shared_ptr<char[]> data;   // File data in memory
    
    // LZ4 frame header info
    uint32_t lz4BlockSizeId;
    bool lz4ContentChecksum;
    bool lz4BlockChecksum;
    bool lz4IndependentBlocks;
    
    FirmwareInfo() 
        : type(FirmwareType::Unknown)
        , offset(0)
        , size(0)
        , uncompressedSize(0)
        , compression(CompressionType::None)
        , lz4BlockSizeId(0)
        , lz4ContentChecksum(false)
        , lz4BlockChecksum(false)
        , lz4IndependentBlocks(false)
    {}
};

// Magic numbers and signatures
constexpr uint32_t LZ4_MAGIC = 0x184D2204;
constexpr uint16_t GZIP_MAGIC = 0x1F8B;
constexpr char TAR_MAGIC[] = "ustar";
constexpr uint32_t DEVINFO_MAGIC = 0x12345678;

} // namespace Odin

#endif // FIRMWARE_INFO_H
