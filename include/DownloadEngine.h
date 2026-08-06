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

// PIT sub-commands (Heimdall: PitFilePacket::Request)
enum class PITSubCmd : int {
    Flash = 0,        // Begin sending a PIT file to the device
    Dump = 1,         // Ask the device for its PIT (responds with the size)
    Part = 2,         // Request/announce a single transfer chunk
    EndTransfer = 3   // Finish the PIT transfer
};

// File transfer sub-commands (Heimdall: FileTransferPacket::Request)
enum class FileSubCmd : int {
    Flash = 0,        // Begin flashing a file
    Dump = 1,         // Read a file back from the device
    Part = 2,         // Announce the byte count of the sequence that follows
    End = 3           // Finish the sequence
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

    // Reboot the device into normal mode once the download finished (--reboot)
    void setRebootAfterDownload(bool enable) { rebootAfterDownload_ = enable; }

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
    bool deviceInfoAnalysis(const char* data, size_t size);
    void writeProtectionFail(int code);

    // Member variables
    std::unique_ptr<UsbDevice> device_;
    FirmwareData* firmware_;
    std::string devicePath_;

    // Size of a firmware data chunk. Negotiated with the device; it never
    // applies to the fixed-size command packets built by request().
    int packetSize_;
    bool hasDeviceInfo_;
    bool rebootAfterDownload_;
};

} // namespace Odin

#endif // DOWNLOAD_ENGINE_H
