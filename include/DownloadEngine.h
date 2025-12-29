/*
 * Odin4 - Samsung Firmware Flashing Tool for Linux
 * DownloadEngine - Core download protocol implementation
 */

#ifndef DOWNLOAD_ENGINE_H
#define DOWNLOAD_ENGINE_H

#include <string>
#include <memory>
#include "UsbDevice.h"
#include "FirmwareData.h"
#include "FirmwareInfo.h"

namespace Odin {

// Protocol command codes (from decompiled code)
enum class ProtocolCmd : int {
    SessionControl = 0x64,   // Session management
    PIT = 0x65,              // PIT operations
    FileTransfer = 0x66,     // File transfer
    Connection = 0x67,       // Connection control
    DeviceInfo = 0x69        // Device information
};

// Session sub-commands
enum class SessionSubCmd : int {
    Begin = 0,
    SetPacketSize = 5,
    GetTotalBytes = 2,
    EnableTFlash = 3,
    FileTransferEnd = 4
};

// PIT sub-commands
enum class PITSubCmd : int {
    Start = 0,
    GetSize = 1,
    GetData = 2,
    End = 3
};

// File transfer sub-commands
enum class FileSubCmd : int {
    Start = 0,
    SetInfo = 1,
    SendData = 2,
    End = 3
};

// Connection sub-commands
enum class ConnSubCmd : int {
    Close = 0,
    Reboot = 1,
    Redownload = 2
};

// Write protection fail codes
enum class WriteProtectionFail : int {
    Unknown = 0,
    WriteError = -4,
    Erase = -3,
    Auth = -5,
    Size = -6,
    Ext4 = -7
};

class DownloadEngine {
public:
    static const std::string TAG;
    
    DownloadEngine(const std::string& devicePath, FirmwareData* firmware);
    ~DownloadEngine();
    
    // Non-copyable
    DownloadEngine(const DownloadEngine&) = delete;
    DownloadEngine& operator=(const DownloadEngine&) = delete;
    
    // Main operations
    bool download();              // Full download sequence
    bool redownload();            // Reboot to download mode
    
    // Connection management
    bool setupConnection();       // ODIN/LOKE handshake
    bool initializeConnection();  // Session setup
    bool closeConnection();       // End session
    
    // Device info
    bool getDeviceInfo();
    
    // PIT operations
    bool sendPitInfo();
    bool receivePitInfo();
    
    // File transfer
    bool transmitData(const std::shared_ptr<char[]>& data, const FirmwareInfo& info);
    bool transmitCompressedData(const std::shared_ptr<char[]>& data, const FirmwareInfo& info);
    
private:
    // Protocol helpers
    bool request(int cmd, int subcmd, int arg = 0);
    bool requestAndResponse(int cmd, int subcmd, int* received = nullptr, int expected = 0);
    bool requestAndResponse(int cmd, int subcmd, int* received, int* extra);
    
    // Data transfer
    bool sendData(const char* data, int size);
    bool sendPitData(const char* data, int size);
    
    // Response handling
    bool deviceInfoAnalysis(char* data);
    void writeProtectionFail(int code);
    
    // Member variables
    std::unique_ptr<UsbDevice> device_;
    FirmwareData* firmware_;
    std::string devicePath_;
    
    int packetSize_;
    bool hasDeviceInfo_;
};

} // namespace Odin

#endif // DOWNLOAD_ENGINE_H
