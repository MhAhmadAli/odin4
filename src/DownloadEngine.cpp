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
constexpr int PIT_CHUNK_SIZE = 500;                       // PIT and device info dump in 500 byte blocks
constexpr int MAX_PIT_SIZE = 4 * 1024 * 1024;             // Sanity bound on a device-reported size
constexpr int MAX_DEVICE_INFO_SIZE = 1024 * 1024;

// Recovering from a reply we failed to collect. The device answers one command
// at a time, so a reply nobody read stays queued and every later exchange reads
// the previous command's answer instead of its own.
constexpr int RESYNC_TIMEOUT = 5000;                      // ms, waiting for the right reply
constexpr int FLUSH_TIMEOUT = 250;                        // ms, draining queued replies
constexpr int MAX_RESYNC_READS = 4;                       // stale replies skipped per exchange
constexpr int MAX_FLUSH_PACKETS = 8;
constexpr int HANDSHAKE_ATTEMPTS = 3;                     // retries after reading stale data
constexpr int OPTIONAL_CMD_TIMEOUT = 10000;               // ms; optional commands must not stall the flash

// File transfer framing. A file is sent as sequences of at most 30MB (Odin
// caps a sequence at 30 file parts of 1MB, or 240 parts of 128KB on protocol
// versions without packet size negotiation — the same 30MB either way), and
// every part on the wire is a full packetSize_ buffer, the last one
// zero-padded. Begin-sequence announces the padded length; end-of-sequence
// carries the real one.
constexpr size_t SEQUENCE_MAX_BYTES = 30 * 1024 * 1024;
constexpr int LEGACY_FLASH_PART_SIZE = 0x20000;           // 128KB, protocol v0/v1
constexpr int FLASH_TIMEOUT = 120000;                     // ms; the device erases/writes before ACKing
constexpr int ACK_READ_ATTEMPTS = 3;                      // empty reads tolerated per part ACK

// Session reply flag: device accepts LZ4 streams (0x66 sub-commands 5/6)
constexpr int SESSION_FLAG_COMPRESSION = 0x8000;

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
    , powerOffAfterDownload_(false)
    , poweredOff_(false)
    , hasPit_(false)
    , compressionSupported_(false)
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

    // A run that was aborted mid-exchange leaves its unread replies queued on
    // the endpoint; without this the handshake reads one of those instead of
    // LOKE.
    flushResponses();

    for (int attempt = 0; attempt < HANDSHAKE_ATTEMPTS; attempt++) {
        // Send "ODIN"
        const char odinStr[] = "ODIN";
        int written = device_->write(odinStr, 4, HANDSHAKE_TIMEOUT);

        if (written != 4) {
            Log::error(TAG, "Failed to send ODIN handshake");
            return false;
        }

        // Receive "LOKE"
        char response[64] = {0};
        int received = device_->read(response, sizeof(response), HANDSHAKE_TIMEOUT, false);

        if (received >= 4 && std::memcmp(response, "LOKE", 4) == 0) {
            Log::info(TAG, "Handshake successful (LOKE received)");
            return true;
        }

        if (received > 0) {
            // Something other than LOKE was queued ahead of our reply; drop
            // it and shake hands again.
            Log::info(TAG, "Discarding " + std::to_string(received) +
                      " unexpected handshake bytes");
            flushResponses();
            continue;
        }

        // Silence. The bootloader answers ODIN only once per download-mode
        // boot, so a run that already shook hands leaves nothing to read
        // here. Treat the session as resumable and let the session-begin
        // command decide: it gets a reply on a live session and a clear
        // failure otherwise.
        Log::info(TAG, "No handshake reply; assuming the session is already "
                  "open and resuming");
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

    compressionSupported_ = (sessionResult & SESSION_FLAG_COMPRESSION) != 0;

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
    } else {
        // No negotiation: the protocol still transfers files in 128KB parts,
        // not command-packet-sized ones.
        packetSize_ = LEGACY_FLASH_PART_SIZE;
    }
    
    // Announce the total about to be flashed (0x64, 2), as Odin and Thor do.
    // The value is a 64-bit integer on the wire; request() zero-fills the
    // packet, so for totals under 2GB the layout is already right. Optional:
    // the figure feeds the device's progress display, so a device that does
    // not acknowledge it is not a reason to abort.
    size_t totalBytes = 0;
    if (firmware_) {
        for (const auto& file : firmware_->getFiles()) {
            totalBytes += file.size;
        }
    }

    if (totalBytes > 0 && totalBytes <= static_cast<size_t>(INT32_MAX)) {
        if (requestAndResponse(static_cast<int>(ProtocolCmd::SessionControl),
                               static_cast<int>(SessionSubCmd::GetTotalBytes),
                               nullptr, static_cast<int>(totalBytes),
                               OPTIONAL_CMD_TIMEOUT)) {
            Log::info(TAG, "Total bytes: " + std::to_string(totalBytes));
        } else {
            Log::info(TAG, "Device did not acknowledge the total transfer size, continuing");
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

    // Request device info size (0x69, 0). The whole exchange is optional, so
    // wait a few seconds rather than TRANSFER_TIMEOUT on bootloaders that do
    // not implement 0x69 at all.
    if (!requestAndResponse(static_cast<int>(ProtocolCmd::DeviceInfo), 0,
                            &infoSize, 0, OPTIONAL_CMD_TIMEOUT)) {
        Log::error(TAG, "Failed to get device info size");
        return false;
    }
    
    if (infoSize <= 0 || infoSize > MAX_DEVICE_INFO_SIZE) {
        Log::error(TAG, "Invalid device info size: " + std::to_string(infoSize));
        return false;
    }

    Log::info(TAG, "Device info size: " + std::to_string(infoSize));

    // Like the PIT, the device info is dumped one fixed-size block at a time,
    // each block requested by its index (0x69, 1). Requesting it as a single
    // transfer left the device mid-send and deaf to the next command.
    int blockCount = (infoSize + PIT_CHUNK_SIZE - 1) / PIT_CHUNK_SIZE;
    std::vector<char> infoData(static_cast<size_t>(blockCount) * PIT_CHUNK_SIZE, 0);
    int received = 0;

    for (int block = 0; block < blockCount; block++) {
        if (!request(static_cast<int>(ProtocolCmd::DeviceInfo), 1, block)) {
            Log::error(TAG, "Failed to request device info block " + std::to_string(block));
            return false;
        }

        char* dst = infoData.data() + static_cast<size_t>(block) * PIT_CHUNK_SIZE;
        int blockRead = device_->read(dst, PIT_CHUNK_SIZE, OPTIONAL_CMD_TIMEOUT, false);

        if (blockRead <= 0) {
            Log::error(TAG, "Failed to receive device info block " + std::to_string(block));
            return false;
        }
        received += blockRead;
    }

    // End device info (0x69, 2)
    if (!requestAndResponse(static_cast<int>(ProtocolCmd::DeviceInfo), 2, nullptr, 0,
                            OPTIONAL_CMD_TIMEOUT)) {
        Log::error(TAG, "Failed to end device info");
        return false;
    }

    // Analyze device info
    if (!deviceInfoAnalysis(infoData.data(),
                            std::min(static_cast<size_t>(received),
                                     static_cast<size_t>(infoSize)))) {
        Log::error(TAG, "Failed to analyze device info");
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

    // Parse and keep the table: file transfer needs it to address the
    // end-of-sequence packets.
    if (pit_.parse(pitData.data(), pitData.size())) {
        hasPit_ = true;
        pit_.print();
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

    // The end-of-sequence packets must name the target partition, so a file
    // we cannot match against the device's PIT cannot be flashed.
    if (!hasPit_) {
        Log::error(TAG, "No PIT available; cannot address " + info.filename);
        return false;
    }

    const PITEntry* entry = pit_.findEntryByFilename(info.filename);
    if (!entry && info.filename.size() > 4 &&
        info.filename.compare(info.filename.size() - 4, 4, ".lz4") == 0) {
        // PIT names the uncompressed file: vbmeta.img.lz4 flashes vbmeta.img
        entry = pit_.findEntryByFilename(info.filename.substr(0, info.filename.size() - 4));
    }

    if (!entry) {
        Log::error(TAG, "No partition in the device PIT takes " + info.filename);
        return false;
    }

    Log::info(TAG, "Target partition: " + entry->partitionName +
              " (id " + std::to_string(entry->partitionId) + ")");

    bool compressed = (info.compression == CompressionType::LZ4);
    if (compressed && !compressionSupported_) {
        // Writing the LZ4 stream as if it were the image would corrupt the
        // partition; there is no safe fallback without decompressing locally.
        Log::error(TAG, "Device does not accept LZ4 streams; refusing to flash " +
                   info.filename);
        return false;
    }

    // File transfer start (0x66, 0; LZ4: 0x66, 5)
    int flashStatus = 0;
    if (!requestAndResponse(static_cast<int>(ProtocolCmd::FileTransfer),
                            compressed ? static_cast<int>(FileSubCmd::FlashCompressed)
                                       : static_cast<int>(FileSubCmd::Flash),
                            &flashStatus, 0, FLASH_TIMEOUT)) {
        Log::error(TAG, "Failed to start file transfer");
        return false;
    }

    if (flashStatus != 0) {
        writeProtectionFail(flashStatus);
        Log::error(TAG, "Device refused to start the transfer of " + info.filename +
                   " (status " + std::to_string(flashStatus) + ")");
        return false;
    }

    // The file goes over as sequences of at most SEQUENCE_MAX_BYTES, each one
    // begun with the padded length, sent as full packetSize_ parts, and ended
    // with a packet naming the real length and the target partition.
    std::vector<char> part(static_cast<size_t>(packetSize_));
    size_t offset = 0;
    size_t remaining = info.size;
    int lastReported = -1;

    while (remaining > 0) {
        size_t sequenceReal = std::min(remaining, SEQUENCE_MAX_BYTES);
        bool lastSequence = (sequenceReal == remaining);
        int partCount = static_cast<int>((sequenceReal + packetSize_ - 1) /
                                         static_cast<size_t>(packetSize_));
        size_t sequencePadded = static_cast<size_t>(partCount) *
                                static_cast<size_t>(packetSize_);

        // Begin sequence (0x66, 2; LZ4: 0x66, 6) with the padded length
        int sequenceStatus = 0;
        if (!requestAndResponse(static_cast<int>(ProtocolCmd::FileTransfer),
                                compressed ? static_cast<int>(FileSubCmd::PartCompressed)
                                           : static_cast<int>(FileSubCmd::Part),
                                &sequenceStatus, static_cast<int>(sequencePadded),
                                FLASH_TIMEOUT)) {
            Log::error(TAG, "Failed to begin sequence");
            return false;
        }

        if (sequenceStatus != 0) {
            writeProtectionFail(sequenceStatus);
            Log::error(TAG, "Device refused the sequence (status " +
                       std::to_string(sequenceStatus) + ")");
            return false;
        }

        size_t sequenceSent = 0;
        for (int partIndex = 0; partIndex < partCount; partIndex++) {
            size_t take = std::min(sequenceReal - sequenceSent,
                                   static_cast<size_t>(packetSize_));

            // Every part is a full packet; the device is told the real length
            // in the end-of-sequence packet. A short final write leaves the
            // device waiting and the part unacknowledged.
            std::memcpy(part.data(), data.get() + offset, take);
            if (take < static_cast<size_t>(packetSize_)) {
                std::memset(part.data() + take, 0,
                            static_cast<size_t>(packetSize_) - take);
            }

            if (!sendFilePart(part.data(), packetSize_, partIndex)) {
                Log::error(TAG, "Failed to send part " + std::to_string(partIndex) +
                           " of " + info.filename);
                return false;
            }

            sequenceSent += take;
            offset += take;
            remaining -= take;

            // Progress update, once per 10% step. Testing "progress % 10 == 0"
            // logged the same figure repeatedly and skipped steps entirely when
            // a chunk spanned more than one of them.
            int progress = static_cast<int>((offset * 100) / info.size);
            int step = progress / 10;
            if (step != lastReported) {
                lastReported = step;
                Log::info(TAG, "Progress: " + std::to_string(progress) + "%");
            }
        }

        // End sequence (0x66, 3) with the real length and the PIT entry
        if (!sendEndSequence(*entry, static_cast<int>(sequenceReal), lastSequence)) {
            Log::error(TAG, "Failed to end sequence for " + info.filename);
            return false;
        }
    }

    Log::info(TAG, "Transfer complete: " + info.filename);
    return true;
}

bool DownloadEngine::transmitCompressedData(const std::shared_ptr<char[]>& data,
                                             const FirmwareInfo& info) {
    // transmitData picks the LZ4 sub-commands from info.compression
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
    
    // 3. Get device info (optional; not every device implements 0x69). A
    //    failure part way through the exchange can leave a reply queued, so
    //    resynchronise before the PIT step rather than letting it inherit one.
    if (!getDeviceInfo()) {
        Log::info(TAG, "Continuing without device info");
        flushResponses();
    }
    
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
    
    // 7. End the session, with exactly one command. Reboot and power-off end
    //    it just as much as the plain close does, and sending the close first
    //    is not harmless: this bootloader rejects it once the last sequence
    //    carried the end-of-file flag, and then stops reading the endpoint
    //    altogether, so the reboot or power-off that followed never landed
    //    (a 0/1024 byte write).
    if (rebootAfterDownload_) {
        Log::info(TAG, "Rebooting into normal mode");
        if (!request(static_cast<int>(ProtocolCmd::Connection),
                     static_cast<int>(ConnSubCmd::Reboot))) {
            Log::error(TAG, "Failed to send the reboot command; the data is "
                       "flashed but the device must be restarted by hand");
        }
    } else if (powerOffAfterDownload_) {
        Log::info(TAG, "Powering the device off");
        if (request(static_cast<int>(ProtocolCmd::Connection),
                    static_cast<int>(ConnSubCmd::PowerOff))) {
            poweredOff_ = true;
        } else {
            Log::error(TAG, "Failed to send the power-off command; the data is "
                       "flashed but the device must be switched off by hand");
        }
    } else if (!closeConnection()) {
        Log::info(TAG, "Device refused the session close after a complete "
                  "transfer; continuing");
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

int DownloadEngine::readResponse(int cmd, char* response, size_t size, int firstTimeout) {
    if (!device_) {
        return -1;
    }

    // The device answers one command at a time. If an earlier exchange gave up
    // before its reply arrived, that reply is still queued and would be handed
    // to this command instead. Skip anything addressed to another command
    // rather than reporting it as this command's failure, which is what turned
    // a single missed reply into every following step failing.
    for (int attempt = 0; attempt < MAX_RESYNC_READS; attempt++) {
        std::memset(response, 0, size);

        int timeout = (attempt == 0) ? firstTimeout : RESYNC_TIMEOUT;
        int bytesRead = device_->read(response, size, timeout, false);

        if (bytesRead < 0) {
            Log::error(TAG, "USB read failed waiting for the reply to command " +
                       std::to_string(cmd));
            return -1;
        }

        if (bytesRead == 0) {
            // Either a zero-length packet — the device punctuates some phases
            // with those — or nothing arrived within the timeout. Both are
            // reasons to read again, not to fail the exchange.
            Log::info(TAG, "Empty read while waiting for the reply to command " +
                      std::to_string(cmd) + ", retrying");
            continue;
        }

        if (bytesRead < 8) {
            Log::error(TAG, "Response too short (" + std::to_string(bytesRead) +
                       " bytes) for command " + std::to_string(cmd));
            return -1;
        }

        int responseCmd = readInt32(response);
        if (responseCmd == cmd) {
            return bytesRead;
        }

        // Failure replies are typed 0xFF rather than echoing the command, so
        // this IS the answer — a rejection — not a stale packet to skip.
        if (responseCmd == -1 || responseCmd == 0xFF) {
            if (bytesRead >= 12) {
                int errorCode = readInt32(response + 8);
                if (errorCode < 0) {
                    writeProtectionFail(errorCode);
                }
            }
            Log::error(TAG, "Device rejected command " + std::to_string(cmd));
            return -1;
        }

        // Check for error codes. The code only exists when the device sent a
        // third word, so do not read it out of a short response.
        if (bytesRead >= 12) {
            int errorCode = readInt32(response + 8);
            if (errorCode < 0) {
                writeProtectionFail(errorCode);
            }
        }

        Log::error(TAG, "Unexpected response " + std::to_string(responseCmd) +
                   " for command " + std::to_string(cmd) + "; discarding it");
    }

    Log::error(TAG, "No usable reply to command " + std::to_string(cmd) +
               " after " + std::to_string(MAX_RESYNC_READS) + " reads");
    return -1;
}

void DownloadEngine::flushResponses() {
    if (!device_) {
        return;
    }

    char scratch[RESPONSE_PACKET_SIZE];
    for (int i = 0; i < MAX_FLUSH_PACKETS; i++) {
        int bytesRead = device_->read(scratch, sizeof(scratch), FLUSH_TIMEOUT, false);
        if (bytesRead <= 0) {
            return;
        }
        Log::info(TAG, "Discarded a stale " + std::to_string(bytesRead) +
                  " byte response left on the endpoint");
    }
}

bool DownloadEngine::requestAndResponse(int cmd, int subcmd, int* received, int expected,
                                        int timeout) {
    if (!request(cmd, subcmd, expected)) {
        return false;
    }

    if (timeout <= 0) {
        timeout = TRANSFER_TIMEOUT;
    }

    char response[RESPONSE_PACKET_SIZE];
    int bytesRead = readResponse(cmd, response, sizeof(response), timeout);

    if (bytesRead < 0) {
        // Leave the endpoint clean, otherwise a late reply becomes the next
        // command's answer.
        flushResponses();
        return false;
    }

    if (received) {
        *received = readInt32(response + 4);
    }

    return true;
}

bool DownloadEngine::sendFilePart(const char* data, int size, int expectedIndex) {
    if (!device_ || !data || size <= 0) {
        Log::error(TAG, "Invalid data chunk");
        return false;
    }

    int written = device_->write(data, size, TRANSFER_TIMEOUT);

    if (written != size) {
        Log::error(TAG, "Data write failed: " + std::to_string(written) + "/" + std::to_string(size));
        return false;
    }

    // The acknowledgement is {0x66, part index}. Empty reads are tolerated:
    // the device punctuates some phases with zero-length packets.
    for (int attempt = 0; attempt < ACK_READ_ATTEMPTS; attempt++) {
        char ack[64] = {0};
        int ackSize = device_->read(ack, sizeof(ack), FLASH_TIMEOUT, false);

        if (ackSize == 0) {
            continue;
        }

        if (ackSize < 8) {
            Log::error(TAG, "Part ACK read failed");
            return false;
        }

        int ackCmd = readInt32(ack);
        int ackIndex = readInt32(ack + 4);

        // Part ACKs are typed 0x00 (Heimdall: kResponseTypeSendFilePart), not
        // 0x66 like the command responses around them.
        if (ackCmd != 0) {
            Log::error(TAG, "Part ACK carries type " + std::to_string(ackCmd) +
                       " (expected 0)");
            return false;
        }

        // An index mismatch means the device and host disagree about how much
        // arrived; carrying on would flash data at the wrong offset.
        if (ackIndex != expectedIndex) {
            Log::error(TAG, "Device acknowledged part " + std::to_string(ackIndex) +
                       ", expected " + std::to_string(expectedIndex));
            return false;
        }

        return true;
    }

    Log::error(TAG, "No ACK for part " + std::to_string(expectedIndex));
    return false;
}

bool DownloadEngine::sendEndSequence(const PITEntry& entry, int sequenceRealSize,
                                     bool lastSequence) {
    // The end-of-sequence packet carries more fields than the generic
    // three-int request(), so it is built by hand. Layouts follow Thor and
    // Heimdall: the modem variant addresses the partition implicitly through
    // the device type, the AP variant by partition id.
    char packet[COMMAND_PACKET_SIZE] = {0};

    writeInt32(packet, static_cast<int>(ProtocolCmd::FileTransfer));
    writeInt32(packet + 4, static_cast<int>(FileSubCmd::End));

    if (entry.binaryType == PITBinaryType::CP) {
        writeInt32(packet + 8, 1);                                        // modem
        writeInt32(packet + 12, sequenceRealSize);
        writeInt32(packet + 16, static_cast<int>(entry.binaryType));
        writeInt32(packet + 20, static_cast<int>(entry.deviceType));
        writeInt32(packet + 24, lastSequence ? 1 : 0);
    } else {
        writeInt32(packet + 8, 0);                                        // phone
        writeInt32(packet + 12, sequenceRealSize);
        writeInt32(packet + 16, static_cast<int>(entry.binaryType));
        writeInt32(packet + 20, static_cast<int>(entry.deviceType));
        writeInt32(packet + 24, static_cast<int>(entry.partitionId));
        writeInt32(packet + 28, lastSequence ? 1 : 0);
        writeInt32(packet + 32, 0);                                       // EFS clear
        writeInt32(packet + 36, 0);                                       // bootloader update
    }

    Log::info(TAG, "End of sequence: partition " + entry.partitionName +
              " id=" + std::to_string(entry.partitionId) +
              " binaryType=" + std::to_string(static_cast<int>(entry.binaryType)) +
              " deviceType=" + std::to_string(static_cast<int>(entry.deviceType)) +
              " bytes=" + std::to_string(sequenceRealSize) +
              (lastSequence ? " (last)" : ""));

    int written = device_->write(packet, COMMAND_PACKET_SIZE, TRANSFER_TIMEOUT);
    if (written != COMMAND_PACKET_SIZE) {
        Log::error(TAG, "End-of-sequence write failed");
        return false;
    }

    // The device commits the sequence to flash before answering.
    char response[RESPONSE_PACKET_SIZE];
    int bytesRead = readResponse(static_cast<int>(ProtocolCmd::FileTransfer),
                                 response, sizeof(response), FLASH_TIMEOUT);
    if (bytesRead < 0) {
        flushResponses();
        return false;
    }

    // The status word decides whether the sequence was written or refused.
    // Ignoring it reported a successful flash for images the bootloader had
    // rejected outright — a locked bootloader turns away unsigned binaries
    // here, having accepted every byte of them.
    int status = readInt32(response + 4);
    if (status != 0) {
        writeProtectionFail(status);
        Log::error(TAG, "Device refused to write the sequence to " +
                   entry.partitionName + " (status " + std::to_string(status) + ")");
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

    // A locked bootloader refuses unsigned images at the point of writing
    // them, after happily accepting the transfer. The device says so on its
    // own screen ("Only official released binaries are allowed to be
    // flashed"); say it here too, rather than printing a bare number.
    if (code == -5) {
        Log::error(TAG, "The bootloader rejected this image as unsigned. Custom "
                   "binaries need OEM unlocking enabled and the bootloader "
                   "unlocked before they can be flashed.");
    }

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
