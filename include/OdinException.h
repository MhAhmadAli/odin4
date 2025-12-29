/*
 * Odin4 - Samsung Firmware Flashing Tool for Linux
 * Copyright (c) 2024
 *
 * Reconstructed from decompiled binary with reference to:
 * - Heimdall (MIT License) by Benjamin Dobell
 *
 * This is free software under the MIT License.
 */

#ifndef ODIN_EXCEPTION_H
#define ODIN_EXCEPTION_H

#include <exception>
#include <string>

namespace Odin {

class OdinException : public std::exception {
public:
    explicit OdinException(const std::string& message) : message_(message) {}
    explicit OdinException(const char* message) : message_(message) {}
    
    const char* what() const noexcept override {
        return message_.c_str();
    }
    
private:
    std::string message_;
};

class UsbException : public OdinException {
public:
    explicit UsbException(const std::string& message) : OdinException(message) {}
};

class FirmwareException : public OdinException {
public:
    explicit FirmwareException(const std::string& message) : OdinException(message) {}
};

class ProtocolException : public OdinException {
public:
    explicit ProtocolException(const std::string& message) : OdinException(message) {}
};

} // namespace Odin

#endif // ODIN_EXCEPTION_H
