/*
 * Odin4 - Samsung Firmware Flashing Tool for Linux
 * DownloadEngine - Core download protocol implementation
 */

#include "DownloadEngine.h"
#include "Log.h"
#include "OdinException.h"
#include "PIT.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <fstream>
#include <thread>
#include <vector>

namespace Odin {

const std::string DownloadEngine::TAG = "DownloadEngine";

// Protocol constants
constexpr int COMMAND_PACKET_SIZE = DEFAULT_PACKET_SIZE;  // 1KB, fixed for every command
constexpr int RESPONSE_PACKET_SIZE = 0x800;               // 2KB receive buffer
constexpr int DEFAULT_TRANSFER_SIZE = 0x100000;           // 1MB firmware chunk
constexpr int PIT_CHUNK_SIZE = 500;                       // PIT is transferred in 500 byte parts
constexpr int MAX_PIT_SIZE = 4 * 1024 * 1024;             // Sanity bound on a device-reported size
constexpr int MAX_DEVICE_INFO_SIZE = 1024 * 1024;

// The protocol is little-endian; go through memcpy rather than punning a
// pointer so the reads/writes stay well defined.
static void writeInt32(char* dst, int32_t value) {
    std::memcpy(dst, &value, sizeof(value));
}

static int32_t readInt32(const char* src) {
    int32_t value = 0;
    std::memcpy(&value, src, sizeof(value));
    return value;
}

DownloadEngine::DownloadEngine(const std::string& devicePath, FirmwareData* firmware)
    : device_(nullptr)
    , firmware_(firmware)
    , devicePath_(devicePath)
    , packetSize_(DEFAULT_PACKET_SIZE)
    , hasDeviceInfo_(false)
    , rebootAfterDownload_(false)
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

    if (!device_) {
        Log::error(TAG, "Invalid USB device");
        return false;
    }

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
        packetSize_ = std::min(DEFAULT_TRANSFER_SIZE, MAX_PACKET_SIZE);  // 1MB

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
    
    if (infoSize <= 0 || infoSize > MAX_DEVICE_INFO_SIZE) {
        Log::error(TAG, "Invalid device info size: " + std::to_string(infoSize));
        return false;
    }

    // Request device info (0x69, 1)
    if (!request(static_cast<int>(ProtocolCmd::DeviceInfo), 1, infoSize)) {
        Log::error(TAG, "Failed to request device info");
        return false;
    }

    // Read device info data
    std::vector<char> infoData(infoSize, 0);
    int received = device_->read(infoData.data(), infoSize, TRANSFER_TIMEOUT, false);

    if (received <= 0) {
        Log::error(TAG, "Failed to receive device info");
        return false;
    }

    // Analyze device info
    if (!deviceInfoAnalysis(infoData.data(), static_cast<size_t>(received))) {
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

    // Load the PIT file. Without this the transfer below used to be a no-op
    // that still reported success.
    std::ifstream pitFile(pitPath, std::ios::binary);
    if (!pitFile.is_open()) {
        Log::error(TAG, "Cannot open PIT file: " + pitPath);
        return false;
    }

    size_t pitSize = firmware_->getPITSize();
    if (pitSize == 0 || pitSize > static_cast<size_t>(MAX_PIT_SIZE)) {
        Log::error(TAG, "Invalid PIT file size: " + std::to_string(pitSize));
        return false;
    }

    std::vector<char> pitData(pitSize, 0);
    pitFile.read(pitData.data(), static_cast<std::streamsize>(pitSize));
    if (static_cast<size_t>(pitFile.gcount()) != pitSize) {
        Log::error(TAG, "Failed to read PIT file: " + pitPath);
        return false;
    }
    pitFile.close();

    // Sanity check the PIT before handing it to the device
    PIT pit;
    if (!pit.parse(pitData.data(), pitData.size())) {
        Log::error(TAG, "PIT file is not a valid partition table: " + pitPath);
        return false;
    }
    pit.print();

    Log::info(TAG, "Writing a new partition table to the device from " + pitPath);

    // Request PIT flash (0x65, 0)
    if (!requestAndResponse(static_cast<int>(ProtocolCmd::PIT),
                            static_cast<int>(PITSubCmd::Flash))) {
        Log::error(TAG, "Failed to start PIT transfer");
        return false;
    }

    // Send the PIT contents in chunks
    size_t offset = 0;
    while (offset < pitSize) {
        int chunkSize = static_cast<int>(std::min(pitSize - offset,
                                                  static_cast<size_t>(PIT_CHUNK_SIZE)));
        if (!sendPitData(pitData.data() + offset, chunkSize)) {
            Log::error(TAG, "Failed to send PIT data at offset " + std::to_string(offset));
            return false;
        }
        offset += static_cast<size_t>(chunkSize);
    }

    // End the transfer, telling the device how much was sent (0x65, 3)
    if (!requestAndResponse(static_cast<int>(ProtocolCmd::PIT),
                            static_cast<int>(PITSubCmd::EndTransfer),
                            nullptr, static_cast<int>(pitSize))) {
        Log::error(TAG, "Failed to end PIT transfer");
        return false;
    }

    Log::info(TAG, "PIT transfer complete (" + std::to_string(pitSize) + " bytes)");
    return true;
}

bool DownloadEngine::receivePitInfo() {
    Log::info(TAG, "Receiving PIT info from device");

    // Ask the device to dump its PIT; the response carries the size (0x65, 1)
    int pitSize = 0;
    if (!requestAndResponse(static_cast<int>(ProtocolCmd::PIT),
                            static_cast<int>(PITSubCmd::Dump),
                            &pitSize, 0)) {
        Log::error(TAG, "Failed to start PIT receive");
        return false;
    }

    if (pitSize <= 0 || pitSize > MAX_PIT_SIZE) {
        Log::error(TAG, "Device reported an implausible PIT size: " + std::to_string(pitSize));
        return false;
    }

    Log::info(TAG, "PIT size: " + std::to_string(pitSize));

    // The device hands the PIT over one fixed-size part at a time; each part
    // needs its own request. Reading the whole table in a single bulk transfer
    // leaves the remaining parts queued on the endpoint.
    int partCount = (pitSize + PIT_CHUNK_SIZE - 1) / PIT_CHUNK_SIZE;
    std::vector<char> pitData(static_cast<size_t>(partCount) * PIT_CHUNK_SIZE, 0);

    for (int part = 0; part < partCount; part++) {
        // Request PIT part (0x65, 2)
        if (!request(static_cast<int>(ProtocolCmd::PIT),
                     static_cast<int>(PITSubCmd::Part), part)) {
            Log::error(TAG, "Failed to request PIT part " + std::to_string(part));
            return false;
        }

        char* dst = pitData.data() + static_cast<size_t>(part) * PIT_CHUNK_SIZE;
        int received = device_->read(dst, PIT_CHUNK_SIZE, TRANSFER_TIMEOUT, false);

        if (received < 0) {
            Log::error(TAG, "Failed to receive PIT part " + std::to_string(part));
            return false;
        }
    }

    pitData.resize(static_cast<size_t>(pitSize));
    Log::info(TAG, "Received " + std::to_string(pitSize) + " bytes of PIT data");

    // End the transfer before doing anything else with the connection (0x65, 3)
    if (!requestAndResponse(static_cast<int>(ProtocolCmd::PIT),
                            static_cast<int>(PITSubCmd::EndTransfer))) {
        Log::error(TAG, "Failed to end PIT receive");
        return false;
    }

    // Parse and display the table we just read
    PIT pit;
    if (pit.parse(pitData.data(), pitData.size())) {
        pit.print();
    } else {
        Log::error(TAG, "Could not parse the PIT received from the device");
    }

    return true;
}

bool DownloadEngine::transmitData(const std::shared_ptr<char[]>& data, 
                                   const FirmwareInfo& info) {
    Log::info(TAG, "Transmitting: " + info.filename +
              " (" + std::to_string(info.size) + " bytes)");

    if (!data || info.size == 0) {
        Log::error(TAG, "Nothing to transmit for " + info.filename);
        return false;
    }

    if (info.size > static_cast<size_t>(INT32_MAX)) {
        Log::error(TAG, "File too large for the protocol: " + info.filename);
        return false;
    }

    // File transfer start (0x66, 0)
    if (!requestAndResponse(static_cast<int>(ProtocolCmd::FileTransfer),
                            static_cast<int>(FileSubCmd::Flash))) {
        Log::error(TAG, "Failed to start file transfer");
        return false;
    }

    // Announce how many bytes follow (0x66, 2)
    if (!requestAndResponse(static_cast<int>(ProtocolCmd::FileTransfer),
                            static_cast<int>(FileSubCmd::Part),
                            nullptr, static_cast<int>(info.size))) {
        Log::error(TAG, "Failed to set file info");
        return false;
    }

    // Transfer data in packets
    size_t offset = 0;
    size_t remaining = info.size;
    int lastReported = -1;

    while (remaining > 0) {
        size_t chunkSize = std::min(remaining, static_cast<size_t>(packetSize_));

        if (!sendData(data.get() + offset, static_cast<int>(chunkSize))) {
            Log::error(TAG, "Failed to send data chunk");
            return false;
        }

        offset += chunkSize;
        remaining -= chunkSize;

        // Progress update, once per 10% step. Testing "progress % 10 == 0"
        // logged the same figure repeatedly and skipped steps entirely when a
        // chunk spanned more than one of them.
        int progress = static_cast<int>((offset * 100) / info.size);
        int step = progress / 10;
        if (step != lastReported) {
            lastReported = step;
            Log::info(TAG, "Progress: " + std::to_string(progress) + "%");
        }
    }

    // File transfer end (0x66, 3)
    if (!requestAndResponse(static_cast<int>(ProtocolCmd::FileTransfer),
                            static_cast<int>(FileSubCmd::End),
                            nullptr, static_cast<int>(info.size))) {
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

    // 8. Reboot, but only when the caller asked for it (--reboot)
    if (rebootAfterDownload_) {
        Log::info(TAG, "Rebooting into normal mode");
        request(static_cast<int>(ProtocolCmd::Connection),
                static_cast<int>(ConnSubCmd::Reboot));
    }

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
    if (!device_) {
        Log::error(TAG, "No USB device");
        return false;
    }

    // Command packets always have the fixed protocol size. packetSize_ is the
    // negotiated *firmware chunk* size (up to 1MB) and must never be used here:
    // doing so reads far past the end of this buffer.
    char packet[COMMAND_PACKET_SIZE] = {0};

    // Build request packet
    writeInt32(packet, cmd);
    writeInt32(packet + 4, subcmd);
    writeInt32(packet + 8, arg);

    int written = device_->write(packet, COMMAND_PACKET_SIZE, TRANSFER_TIMEOUT);

    if (written != COMMAND_PACKET_SIZE) {
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
    char response[RESPONSE_PACKET_SIZE] = {0};
    int bytesRead = device_->read(response, sizeof(response), TRANSFER_TIMEOUT, false);

    if (bytesRead < 8) {
        Log::error(TAG, "Response too short");
        return false;
    }

    int responseCmd = readInt32(response);
    int responseVal = readInt32(response + 4);

    if (responseCmd != cmd) {
        // Check for error codes. The code only exists when the device sent a
        // third word, so do not read it out of a short response.
        if (bytesRead >= 12) {
            int errorCode = readInt32(response + 8);
            if (errorCode < 0) {
                writeProtectionFail(errorCode);
            }
        }
        Log::error(TAG, "Unexpected response " + std::to_string(responseCmd) +
                   " for command " + std::to_string(cmd));
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

    char response[RESPONSE_PACKET_SIZE] = {0};
    int bytesRead = device_->read(response, sizeof(response), TRANSFER_TIMEOUT, false);

    if (bytesRead < 12) {
        return false;
    }

    if (received) {
        *received = readInt32(response + 4);
    }
    if (extra) {
        *extra = readInt32(response + 8);
    }

    return true;
}

bool DownloadEngine::sendData(const char* data, int size) {
    if (!device_ || !data || size <= 0) {
        Log::error(TAG, "Invalid data chunk");
        return false;
    }

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
    if (!device_ || !data || size <= 0) {
        return false;
    }

    int written = device_->write(data, size, TRANSFER_TIMEOUT);

    if (written != size) {
        return false;
    }

    char ack[64] = {0};
    int ackSize = device_->read(ack, 64, TRANSFER_TIMEOUT, false);

    return ackSize >= 8;
}

bool DownloadEngine::deviceInfoAnalysis(const char* data, size_t size) {
    if (!data || size < 8) {
        Log::error(TAG, "Device info too short");
        return false;
    }

    // Check magic number
    int32_t magic = readInt32(data);

    if (static_cast<uint32_t>(magic) != DEVINFO_MAGIC) {
        Log::error(TAG, "Invalid device info magic");
        return false;
    }

    // Parse device info structure
    int numEntries = readInt32(data + 4);

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
