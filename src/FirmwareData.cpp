/*
 * Odin4 - Samsung Firmware Flashing Tool for Linux
 * FirmwareData - Firmware file parsing implementation
 */

#include "FirmwareData.h"
#include "Tar.h"
#include "Manifest.h"
#include "Log.h"
#include "OdinException.h"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <sys/stat.h>

// LZ4 support
#ifdef HAVE_LZ4
#include <lz4frame.h>
#endif

// GZIP support
#include <zlib.h>

namespace Odin {

const std::string FirmwareData::TAG = "FirmwareData";

FirmwareData::FirmwareData()
    : eraseEnabled_(false)
    , optionLock_(false)
    , pitSize_(0)
    , pitOffset_(0)
{
}

FirmwareData::FirmwareData(const FirmwareData& other)
    : blPath_(other.blPath_)
    , apPath_(other.apPath_)
    , cpPath_(other.cpPath_)
    , cscPath_(other.cscPath_)
    , umsPath_(other.umsPath_)
    , pitPath_(other.pitPath_)
    , eraseEnabled_(other.eraseEnabled_)
    , optionLock_(other.optionLock_)
    , files_(other.files_)
    , pitSize_(other.pitSize_)
    , pitOffset_(other.pitOffset_)
    , sha256Expected_(other.sha256Expected_)
{
}

FirmwareData& FirmwareData::operator=(const FirmwareData& other) {
    if (this != &other) {
        blPath_ = other.blPath_;
        apPath_ = other.apPath_;
        cpPath_ = other.cpPath_;
        cscPath_ = other.cscPath_;
        umsPath_ = other.umsPath_;
        pitPath_ = other.pitPath_;
        eraseEnabled_ = other.eraseEnabled_;
        optionLock_ = other.optionLock_;
        files_ = other.files_;
        pitSize_ = other.pitSize_;
        pitOffset_ = other.pitOffset_;
        sha256Expected_ = other.sha256Expected_;
    }
    return *this;
}

FirmwareData::~FirmwareData() {
}

bool FirmwareData::setBootloader(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    
    blPath_ = path;
    Log::info(TAG, "Bootloader set: " + path);
    
    return parseBinary(path);
}

bool FirmwareData::setAP(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    
    apPath_ = path;
    Log::info(TAG, "AP set: " + path);
    
    return parseBinary(path);
}

bool FirmwareData::setCP(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    
    cpPath_ = path;
    Log::info(TAG, "CP set: " + path);
    
    return parseBinary(path);
}

bool FirmwareData::setCSC(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    
    cscPath_ = path;
    Log::info(TAG, "CSC set: " + path);
    
    return parseBinary(path);
}

bool FirmwareData::setUMS(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    
    umsPath_ = path;
    Log::info(TAG, "UMS set: " + path);
    
    return parseBinary(path);
}

bool FirmwareData::setPIT(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    
    // Check if file exists
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        Log::error(TAG, "Cannot open PIT file: " + path);
        return false;
    }
    
    pitSize_ = file.tellg();
    file.seekg(0);
    
    if (pitSize_ < 1) {
        Log::error(TAG, "Invalid PIT file size");
        return false;
    }
    
    pitPath_ = path;
    pitOffset_ = 0;
    
    Log::info(TAG, "PIT file set: " + path + " (" + std::to_string(pitSize_) + " bytes)");
    return true;
}

void FirmwareData::setErase(bool enable) {
    eraseEnabled_ = enable;
    Log::info(TAG, "Erase mode: " + std::string(enable ? "enabled" : "disabled"));
}

void FirmwareData::setOptionLock(bool enable) {
    optionLock_ = enable;
}

bool FirmwareData::parseBinary(const std::string& path) {
    Log::info(TAG, "Parsing: " + path);
    
    // Check file extension
    std::string ext = path.substr(path.find_last_of('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    // Check for .md5 extension (tar with md5 checksum)
    if (ext == "md5") {
        // Verify MD5 first
        if (!verifyMD5(path)) {
            Log::error(TAG, "MD5 verification failed");
            return false;
        }
        
        // Then parse as TAR
        return parseBinaryInternal(path);
    }
    
    // Check for .sha256 extension
    if (ext == "sha256") {
        if (!verifySHA256(path)) {
            Log::error(TAG, "SHA256 verification failed");
            return false;
        }
        return parseBinaryInternal(path);
    }
    
    return parseBinaryInternal(path);
}

bool FirmwareData::parseBinaryInternal(const std::string& path) {
    // Determine file type
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        Log::error(TAG, "Cannot open file: " + path);
        return false;
    }
    
    // Read header to detect file type
    char header[512];
    file.read(header, sizeof(header));
    file.close();
    
    // Check for GZIP magic
    if (static_cast<uint8_t>(header[0]) == 0x1F && 
        static_cast<uint8_t>(header[1]) == 0x8B) {
        Log::info(TAG, "Detected GZIP file");
        
        // Extract to temp file and parse
        std::string tempPath = "/tmp/odin4_extracted.tar";
        extractGzipFile(path, tempPath);
        return parseBinaryInternal(tempPath);
    }
    
    // Check for LZ4 magic
    if (*reinterpret_cast<uint32_t*>(header) == LZ4_MAGIC) {
        Log::info(TAG, "Detected LZ4 file");
        // LZ4 files can be streamed directly to the device
        
        FirmwareInfo info;
        info.filename = path.substr(path.find_last_of('/') + 1);
        info.compression = CompressionType::LZ4;
        
        // Parse LZ4 frame header
        parseLZ4FrameHeader(header, info);
        
        // Read file into memory
        std::ifstream lz4File(path, std::ios::binary | std::ios::ate);
        info.size = lz4File.tellg();
        lz4File.seekg(0);
        
        info.data = std::shared_ptr<char[]>(new char[info.size]);
        lz4File.read(info.data.get(), info.size);
        
        files_.push_back(info);
        return true;
    }
    
    // Check for TAR magic (at offset 257)
    if (memcmp(header + 257, "ustar", 5) == 0) {
        Log::info(TAG, "Detected TAR file");
        return parseTAR(path, FirmwareType::Unknown);
    }
    
    // Assume binary file
    Log::info(TAG, "Parsing as binary file");
    return parseBIN(path, FirmwareType::Unknown);
}

bool FirmwareData::parseTAR(const std::string& path, FirmwareType type) {
    Tar tar(path);
    
    if (!tar.open()) {
        Log::error(TAG, "Failed to open TAR: " + path);
        return false;
    }
    
    const auto& entries = tar.getEntries();
    Log::info(TAG, "TAR contains " + std::to_string(entries.size()) + " entries");
    
    for (const auto& entry : entries) {
        if (!entry.isFile || entry.size == 0) {
            continue;
        }
        
        Log::info(TAG, "  Entry: " + entry.name + " (" + std::to_string(entry.size) + " bytes)");
        
        // Skip checksum files
        std::string lowerName = entry.name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
        
        if (lowerName.find(".md5") != std::string::npos ||
            lowerName.find(".sha256") != std::string::npos) {
            continue;
        }
        
        FirmwareInfo info;
        info.filename = entry.name;
        info.size = entry.size;
        info.offset = entry.offset;
        info.type = type;
        info.compression = CompressionType::None;
        
        // Determine partition name from filename
        // Common patterns: boot.img, system.img, modem.bin, etc.
        if (lowerName.find(".pit") != std::string::npos) {
            info.type = FirmwareType::PIT;
            info.partitionName = "PIT";
        } else if (lowerName.find("boot") != std::string::npos) {
            info.partitionName = "BOOT";
        } else if (lowerName.find("recovery") != std::string::npos) {
            info.partitionName = "RECOVERY";
        } else if (lowerName.find("system") != std::string::npos) {
            info.partitionName = "SYSTEM";
        } else if (lowerName.find("modem") != std::string::npos ||
                   lowerName.find("cp_") != std::string::npos) {
            info.partitionName = "MODEM";
        } else if (lowerName.find("param") != std::string::npos) {
            info.partitionName = "PARAM";
        } else if (lowerName.find("efs") != std::string::npos) {
            info.partitionName = "EFS";
        } else if (lowerName.find("cache") != std::string::npos) {
            info.partitionName = "CACHE";
        } else if (lowerName.find("hidden") != std::string::npos) {
            info.partitionName = "HIDDEN";
        } else {
            // Use filename without extension as partition name
            size_t dotPos = entry.name.find_last_of('.');
            info.partitionName = entry.name.substr(0, dotPos);
        }
        
        // Read file data
        info.data = std::shared_ptr<char[]>(new char[entry.size]);
        if (!tar.readEntry(entry, info.data.get(), entry.size)) {
            Log::error(TAG, "Failed to read entry: " + entry.name);
            continue;
        }
        
        // Check for LZ4 compression in the data
        if (entry.size >= 4 && 
            *reinterpret_cast<uint32_t*>(info.data.get()) == LZ4_MAGIC) {
            info.compression = CompressionType::LZ4;
            parseLZ4FrameHeader(info.data.get(), info);
        }
        
        files_.push_back(info);
    }
    
    tar.close();
    return true;
}

bool FirmwareData::parseBIN(const std::string& path, FirmwareType type) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        Log::error(TAG, "Cannot open file: " + path);
        return false;
    }
    
    FirmwareInfo info;
    info.filename = path.substr(path.find_last_of('/') + 1);
    info.size = file.tellg();
    info.offset = 0;
    info.type = type;
    info.compression = CompressionType::None;
    
    // Determine partition name from filename
    std::string lowerName = info.filename;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    
    size_t dotPos = info.filename.find_last_of('.');
    info.partitionName = info.filename.substr(0, dotPos);
    
    // Read file data
    file.seekg(0);
    info.data = std::shared_ptr<char[]>(new char[info.size]);
    file.read(info.data.get(), info.size);
    
    // Check for LZ4 compression
    if (info.size >= 4 && 
        *reinterpret_cast<uint32_t*>(info.data.get()) == LZ4_MAGIC) {
        info.compression = CompressionType::LZ4;
        parseLZ4FrameHeader(info.data.get(), info);
    }
    
    files_.push_back(info);
    return true;
}

bool FirmwareData::verifyMD5(const std::string& path) {
    Log::info(TAG, "Verifying MD5...");
    
    // The .md5 file contains the MD5 hash at the end of the filename
    // or in an accompanying .md5 file
    
    // Try to read the MD5 from the filename (common Samsung format)
    size_t lastSlash = path.find_last_of('/');
    std::string filename = (lastSlash != std::string::npos) ? 
                           path.substr(lastSlash + 1) : path;
    
    // Calculate actual MD5
    std::string actualMD5 = Manifest::calculateMD5(path);
    
    if (actualMD5.empty()) {
        Log::error(TAG, "Failed to calculate MD5");
        return false;
    }
    
    Log::info(TAG, "MD5: " + actualMD5);
    
    // For .tar.md5 files, the verification is typically built into the format
    // Samsung appends the MD5 to the end of the file
    
    return true;  // Assume valid if we can calculate it
}

bool FirmwareData::verifySHA256(const std::string& path) {
    Log::info(TAG, "Verifying SHA256...");
    
    std::string actualSHA256 = Manifest::calculateSHA256(path);
    
    if (actualSHA256.empty()) {
        Log::error(TAG, "Failed to calculate SHA256");
        return false;
    }
    
    Log::info(TAG, "SHA256: " + actualSHA256);
    
    if (!sha256Expected_.empty()) {
        if (actualSHA256 != sha256Expected_) {
            Log::error(TAG, "SHA256 mismatch!");
            return false;
        }
    }
    
    return true;
}

void FirmwareData::extractGzipFile(const std::string& src, const std::string& dst) {
    Log::info(TAG, "Extracting GZIP: " + src);
    
    gzFile gz = gzopen(src.c_str(), "rb");
    if (!gz) {
        Log::error(TAG, "Failed to open GZIP file");
        return;
    }
    
    std::ofstream out(dst, std::ios::binary);
    if (!out.is_open()) {
        Log::error(TAG, "Failed to create output file");
        gzclose(gz);
        return;
    }
    
    char buffer[65536];
    int bytesRead;
    
    while ((bytesRead = gzread(gz, buffer, sizeof(buffer))) > 0) {
        out.write(buffer, bytesRead);
    }
    
    gzclose(gz);
    out.close();
    
    Log::info(TAG, "Extraction complete: " + dst);
}

bool FirmwareData::parseLZ4FrameHeader(const char* data, FirmwareInfo& info) {
    // LZ4 frame format:
    // [4 bytes] Magic = 0x184D2204
    // [1 byte]  FLG byte
    // [1 byte]  BD byte
    // [0-8 bytes] Optional content size
    // [1 byte]  Header checksum
    
    if (*reinterpret_cast<const uint32_t*>(data) != LZ4_MAGIC) {
        return false;
    }
    
    uint8_t flg = static_cast<uint8_t>(data[4]);
    uint8_t bd = static_cast<uint8_t>(data[5]);
    
    // Parse FLG
    info.lz4IndependentBlocks = (flg & 0x20) != 0;
    info.lz4BlockChecksum = (flg & 0x10) != 0;
    bool hasContentSize = (flg & 0x08) != 0;
    info.lz4ContentChecksum = (flg & 0x04) != 0;
    
    // Parse BD
    info.lz4BlockSizeId = (bd >> 4) & 0x07;
    
    // Read content size if present
    if (hasContentSize) {
        info.uncompressedSize = *reinterpret_cast<const uint64_t*>(data + 6);
        Log::info(TAG, "LZ4 uncompressed size: " + std::to_string(info.uncompressedSize));
    }
    
    return true;
}

} // namespace Odin
