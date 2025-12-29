/*
 * Odin4 - Samsung Firmware Flashing Tool for Linux
 * DownloadEngine - Core download protocol implementation
 */

#include "DownloadEngine.h"
#include "Log.h"
#include "OdinException.h"
#include <cstring>
#include <chrono>
#include <thread>

namespace Odin {

const std::string DownloadEngine::TAG = "DownloadEngine";

// Protocol constants
constexpr int PACKET_HEADER_SIZE = 0x800;  // 2KB header
constexpr int DEFAULT_TRANSFER_SIZE = 0x100000;  // 1MB

DownloadEngine::DownloadEngine(const std::string& devicePath, FirmwareData* firmware)
    : device_(nullptr)
    , firmware_(firmware)
    , devicePath_(devicePath)
    , packetSize_(DEFAULT_PACKET_SIZE)
    , hasDeviceInfo_(false)
{
    Log::info(TAG, "Creating download engine for: " + devicePath);
    
    // Create USB device
    device_ = UsbDevice::create(devicePath);
    
    if (!device_ || !device_->isValid()) {
        Log::error(TAG, "USB device creation failed");
    }
}

DownloadEngine::~DownloadEngine() {
    Log::info(TAG, "Destroying download engine");
}

bool DownloadEngine::setupConnection() {
    Log::info(TAG, "Setting up connection (ODIN/LOKE handshake)");
    
    if (!device_ || !device_->isValid()) {
        Log::error(TAG, "Invalid USB device");
        return false;
    }
    
    // Send "ODIN"
    const char odinStr[] = "ODIN";
    int written = device_->write(odinStr, 4, HANDSHAKE_TIMEOUT);
    
    if (written != 4) {
        Log::error(TAG, "Failed to send ODIN handshake");
        return false;
    }
    
    // Receive "LOKE"
    char response[64] = {0};
    int received = device_->read(response, 64, HANDSHAKE_TIMEOUT, false);
    
    if (received >= 4 && 
        response[0] == 'L' && response[1] == 'O' && 
        response[2] == 'K' && response[3] == 'E') {
        Log::info(TAG, "Handshake successful (LOKE received)");
        return true;
    }
    
    Log::error(TAG, "Invalid handshake response");
    return false;
}

bool DownloadEngine::initializeConnection() {
    Log::info(TAG, "Initializing session");
    
    int sessionResult = 0;
    
    // Begin session (0x64, 0)
    if (!requestAndResponse(static_cast<int>(ProtocolCmd::SessionControl), 
                            static_cast<int>(SessionSubCmd::Begin), 
                            &sessionResult, 4)) {
        Log::error(TAG, "Failed to begin session");
        return false;
    }
    
    Log::info(TAG, "Session result: " + std::to_string(sessionResult));
    
    // If device supports packet size change (result != 0)
    if (sessionResult != 0) {
        packetSize_ = DEFAULT_TRANSFER_SIZE;  // 1MB
        
        // Set packet size (0x64, 5)
        if (!requestAndResponse(static_cast<int>(ProtocolCmd::SessionControl),
                                static_cast<int>(SessionSubCmd::SetPacketSize),
                                nullptr, packetSize_)) {
            Log::error(TAG, "Failed to set packet size");
            return false;
        }
        
        Log::info(TAG, "Packet size set to: " + std::to_string(packetSize_));
    }
    
    // Check if ZLP is supported
    if (device_->isSupportedZLP()) {
        // Get total bytes (0x64, 2)
        int totalBytes = 0;
        if (requestAndResponse(static_cast<int>(ProtocolCmd::SessionControl),
                               static_cast<int>(SessionSubCmd::GetTotalBytes),
                               nullptr, &totalBytes)) {
            Log::info(TAG, "Total bytes: " + std::to_string(totalBytes));
        }
    }
    
    // Enable TFlash if needed (0x64, 3)
    if (firmware_ && firmware_->isErase()) {
        if (!requestAndResponse(static_cast<int>(ProtocolCmd::SessionControl),
                                static_cast<int>(SessionSubCmd::EnableTFlash),
                                nullptr, 1)) {
            Log::error(TAG, "Failed to enable erase mode");
            return false;
        }
        Log::info(TAG, "Erase mode enabled");
    }
    
    return true;
}

bool DownloadEngine::getDeviceInfo() {
    Log::info(TAG, "Getting device info");
    
    int infoSize = 0;
    
    // Request device info size (0x69, 0)
    if (!requestAndResponse(static_cast<int>(ProtocolCmd::DeviceInfo), 0,
                            &infoSize, 0)) {
        Log::error(TAG, "Failed to get device info size");
        return false;
    }
    
    if (infoSize <= 0) {
        Log::error(TAG, "Invalid device info size");
        return false;
    }
    
    // Request device info (0x69, 1)
    if (!request(static_cast<int>(ProtocolCmd::DeviceInfo), 1, infoSize)) {
        Log::error(TAG, "Failed to request device info");
        return false;
    }
    
    // Read device info data
    std::vector<char> infoData(infoSize);
    int received = device_->read(infoData.data(), infoSize, TRANSFER_TIMEOUT, false);
    
    if (received <= 0) {
        Log::error(TAG, "Failed to receive device info");
        return false;
    }
    
    // Analyze device info
    if (!deviceInfoAnalysis(infoData.data())) {
        Log::error(TAG, "Failed to analyze device info");
        return false;
    }
    
    // End device info (0x69, 2)
    if (!requestAndResponse(static_cast<int>(ProtocolCmd::DeviceInfo), 2)) {
        Log::error(TAG, "Failed to end device info");
        return false;
    }
    
    hasDeviceInfo_ = true;
    return true;
}

bool DownloadEngine::sendPitInfo() {
    Log::info(TAG, "Sending PIT info");
    
    if (!firmware_) {
        return false;
    }
    
    // Check if we have PIT data
    const std::string& pitPath = firmware_->getPITPath();
    if (pitPath.empty()) {
        Log::info(TAG, "No PIT file specified");
        return true;
    }
    
    // PIT start (0x65, 0)
    if (!requestAndResponse(static_cast<int>(ProtocolCmd::PIT), 
                            static_cast<int>(PITSubCmd::Start))) {
        Log::error(TAG, "Failed to start PIT transfer");
        return false;
    }
    
    // Send PIT size
    size_t pitSize = firmware_->getPITSize();
    if (!requestAndResponse(static_cast<int>(ProtocolCmd::PIT),
                            static_cast<int>(PITSubCmd::GetSize),
                            nullptr, static_cast<int>(pitSize))) {
        Log::error(TAG, "Failed to send PIT size");
        return false;
    }
    
    // TODO: Send actual PIT data
    // This would involve reading the PIT file and sending it in chunks
    
    // PIT end (0x65, 3)
    if (!requestAndResponse(static_cast<int>(ProtocolCmd::PIT),
                            static_cast<int>(PITSubCmd::End))) {
        Log::error(TAG, "Failed to end PIT transfer");
        return false;
    }
    
    return true;
}

bool DownloadEngine::receivePitInfo() {
    Log::info(TAG, "Receiving PIT info from device");
    
    // Check packet size for newer protocol
    if (packetSize_ == DEFAULT_TRANSFER_SIZE) {
        // Get PIT size from device (0x64, 7)
        int pitSize = 0;
        if (!requestAndResponse(static_cast<int>(ProtocolCmd::SessionControl), 7,
                                &pitSize, 0)) {
            Log::error(TAG, "Failed to get PIT size");
            return false;
        }
        
        if (pitSize <= 0) {
            Log::error(TAG, "Invalid PIT size");
            return false;
        }
        
        Log::info(TAG, "PIT size: " + std::to_string(pitSize));
    }
    
    // PIT receive start (0x65, 1)
    int pitSize = 0;
    if (!requestAndResponse(static_cast<int>(ProtocolCmd::PIT),
                            static_cast<int>(PITSubCmd::GetSize),
                            &pitSize, 0)) {
        Log::error(TAG, "Failed to start PIT receive");
        return false;
    }
    
    if (pitSize <= 0) {
        Log::error(TAG, "No PIT data available");
        return false;
    }
    
    // Calculate transfer size
    int transferSize = (pitSize + 499) / 500 * 500;  // Round up to 500
    
    // Request PIT data (0x65, 2)
    if (!request(static_cast<int>(ProtocolCmd::PIT),
                 static_cast<int>(PITSubCmd::GetData), transferSize)) {
        Log::error(TAG, "Failed to request PIT data");
        return false;
    }
    
    // Read PIT data
    std::vector<char> pitData(transferSize);
    int received = device_->read(pitData.data(), transferSize, TRANSFER_TIMEOUT, false);
    
    if (received < pitSize) {
        Log::error(TAG, "Failed to receive complete PIT data");
        closeConnection();
        return false;
    }
    
    // Parse and display PIT
    Log::info(TAG, "Received " + std::to_string(received) + " bytes of PIT data");
    
    // PIT end (0x65, 3)
    if (!requestAndResponse(static_cast<int>(ProtocolCmd::PIT),
                            static_cast<int>(PITSubCmd::End))) {
        Log::error(TAG, "Failed to end PIT receive");
        return false;
    }
    
    return true;
}

bool DownloadEngine::transmitData(const std::shared_ptr<char[]>& data, 
                                   const FirmwareInfo& info) {
    Log::info(TAG, "Transmitting: " + info.filename + 
              " (" + std::to_string(info.size) + " bytes)");
    
    // File transfer start (0x66, 0)
    if (!requestAndResponse(static_cast<int>(ProtocolCmd::FileTransfer),
                            static_cast<int>(FileSubCmd::Start))) {
        Log::error(TAG, "Failed to start file transfer");
        return false;
    }
    
    // Send file info (0x66, 1)
    // The info includes: file size, partition name, etc.
    if (!requestAndResponse(static_cast<int>(ProtocolCmd::FileTransfer),
                            static_cast<int>(FileSubCmd::SetInfo),
                            nullptr, static_cast<int>(info.size))) {
        Log::error(TAG, "Failed to set file info");
        return false;
    }
    
    // Transfer data in packets
    size_t offset = 0;
    size_t remaining = info.size;
    
    while (remaining > 0) {
        size_t chunkSize = std::min(remaining, static_cast<size_t>(packetSize_));
        
        if (!sendData(data.get() + offset, static_cast<int>(chunkSize))) {
            Log::error(TAG, "Failed to send data chunk");
            return false;
        }
        
        offset += chunkSize;
        remaining -= chunkSize;
        
        // Progress update
        int progress = static_cast<int>((offset * 100) / info.size);
        if (progress % 10 == 0) {
            Log::info(TAG, "Progress: " + std::to_string(progress) + "%");
        }
    }
    
    // File transfer end (0x66, 3)
    if (!requestAndResponse(static_cast<int>(ProtocolCmd::FileTransfer),
                            static_cast<int>(FileSubCmd::End))) {
        Log::error(TAG, "Failed to end file transfer");
        return false;
    }
    
    Log::info(TAG, "Transfer complete: " + info.filename);
    return true;
}

bool DownloadEngine::transmitCompressedData(const std::shared_ptr<char[]>& data,
                                             const FirmwareInfo& info) {
    Log::info(TAG, "Transmitting compressed: " + info.filename);
    
    // For LZ4 compressed data, send the compressed stream directly
    // The device will decompress it
    
    return transmitData(data, info);
}

bool DownloadEngine::closeConnection() {
    Log::info(TAG, "Closing connection");
    
    // End session (0x67, 0)
    return requestAndResponse(static_cast<int>(ProtocolCmd::Connection),
                              static_cast<int>(ConnSubCmd::Close));
}

bool DownloadEngine::download() {
    Log::info(TAG, "Starting download");
    
    // 1. Setup connection (ODIN/LOKE)
    if (!setupConnection()) {
        Log::error(TAG, "Setup connection failed");
        return false;
    }
    
    // 2. Initialize session
    if (!initializeConnection()) {
        Log::error(TAG, "Initialize connection failed");
        return false;
    }
    
    // 3. Get device info (optional)
    getDeviceInfo();
    
    // 4. Receive PIT from device
    if (!receivePitInfo()) {
        Log::error(TAG, "Receive PIT failed");
        closeConnection();
        return false;
    }
    
    // 5. Send PIT if provided
    if (!sendPitInfo()) {
        Log::error(TAG, "Send PIT failed");
        closeConnection();
        return false;
    }
    
    // 6. Transfer firmware files
    if (firmware_) {
        for (const auto& file : firmware_->getFiles()) {
            bool success;
            
            if (file.compression == CompressionType::LZ4) {
                success = transmitCompressedData(file.data, file);
            } else {
                success = transmitData(file.data, file);
            }
            
            if (!success) {
                Log::error(TAG, "File transfer failed: " + file.filename);
                closeConnection();
                return false;
            }
        }
    }
    
    // 7. Close connection
    if (!closeConnection()) {
        Log::error(TAG, "Close connection failed");
        return false;
    }
    
    // 8. Reboot (0x67, 1)
    request(static_cast<int>(ProtocolCmd::Connection),
            static_cast<int>(ConnSubCmd::Reboot));
    
    Log::info(TAG, "Download complete");
    return true;
}

bool DownloadEngine::redownload() {
    Log::info(TAG, "Rebooting to download mode");
    
    if (!setupConnection()) {
        return false;
    }
    
    if (!initializeConnection()) {
        return false;
    }
    
    // Redownload command (0x67, 2)
    return requestAndResponse(static_cast<int>(ProtocolCmd::Connection),
                              static_cast<int>(ConnSubCmd::Redownload));
}

bool DownloadEngine::request(int cmd, int subcmd, int arg) {
    char packet[PACKET_HEADER_SIZE] = {0};
    
    // Build request packet
    *reinterpret_cast<int*>(packet) = cmd;
    *reinterpret_cast<int*>(packet + 4) = subcmd;
    *reinterpret_cast<int*>(packet + 8) = arg;
    
    int written = device_->write(packet, packetSize_, TRANSFER_TIMEOUT);
    
    if (written != packetSize_) {
        Log::error(TAG, "Request write failed");
        return false;
    }
    
    return true;
}

bool DownloadEngine::requestAndResponse(int cmd, int subcmd, int* received, int expected) {
    if (!request(cmd, subcmd, expected)) {
        return false;
    }
    
    // Read response
    char response[PACKET_HEADER_SIZE] = {0};
    int bytesRead = device_->read(response, sizeof(response), TRANSFER_TIMEOUT, false);
    
    if (bytesRead < 8) {
        Log::error(TAG, "Response too short");
        return false;
    }
    
    int responseCmd = *reinterpret_cast<int*>(response);
    int responseVal = *reinterpret_cast<int*>(response + 4);
    
    if (responseCmd != cmd) {
        // Check for error codes
        int errorCode = *reinterpret_cast<int*>(response + 8);
        if (errorCode < 0) {
            writeProtectionFail(errorCode);
        }
        return false;
    }
    
    if (received) {
        *received = responseVal;
    }
    
    return true;
}

bool DownloadEngine::requestAndResponse(int cmd, int subcmd, int* received, int* extra) {
    if (!request(cmd, subcmd, 0)) {
        return false;
    }
    
    char response[PACKET_HEADER_SIZE] = {0};
    int bytesRead = device_->read(response, sizeof(response), TRANSFER_TIMEOUT, false);
    
    if (bytesRead < 12) {
        return false;
    }
    
    if (received) {
        *received = *reinterpret_cast<int*>(response + 4);
    }
    if (extra) {
        *extra = *reinterpret_cast<int*>(response + 8);
    }
    
    return true;
}

bool DownloadEngine::sendData(const char* data, int size) {
    int written = device_->write(data, size, TRANSFER_TIMEOUT);
    
    if (written != size) {
        Log::error(TAG, "Data write failed: " + std::to_string(written) + "/" + std::to_string(size));
        return false;
    }
    
    // Read acknowledgment
    char ack[64] = {0};
    int ackSize = device_->read(ack, 64, TRANSFER_TIMEOUT, false);
    
    if (ackSize < 8) {
        Log::error(TAG, "ACK read failed");
        return false;
    }
    
    return true;
}

bool DownloadEngine::sendPitData(const char* data, int size) {
    int written = device_->write(data, size, TRANSFER_TIMEOUT);
    
    if (written != size) {
        return false;
    }
    
    char ack[64] = {0};
    int ackSize = device_->read(ack, 64, TRANSFER_TIMEOUT, false);
    
    return ackSize >= 8;
}

bool DownloadEngine::deviceInfoAnalysis(char* data) {
    // Check magic number
    uint32_t magic = *reinterpret_cast<uint32_t*>(data);
    
    if (magic != DEVINFO_MAGIC) {
        Log::error(TAG, "Invalid device info magic");
        return false;
    }
    
    // Parse device info structure
    int numEntries = *reinterpret_cast<int*>(data + 4);
    
    Log::info(TAG, "Device info entries: " + std::to_string(numEntries));
    
    // Additional parsing would go here
    // The structure contains partition info, version strings, etc.
    
    return true;
}

void DownloadEngine::writeProtectionFail(int code) {
    std::string message;
    
    switch (code) {
        case -7:
            message = "FAIL! (Ext4)";
            break;
        case -6:
            message = "FAIL! (Size)";
            break;
        case -5:
            message = "FAIL! (Auth)";
            break;
        case -4:
            message = "FAIL! (Write)";
            break;
        case -3:
            message = "FAIL! (Erase)";
            break;
        case -2:
            message = "FAIL!";
            break;
        default:
            message = "FAIL! (Unknown: " + std::to_string(code) + ")";
            break;
    }
    
    Log::error(TAG, message);
}

} // namespace Odin
