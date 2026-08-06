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
#include <cstdio>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <new>
#include <sys/stat.h>
#include <unistd.h>

// GZIP support
#include <zlib.h>

namespace Odin {

const std::string FirmwareData::TAG = "FirmwareData";

// A gzip member can legitimately wrap another archive, but a file that keeps
// decompressing to more gzip is either broken or hostile. Bound the nesting.
constexpr int MAX_GZIP_NESTING = 4;

// Size of the sniffing buffer used to detect the container type.
constexpr size_t HEADER_SNIFF_SIZE = 512;

// Return the file size, or -1 when it cannot be determined.
static long long fileSize(std::ifstream& file) {
    file.seekg(0, std::ios::end);
    std::streampos end = file.tellg();
    file.seekg(0, std::ios::beg);

    if (!file || end < 0) {
        return -1;
    }
    return static_cast<long long>(end);
}

// Allocate a buffer for firmware contents, reporting rather than throwing when
// the requested size is absurd or the allocation fails.
static std::shared_ptr<char[]> allocateBuffer(size_t size) {
    return std::shared_ptr<char[]>(new (std::nothrow) char[size]);
}

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
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        Log::error(TAG, "Cannot open PIT file: " + path);
        return false;
    }

    long long size = fileSize(file);
    if (size < 1) {
        Log::error(TAG, "Invalid PIT file size");
        return false;
    }

    pitSize_ = static_cast<size_t>(size);

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

    // Check file extension. find_last_of must not run past a directory
    // separator, otherwise "archives.d/firmware" is read as extension
    // "d/firmware".
    std::string ext;
    size_t slashPos = path.find_last_of('/');
    size_t dotPos = path.find_last_of('.');
    if (dotPos != std::string::npos &&
        (slashPos == std::string::npos || dotPos > slashPos)) {
        ext = path.substr(dotPos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
    }

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
    return parseBinaryInternal(path, 0);
}

bool FirmwareData::parseBinaryInternal(const std::string& path, int gzipDepth) {
    // Determine file type
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        Log::error(TAG, "Cannot open file: " + path);
        return false;
    }

    // Read header to detect file type. Zero it first and track how much was
    // actually read: a file shorter than the buffer used to leave the rest
    // uninitialised, and the magic checks below then read that garbage.
    char header[HEADER_SNIFF_SIZE] = {0};
    file.read(header, sizeof(header));
    size_t headerLen = static_cast<size_t>(file.gcount());
    file.close();

    if (headerLen == 0) {
        Log::error(TAG, "File is empty: " + path);
        return false;
    }

    // Check for GZIP magic
    if (headerLen >= 2 &&
        static_cast<uint8_t>(header[0]) == 0x1F &&
        static_cast<uint8_t>(header[1]) == 0x8B) {
        Log::info(TAG, "Detected GZIP file");

        if (gzipDepth >= MAX_GZIP_NESTING) {
            Log::error(TAG, "Refusing to decompress more than " +
                       std::to_string(MAX_GZIP_NESTING) + " nested GZIP layers");
            return false;
        }

        // Extract to a private temp file and parse that
        std::string tempPath;
        if (!extractGzipFile(path, tempPath)) {
            return false;
        }

        bool result = parseBinaryInternal(tempPath, gzipDepth + 1);
        ::unlink(tempPath.c_str());
        return result;
    }

    // Check for LZ4 magic
    uint32_t magic = 0;
    if (headerLen >= sizeof(magic)) {
        std::memcpy(&magic, header, sizeof(magic));
    }

    if (magic == LZ4_MAGIC) {
        Log::info(TAG, "Detected LZ4 file");
        // LZ4 files can be streamed directly to the device

        FirmwareInfo info;
        info.filename = baseName(path);
        info.compression = CompressionType::LZ4;

        // Parse LZ4 frame header
        parseLZ4FrameHeader(header, headerLen, info);

        // Read file into memory
        std::ifstream lz4File(path, std::ios::binary);
        long long size = fileSize(lz4File);
        if (size <= 0) {
            Log::error(TAG, "Cannot determine size of: " + path);
            return false;
        }
        info.size = static_cast<size_t>(size);

        info.data = allocateBuffer(info.size);
        if (!info.data) {
            Log::error(TAG, "Out of memory reading " + path +
                       " (" + std::to_string(info.size) + " bytes)");
            return false;
        }

        lz4File.read(info.data.get(), static_cast<std::streamsize>(info.size));
        if (static_cast<size_t>(lz4File.gcount()) != info.size) {
            Log::error(TAG, "Short read on: " + path);
            return false;
        }

        files_.push_back(info);
        return true;
    }

    // Check for TAR magic (at offset 257)
    if (headerLen >= 262 && memcmp(header + 257, "ustar", 5) == 0) {
        Log::info(TAG, "Detected TAR file");
        return parseTAR(path, FirmwareType::Unknown);
    }

    // Assume binary file
    Log::info(TAG, "Parsing as binary file");
    return parseBIN(path, FirmwareType::Unknown);
}

std::string FirmwareData::baseName(const std::string& path) {
    size_t slashPos = path.find_last_of('/');
    return (slashPos == std::string::npos) ? path : path.substr(slashPos + 1);
}

bool FirmwareData::parseTAR(const std::string& path, FirmwareType type) {
    Tar tar(path);
    
    if (!tar.open()) {
        Log::error(TAG, "Failed to open TAR: " + path);
        return false;
    }
    
    const auto& entries = tar.getEntries();
    Log::info(TAG, "TAR contains " + std::to_string(entries.size()) + " entries");

    size_t filesBefore = files_.size();

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
        info.data = allocateBuffer(entry.size);
        if (!info.data) {
            Log::error(TAG, "Out of memory reading entry " + entry.name +
                       " (" + std::to_string(entry.size) + " bytes)");
            return false;
        }

        if (!tar.readEntry(entry, info.data.get(), entry.size)) {
            Log::error(TAG, "Failed to read entry: " + entry.name);
            continue;
        }

        // Check for LZ4 compression in the data
        uint32_t magic = 0;
        if (entry.size >= sizeof(magic)) {
            std::memcpy(&magic, info.data.get(), sizeof(magic));
        }

        if (magic == LZ4_MAGIC) {
            info.compression = CompressionType::LZ4;
            parseLZ4FrameHeader(info.data.get(), entry.size, info);
        }

        files_.push_back(info);
    }

    tar.close();

    if (files_.size() == filesBefore) {
        Log::error(TAG, "TAR contains no flashable files: " + path);
        return false;
    }

    return true;
}

bool FirmwareData::parseBIN(const std::string& path, FirmwareType type) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        Log::error(TAG, "Cannot open file: " + path);
        return false;
    }

    long long size = fileSize(file);
    if (size <= 0) {
        Log::error(TAG, "Cannot determine size of: " + path);
        return false;
    }

    FirmwareInfo info;
    info.filename = baseName(path);
    info.size = static_cast<size_t>(size);
    info.offset = 0;
    info.type = type;
    info.compression = CompressionType::None;

    // Determine partition name from filename
    size_t dotPos = info.filename.find_last_of('.');
    info.partitionName = info.filename.substr(0, dotPos);

    // Read file data
    info.data = allocateBuffer(info.size);
    if (!info.data) {
        Log::error(TAG, "Out of memory reading " + path +
                   " (" + std::to_string(info.size) + " bytes)");
        return false;
    }

    file.read(info.data.get(), static_cast<std::streamsize>(info.size));
    if (static_cast<size_t>(file.gcount()) != info.size) {
        Log::error(TAG, "Short read on: " + path);
        return false;
    }

    // Check for LZ4 compression
    uint32_t magic = 0;
    if (info.size >= sizeof(magic)) {
        std::memcpy(&magic, info.data.get(), sizeof(magic));
    }

    if (magic == LZ4_MAGIC) {
        info.compression = CompressionType::LZ4;
        parseLZ4FrameHeader(info.data.get(), info.size, info);
    }

    files_.push_back(info);
    return true;
}

bool FirmwareData::verifyMD5(const std::string& path) {
    Log::info(TAG, "Verifying MD5...");

    // A Samsung .tar.md5 is a plain tar with the output of md5sum appended:
    //
    //     <32 hex digits>  <filename>\n
    //
    // The tar itself is always a whole number of 512 byte blocks, so the
    // trailing line starts at the last block boundary. Everything before that
    // point is what the recorded digest covers.
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        Log::error(TAG, "Cannot open file: " + path);
        return false;
    }

    long long size = fileSize(file);
    if (size <= 0) {
        Log::error(TAG, "Cannot determine size of: " + path);
        return false;
    }

    size_t contentSize = (static_cast<size_t>(size) / 512) * 512;
    std::string expectedMD5;

    if (contentSize > 0 && contentSize < static_cast<size_t>(size)) {
        size_t trailerSize = static_cast<size_t>(size) - contentSize;
        std::string trailer(trailerSize, '\0');

        file.seekg(static_cast<std::streamoff>(contentSize));
        file.read(&trailer[0], static_cast<std::streamsize>(trailerSize));

        if (static_cast<size_t>(file.gcount()) == trailerSize) {
            expectedMD5 = extractTrailingMD5(trailer);
        }
    }
    file.close();

    if (expectedMD5.empty()) {
        // Not every file named *.md5 actually carries a trailer. Say so
        // instead of silently reporting a verification that never happened.
        Log::info(TAG, "No embedded MD5 checksum found in " + path + ", skipping verification");
        return true;
    }

    // Calculate the actual MD5 over the archive portion only
    std::string actualMD5 = Manifest::calculateMD5(path, contentSize);

    if (actualMD5.empty()) {
        Log::error(TAG, "Failed to calculate MD5");
        return false;
    }

    if (actualMD5 != expectedMD5) {
        Log::error(TAG, "MD5 mismatch: expected " + expectedMD5 + ", got " + actualMD5);
        return false;
    }

    Log::info(TAG, "MD5 OK: " + actualMD5);
    return true;
}

// Pull the lower-case digest out of an "md5sum" style trailer, or return an
// empty string when the trailer is not in that form.
std::string FirmwareData::extractTrailingMD5(const std::string& trailer) {
    size_t start = 0;
    while (start < trailer.size() &&
           std::isspace(static_cast<unsigned char>(trailer[start]))) {
        start++;
    }

    if (trailer.size() - start < 32) {
        return "";
    }

    std::string digest = trailer.substr(start, 32);
    for (char c : digest) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return "";
        }
    }

    // The digest must be followed by whitespace (or nothing at all), otherwise
    // we matched something that merely began with hex characters.
    if (trailer.size() > start + 32 &&
        !std::isspace(static_cast<unsigned char>(trailer[start + 32]))) {
        return "";
    }

    std::transform(digest.begin(), digest.end(), digest.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return digest;
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

bool FirmwareData::extractGzipFile(const std::string& src, std::string& dst) {
    Log::info(TAG, "Extracting GZIP: " + src);

    gzFile gz = gzopen(src.c_str(), "rb");
    if (!gz) {
        Log::error(TAG, "Failed to open GZIP file");
        return false;
    }

    // Create the scratch file with mkstemp rather than a fixed name under
    // /tmp: a predictable path is both a symlink target for anyone else on the
    // machine and a collision between concurrent runs.
    const char* tmpDir = ::getenv("TMPDIR");
    std::string tmpTemplate = std::string(tmpDir && *tmpDir ? tmpDir : "/tmp") +
                              "/odin4_extracted_XXXXXX";
    std::vector<char> nameBuffer(tmpTemplate.begin(), tmpTemplate.end());
    nameBuffer.push_back('\0');

    int fd = ::mkstemp(nameBuffer.data());
    if (fd < 0) {
        Log::error(TAG, "Failed to create temporary file");
        gzclose(gz);
        return false;
    }

    dst = nameBuffer.data();

    FILE* out = ::fdopen(fd, "wb");
    if (!out) {
        Log::error(TAG, "Failed to create output file");
        ::close(fd);
        ::unlink(dst.c_str());
        gzclose(gz);
        return false;
    }

    char buffer[65536];
    int bytesRead;
    bool ok = true;

    while ((bytesRead = gzread(gz, buffer, sizeof(buffer))) > 0) {
        if (std::fwrite(buffer, 1, static_cast<size_t>(bytesRead), out) !=
            static_cast<size_t>(bytesRead)) {
            Log::error(TAG, "Failed to write to temporary file");
            ok = false;
            break;
        }
    }

    if (bytesRead < 0) {
        int errnum = 0;
        const char* message = gzerror(gz, &errnum);
        Log::error(TAG, std::string("GZIP decompression failed: ") +
                   (message ? message : "unknown error"));
        ok = false;
    }

    std::fclose(out);
    gzclose(gz);

    if (!ok) {
        ::unlink(dst.c_str());
        dst.clear();
        return false;
    }

    Log::info(TAG, "Extraction complete: " + dst);
    return true;
}

bool FirmwareData::parseLZ4FrameHeader(const char* data, size_t size, FirmwareInfo& info) {
    // LZ4 frame format:
    // [4 bytes] Magic = 0x184D2204
    // [1 byte]  FLG byte
    // [1 byte]  BD byte
    // [0-8 bytes] Optional content size
    // [1 byte]  Header checksum
    //
    // Every read below is bounds checked; the caller may only have a partial
    // header (a short file, or the first block of a sniffing buffer).
    if (!data || size < 6) {
        return false;
    }

    uint32_t magic = 0;
    std::memcpy(&magic, data, sizeof(magic));
    if (magic != LZ4_MAGIC) {
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
        if (size < 6 + sizeof(uint64_t)) {
            Log::error(TAG, "Truncated LZ4 frame header");
            return false;
        }

        uint64_t uncompressed = 0;
        std::memcpy(&uncompressed, data + 6, sizeof(uncompressed));
        info.uncompressedSize = static_cast<size_t>(uncompressed);
        Log::info(TAG, "LZ4 uncompressed size: " + std::to_string(info.uncompressedSize));
    }

    return true;
}

} // namespace Odin
