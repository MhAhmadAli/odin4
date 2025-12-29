/*
 * Odin4 - Samsung Firmware Flashing Tool for Linux
 * FirmwareData - Firmware file parsing and management
 */

#ifndef FIRMWARE_DATA_H
#define FIRMWARE_DATA_H

#include <string>
#include <vector>
#include <memory>
#include "FirmwareInfo.h"

namespace Odin {

class FirmwareData {
public:
    static const std::string TAG;
    
    FirmwareData();
    FirmwareData(const FirmwareData& other);
    FirmwareData& operator=(const FirmwareData& other);
    ~FirmwareData();
    
    // File path setters (called from command line parsing)
    bool setBootloader(const std::string& path);   // -b
    bool setAP(const std::string& path);           // -a
    bool setCP(const std::string& path);           // -c
    bool setCSC(const std::string& path);          // -s
    bool setUMS(const std::string& path);          // -u
    bool setPIT(const std::string& path);          // -V
    
    // Option setters
    void setErase(bool enable);
    void setOptionLock(bool enable);
    
    // Getters
    bool isErase() const { return eraseEnabled_; }
    bool isOptionLock() const { return optionLock_; }
    
    // Path getters
    const std::string& getBootloaderPath() const { return blPath_; }
    const std::string& getAPPath() const { return apPath_; }
    const std::string& getCPPath() const { return cpPath_; }
    const std::string& getCSCPath() const { return cscPath_; }
    const std::string& getUMSPath() const { return umsPath_; }
    const std::string& getPITPath() const { return pitPath_; }
    
    // File information
    const std::vector<FirmwareInfo>& getFiles() const { return files_; }
    size_t getPITSize() const { return pitSize_; }
    
    // Parsing methods
    bool parseBinary(const std::string& path);
    
private:
    bool parseBinaryInternal(const std::string& path);
    bool parseTAR(const std::string& path, FirmwareType type);
    bool parseBIN(const std::string& path, FirmwareType type);
    
    bool verifyMD5(const std::string& path);
    bool verifySHA256(const std::string& path);
    void extractGzipFile(const std::string& src, const std::string& dst);
    bool parseLZ4FrameHeader(const char* data, FirmwareInfo& info);
    
    // File paths
    std::string blPath_;
    std::string apPath_;
    std::string cpPath_;
    std::string cscPath_;
    std::string umsPath_;
    std::string pitPath_;
    
    // Options
    bool eraseEnabled_;
    bool optionLock_;
    
    // Parsed data
    std::vector<FirmwareInfo> files_;
    size_t pitSize_;
    size_t pitOffset_;
    
    // SHA256 manifest
    std::string sha256Expected_;
};

} // namespace Odin

#endif // FIRMWARE_DATA_H
