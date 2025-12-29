/*
 * Odin4 - Samsung Firmware Flashing Tool for Linux
 * UsbDeviceImpl - USB device implementation using libusb
 */

#include "UsbDevice.h"
#include "Log.h"
#include "OdinException.h"
#include <cstring>
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
    , interfaceClaimed_(false)
    , detachedDriver_(false)
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
    
    if (!device_) {
        // If no path match, try to find any Samsung device in download mode
        libusb_device** deviceList2 = nullptr;
        count = libusb_get_device_list(context_, &deviceList2);
        
        for (ssize_t i = 0; i < count; i++) {
            libusb_device_descriptor desc;
            if (libusb_get_device_descriptor(deviceList2[i], &desc) != LIBUSB_SUCCESS) {
                continue;
            }
            
            if (desc.idVendor == SAMSUNG_VID &&
                (desc.idProduct == SAMSUNG_PID_DOWNLOAD || 
                 desc.idProduct == SAMSUNG_PID_DOWNLOAD2)) {
                device_ = deviceList2[i];
                libusb_ref_device(device_);
                break;
            }
        }
        
        libusb_free_device_list(deviceList2, 1);
    }
    
    if (!device_) {
        Log::error(TAG, "Device not found");
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
    
    int transferred = 0;
    int result = libusb_bulk_transfer(handle_, outEndpoint_, 
                                       const_cast<unsigned char*>(
                                           reinterpret_cast<const unsigned char*>(data)),
                                       static_cast<int>(size), 
                                       &transferred, timeout);
    
    if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_TIMEOUT) {
        Log::error(TAG, "Write failed: " + std::to_string(result));
        return -1;
    }
    
    return transferred;
}

int UsbDeviceImpl::read(char* buffer, size_t size, unsigned int timeout, bool exactSize) {
    if (!handle_ || !buffer || size == 0) {
        return -1;
    }
    
    int transferred = 0;
    int result = libusb_bulk_transfer(handle_, inEndpoint_, 
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
    
    int result = libusb_claim_interface(handle_, interfaceNum);
    
#ifdef __linux__
    if (result != LIBUSB_SUCCESS) {
        Log::info(TAG, "Detaching kernel driver...");
        detachedDriver_ = true;
        libusb_detach_kernel_driver(handle_, interfaceNum);
        result = libusb_claim_interface(handle_, interfaceNum);
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
    }
#endif
    
    interfaceClaimed_ = false;
    return result;
}

uint8_t* UsbDeviceImpl::getNextDescriptor(uint8_t* start, uint8_t* end,
                                           uint8_t descriptorType, 
                                           uint8_t descriptorSubtype,
                                           void** context) {
    // Basic descriptor iteration
    uint8_t* ptr = start;
    while (ptr < end) {
        if (ptr[0] == 0) {
            break;
        }
        
        if (ptr[1] == descriptorType) {
            if (descriptorSubtype == 0 || ptr[2] == descriptorSubtype) {
                *context = ptr;
                return ptr + ptr[0];
            }
        }
        
        ptr += ptr[0];
    }
    
    return nullptr;
}

} // namespace Odin
