/*
 * Odin4 - Samsung Firmware Flashing Tool for Linux
 * UsbDeviceImpl - USB device implementation using libusb
 */

#include "UsbDevice.h"
#include "Log.h"
#include "OdinException.h"
#include <cstring>
#include <cstdint>
#include <algorithm>

namespace Odin {

const std::string UsbDeviceImpl::TAG = "UsbDeviceImpl";

// Factory method
std::unique_ptr<UsbDevice> UsbDevice::create(const std::string& devicePath) {
    auto device = std::make_unique<UsbDeviceImpl>(devicePath);
    if (!device->isValid()) {
        return nullptr;
    }
    return device;
}

// List available Samsung devices in download mode
std::vector<DeviceInfo> UsbDevice::listDevices() {
    std::vector<DeviceInfo> devices;
    
    libusb_context* context = nullptr;
    int result = libusb_init(&context);
    if (result != LIBUSB_SUCCESS) {
        Log::error("UsbDevice", "Failed to initialize libusb: " + std::to_string(result));
        return devices;
    }
    
    libusb_device** deviceList = nullptr;
    ssize_t count = libusb_get_device_list(context, &deviceList);

    if (count < 0) {
        Log::error("UsbDevice", "Failed to enumerate USB devices: " + std::to_string(count));
        libusb_exit(context);
        return devices;
    }

    for (ssize_t i = 0; i < count; i++) {
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(deviceList[i], &desc) != LIBUSB_SUCCESS) {
            continue;
        }
        
        // Check for Samsung VID
        if (desc.idVendor != SAMSUNG_VID) {
            continue;
        }
        
        // Check for download mode PIDs
        if (desc.idProduct != SAMSUNG_PID_DOWNLOAD && 
            desc.idProduct != SAMSUNG_PID_DOWNLOAD2) {
            continue;
        }
        
        // Get device path
        uint8_t busNum = libusb_get_bus_number(deviceList[i]);
        uint8_t devAddr = libusb_get_device_address(deviceList[i]);
        
        DeviceInfo info;
        info.path = "/dev/bus/usb/" + 
                    std::to_string(busNum) + "/" + 
                    std::to_string(devAddr);
        info.vendorId = desc.idVendor;
        info.productId = desc.idProduct;
        
        // Try to get string descriptors
        libusb_device_handle* handle = nullptr;
        if (libusb_open(deviceList[i], &handle) == LIBUSB_SUCCESS) {
            unsigned char buffer[256];
            
            if (desc.iManufacturer && 
                libusb_get_string_descriptor_ascii(handle, desc.iManufacturer, 
                                                    buffer, sizeof(buffer)) > 0) {
                info.manufacturer = reinterpret_cast<char*>(buffer);
            }
            
            if (desc.iProduct && 
                libusb_get_string_descriptor_ascii(handle, desc.iProduct, 
                                                    buffer, sizeof(buffer)) > 0) {
                info.product = reinterpret_cast<char*>(buffer);
            }
            
            if (desc.iSerialNumber && 
                libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, 
                                                    buffer, sizeof(buffer)) > 0) {
                info.serialNumber = reinterpret_cast<char*>(buffer);
            }
            
            libusb_close(handle);
        }
        
        devices.push_back(info);
    }
    
    libusb_free_device_list(deviceList, 1);
    libusb_exit(context);
    
    return devices;
}

UsbDeviceImpl::UsbDeviceImpl(const std::string& devicePath)
    : context_(nullptr)
    , handle_(nullptr)
    , device_(nullptr)
    , inEndpoint_(-1)
    , outEndpoint_(-1)
    , interfaceIndex_(-1)
    , altSettingIndex_(-1)
    , valid_(false)
    , systemLSI_(false)
    , supportedZLP_(false)
    , zlpOutEnabled_(true)
    , interfaceClaimed_(false)
#ifdef __linux__
    , detachedDriver_(false)
#endif
{
    valid_ = initialize(devicePath);
}

UsbDeviceImpl::~UsbDeviceImpl() {
    if (interfaceClaimed_) {
        releaseInterface();
    }
    
    if (handle_) {
        libusb_close(handle_);
    }
    
    if (device_) {
        libusb_unref_device(device_);
    }
    
    if (context_) {
        libusb_exit(context_);
    }
}

bool UsbDeviceImpl::initialize(const std::string& devicePath) {
    Log::info(TAG, "Initializing USB device: " + devicePath);
    
    // Initialize libusb
    int result = libusb_init(&context_);
    if (result != LIBUSB_SUCCESS) {
        Log::error(TAG, "Failed to initialize libusb: " + std::to_string(result));
        return false;
    }
    
    // Find the device
    libusb_device** deviceList = nullptr;
    ssize_t count = libusb_get_device_list(context_, &deviceList);

    if (count < 0) {
        Log::error(TAG, "Failed to enumerate USB devices: " + std::to_string(count));
        return false;
    }

    for (ssize_t i = 0; i < count; i++) {
        uint8_t busNum = libusb_get_bus_number(deviceList[i]);
        uint8_t devAddr = libusb_get_device_address(deviceList[i]);

        std::string path = "/dev/bus/usb/" +
                           std::to_string(busNum) + "/" +
                           std::to_string(devAddr);

        if (path == devicePath) {
            device_ = deviceList[i];
            libusb_ref_device(device_);
            break;
        }
    }

    libusb_free_device_list(deviceList, 1);

    // Deliberately no fallback here. The caller always passes a concrete path,
    // so if that path is gone the right answer is to fail: silently picking
    // whatever other Samsung device happens to be plugged in would flash the
    // wrong phone, and in multi-device mode would point every thread at the
    // same one.
    if (!device_) {
        Log::error(TAG, "Device not found: " + devicePath);
        return false;
    }

    // Confirm this really is a Samsung device in download mode before we start
    // claiming interfaces on it.
    libusb_device_descriptor probeDesc;
    if (libusb_get_device_descriptor(device_, &probeDesc) != LIBUSB_SUCCESS) {
        Log::error(TAG, "Failed to read device descriptor for " + devicePath);
        return false;
    }

    if (probeDesc.idVendor != SAMSUNG_VID ||
        (probeDesc.idProduct != SAMSUNG_PID_DOWNLOAD &&
         probeDesc.idProduct != SAMSUNG_PID_DOWNLOAD2)) {
        Log::error(TAG, devicePath + " is not a Samsung device in download mode");
        return false;
    }

    // Open device
    result = libusb_open(device_, &handle_);
    if (result != LIBUSB_SUCCESS) {
        Log::error(TAG, "Failed to open device: " + std::to_string(result));
        return false;
    }
    
    // Get device descriptor
    libusb_device_descriptor deviceDesc;
    result = libusb_get_device_descriptor(device_, &deviceDesc);
    if (result != LIBUSB_SUCCESS) {
        Log::error(TAG, "Failed to get device descriptor");
        return false;
    }
    
    // Get config descriptor
    libusb_config_descriptor* configDesc = nullptr;
    result = libusb_get_config_descriptor(device_, 0, &configDesc);
    if (result != LIBUSB_SUCCESS || !configDesc) {
        Log::error(TAG, "Failed to get config descriptor");
        return false;
    }
    
    // Find the CDC DATA interface with bulk endpoints
    for (int i = 0; i < configDesc->bNumInterfaces; i++) {
        for (int j = 0; j < configDesc->interface[i].num_altsetting; j++) {
            const libusb_interface_descriptor* ifaceDesc = 
                &configDesc->interface[i].altsetting[j];
            
            if (ifaceDesc->bNumEndpoints != 2) {
                continue;
            }
            
            if (ifaceDesc->bInterfaceClass != USB_CLASS_CDC_DATA) {
                continue;
            }
            
            int tempIn = -1, tempOut = -1;
            
            for (int k = 0; k < ifaceDesc->bNumEndpoints; k++) {
                const libusb_endpoint_descriptor* epDesc = &ifaceDesc->endpoint[k];
                
                if (epDesc->bEndpointAddress & LIBUSB_ENDPOINT_IN) {
                    tempIn = epDesc->bEndpointAddress;
                } else {
                    tempOut = epDesc->bEndpointAddress;
                }
            }
            
            if (tempIn != -1 && tempOut != -1) {
                interfaceIndex_ = i;
                altSettingIndex_ = j;
                inEndpoint_ = tempIn;
                outEndpoint_ = tempOut;
                break;
            }
        }
        
        if (interfaceIndex_ != -1) {
            break;
        }
    }
    
    libusb_free_config_descriptor(configDesc);
    
    if (interfaceIndex_ < 0) {
        Log::error(TAG, "Failed to find suitable interface");
        return false;
    }
    
    Log::info(TAG, "Found interface " + std::to_string(interfaceIndex_) + 
              ", endpoints IN=" + std::to_string(inEndpoint_) + 
              " OUT=" + std::to_string(outEndpoint_));
    
    // Claim interface
    if (claimInterface(interfaceIndex_) != 0) {
        return false;
    }
    
    // Check product name
    checkProductName(deviceDesc.iProduct);
    
    return true;
}

void UsbDeviceImpl::checkProductName(uint8_t productIndex) {
    if (!productIndex) {
        return;
    }
    
    unsigned char buffer[256];
    int len = libusb_get_string_descriptor_ascii(handle_, productIndex, 
                                                  buffer, sizeof(buffer));
    if (len <= 0) {
        return;
    }
    
    std::string product(reinterpret_cast<char*>(buffer), len);
    Log::info(TAG, "Product: " + product);
    
    // Check for SystemLSI (Exynos)
    if (product.find("SAMSUNG") != std::string::npos ||
        product.find("LSI") != std::string::npos) {
        systemLSI_ = true;
    }
    
    // Newer devices support ZLP
    supportedZLP_ = true;
}

bool UsbDeviceImpl::isValid() const {
    return valid_;
}

bool UsbDeviceImpl::isSystemLSI() const {
    return systemLSI_;
}

bool UsbDeviceImpl::isSupportedZLP() const {
    return supportedZLP_;
}

int UsbDeviceImpl::write(const char* data, size_t size, unsigned int timeout) {
    if (!handle_ || !data || size == 0) {
        return -1;
    }
    
    if (size > static_cast<size_t>(INT32_MAX)) {
        Log::error(TAG, "Write size exceeds transfer limit");
        return -1;
    }

    int transferred = 0;
    int result = libusb_bulk_transfer(handle_, static_cast<unsigned char>(outEndpoint_),
                                       const_cast<unsigned char*>(
                                           reinterpret_cast<const unsigned char*>(data)),
                                       static_cast<int>(size),
                                       &transferred, timeout);

    if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_TIMEOUT) {
        Log::error(TAG, "Write failed: " + std::to_string(result));
        return -1;
    }

    // A timeout is reported as a short write rather than an error so callers
    // can see how far they got, but it should not pass unmentioned.
    if (result == LIBUSB_ERROR_TIMEOUT) {
        Log::error(TAG, "Write timed out after " + std::to_string(transferred) +
                   "/" + std::to_string(size) + " bytes");
    }

    // Terminate the transfer with a zero-length packet. The Odin protocol
    // expects one after every packet the host sends; without it, a bootloader
    // in large-packet mode does not consider a 1024-byte command (an exact
    // multiple of the 512-byte USB packet size) complete until the *next*
    // transfer arrives, which delays every reply by one command. Thor does the
    // same after every bulk write, and like Thor we stop after the first
    // rejection for bootloaders that do not want them.
    if (zlpOutEnabled_ && result == LIBUSB_SUCCESS) {
        unsigned char dummy = 0;
        int zlpTransferred = 0;
        int zlpResult = libusb_bulk_transfer(handle_,
                                             static_cast<unsigned char>(outEndpoint_),
                                             &dummy, 0, &zlpTransferred, ZLP_TIMEOUT);
        if (zlpResult != LIBUSB_SUCCESS) {
            Log::info(TAG, "Device rejected a zero-length packet (" +
                      std::to_string(zlpResult) + "); disabling them");
            zlpOutEnabled_ = false;
        }
    }

    return transferred;
}

int UsbDeviceImpl::read(char* buffer, size_t size, unsigned int timeout, bool exactSize) {
    if (!handle_ || !buffer || size == 0) {
        return -1;
    }
    
    if (size > static_cast<size_t>(INT32_MAX)) {
        Log::error(TAG, "Read size exceeds transfer limit");
        return -1;
    }

    int transferred = 0;
    int result = libusb_bulk_transfer(handle_, static_cast<unsigned char>(inEndpoint_),
                                       reinterpret_cast<unsigned char*>(buffer),
                                       static_cast<int>(size),
                                       &transferred, timeout);

    if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_TIMEOUT) {
        Log::error(TAG, "Read failed: " + std::to_string(result));
        return -1;
    }

    if (exactSize && transferred != static_cast<int>(size)) {
        Log::error(TAG, "Read size mismatch: expected " + std::to_string(size) + 
                   ", got " + std::to_string(transferred));
        return -1;
    }
    
    return transferred;
}

int UsbDeviceImpl::request(const char* data, size_t size) {
    return write(data, size, DEFAULT_TIMEOUT);
}

int UsbDeviceImpl::claimInterface(unsigned int interfaceNum) {
    Log::info(TAG, "Claiming interface " + std::to_string(interfaceNum));

    int result = libusb_claim_interface(handle_, static_cast<int>(interfaceNum));

#ifdef __linux__
    if (result != LIBUSB_SUCCESS) {
        // Only record a detach that actually happened, otherwise
        // releaseInterface() re-attaches a driver we never took away.
        if (libusb_kernel_driver_active(handle_, static_cast<int>(interfaceNum)) == 1) {
            Log::info(TAG, "Detaching kernel driver...");
            int detachResult = libusb_detach_kernel_driver(handle_,
                                                           static_cast<int>(interfaceNum));
            if (detachResult == LIBUSB_SUCCESS) {
                detachedDriver_ = true;
            } else {
                Log::error(TAG, "Failed to detach kernel driver: " +
                           std::to_string(detachResult));
            }
        }

        result = libusb_claim_interface(handle_, static_cast<int>(interfaceNum));
    }
#endif

    if (result != LIBUSB_SUCCESS) {
        Log::error(TAG, "Failed to claim interface: " + std::to_string(result));
        return result;
    }
    
    interfaceClaimed_ = true;
    
    // Set alt setting
    result = libusb_set_interface_alt_setting(handle_, interfaceIndex_, altSettingIndex_);
    if (result != LIBUSB_SUCCESS) {
        Log::error(TAG, "Failed to set alt setting: " + std::to_string(result));
        return result;
    }
    
    return 0;
}

int UsbDeviceImpl::releaseInterface() {
    if (!interfaceClaimed_) {
        return 0;
    }
    
    Log::info(TAG, "Releasing interface");
    
    int result = libusb_release_interface(handle_, interfaceIndex_);

#ifdef __linux__
    if (detachedDriver_) {
        Log::info(TAG, "Re-attaching kernel driver...");
        libusb_attach_kernel_driver(handle_, interfaceIndex_);
        detachedDriver_ = false;
    }
#endif

    interfaceClaimed_ = false;
    return result;
}

} // namespace Odin
