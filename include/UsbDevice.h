/*
 * Odin4 - Samsung Firmware Flashing Tool for Linux
 * UsbDevice - USB device interface and implementation
 */

#ifndef USB_DEVICE_H
#define USB_DEVICE_H

#include <string>
#include <vector>
#include <memory>
#include <libusb-1.0/libusb.h>

namespace Odin {

// Samsung USB identifiers
constexpr uint16_t SAMSUNG_VID = 0x04E8;
constexpr uint16_t SAMSUNG_PID_DOWNLOAD = 0x6601;  // Download mode
constexpr uint16_t SAMSUNG_PID_DOWNLOAD2 = 0x685D; // Alternate download mode

// USB class for CDC DATA
constexpr uint8_t USB_CLASS_CDC_DATA = 0x0A;

// Default packet sizes
constexpr int DEFAULT_PACKET_SIZE = 0x400;       // 1KB
constexpr int MAX_PACKET_SIZE = 0x100000;        // 1MB

// Timeout values (milliseconds)
constexpr int DEFAULT_TIMEOUT = 30000;           // 30 seconds
constexpr int HANDSHAKE_TIMEOUT = 1000;          // 1 second
constexpr int TRANSFER_TIMEOUT = 60000;          // 60 seconds

struct DeviceInfo {
    std::string path;
    std::string manufacturer;
    std::string product;
    std::string serialNumber;
    uint16_t vendorId;
    uint16_t productId;
};

class UsbDevice {
public:
    virtual ~UsbDevice() = default;
    
    // Connection management
    virtual bool isValid() const = 0;
    virtual bool isSystemLSI() const = 0;
    virtual bool isSupportedZLP() const = 0;
    
    // Data transfer
    virtual int write(const char* data, size_t size, unsigned int timeout = DEFAULT_TIMEOUT) = 0;
    virtual int read(char* buffer, size_t size, unsigned int timeout = DEFAULT_TIMEOUT, bool exactSize = false) = 0;
    virtual int request(const char* data, size_t size) = 0;
    
    // Interface management
    virtual int claimInterface(unsigned int interfaceNum) = 0;
    virtual int releaseInterface() = 0;
    
    // Factory method
    static std::unique_ptr<UsbDevice> create(const std::string& devicePath);
    
    // Device enumeration
    static std::vector<DeviceInfo> listDevices();
};

class UsbDeviceImpl : public UsbDevice {
public:
    static const std::string TAG;
    
    explicit UsbDeviceImpl(const std::string& devicePath);
    ~UsbDeviceImpl() override;
    
    // Non-copyable
    UsbDeviceImpl(const UsbDeviceImpl&) = delete;
    UsbDeviceImpl& operator=(const UsbDeviceImpl&) = delete;
    
    // Connection management
    bool isValid() const override;
    bool isSystemLSI() const override;
    bool isSupportedZLP() const override;
    
    // Data transfer
    int write(const char* data, size_t size, unsigned int timeout = DEFAULT_TIMEOUT) override;
    int read(char* buffer, size_t size, unsigned int timeout = DEFAULT_TIMEOUT, bool exactSize = false) override;
    int request(const char* data, size_t size) override;
    
    // Interface management
    int claimInterface(unsigned int interfaceNum) override;
    int releaseInterface() override;
    
private:
    bool initialize(const std::string& devicePath);
    void checkProductName(uint8_t productIndex);
    uint8_t* getNextDescriptor(uint8_t* start, uint8_t* end, 
                                uint8_t descriptorType, uint8_t descriptorSubtype,
                                void** context);
    
    libusb_context* context_;
    libusb_device_handle* handle_;
    libusb_device* device_;
    
    int inEndpoint_;
    int outEndpoint_;
    int interfaceIndex_;
    int altSettingIndex_;
    
    bool valid_;
    bool systemLSI_;
    bool supportedZLP_;
    bool interfaceClaimed_;
    bool detachedDriver_;
};

} // namespace Odin

#endif // USB_DEVICE_H
