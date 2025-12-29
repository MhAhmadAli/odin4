/*
 * Odin4 - Samsung Firmware Flashing Tool for Linux
 * Log - Logging utilities implementation
 */

#include "Log.h"
#include <iostream>
#include <cstdio>

namespace Odin {

// Static member initialization
bool Log::mMultiDeviceMode = false;
bool Log::mInteractiveMode = false;
Log::Callback Log::mCallback = nullptr;
std::string Log::mDevicePrefix;
std::mutex Log::mMutex;

void Log::print(const std::string& tag, const std::string& message) {
    std::lock_guard<std::mutex> lock(mMutex);
    std::string formatted = "[" + tag + "] " + message;
    output(formatted);
}

void Log::info(const std::string& tag, const std::string& message) {
    std::lock_guard<std::mutex> lock(mMutex);
    std::string formatted = "[" + tag + "] " + message;
    output(formatted);
}

void Log::error(const std::string& tag, const std::string& message) {
    std::lock_guard<std::mutex> lock(mMutex);
    std::string formatted = "[" + tag + "] ERROR: " + message;
    output(formatted, true);
}

void Log::debug(const std::string& tag, const std::string& message) {
#ifdef DEBUG
    std::lock_guard<std::mutex> lock(mMutex);
    std::string formatted = "[" + tag + "] DEBUG: " + message;
    output(formatted);
#else
    (void)tag;
    (void)message;
#endif
}

void Log::setMultiDeviceMode(bool enabled) {
    mMultiDeviceMode = enabled;
}

void Log::setInteractiveMode(bool enabled) {
    mInteractiveMode = enabled;
}

bool Log::isMultiDeviceMode() {
    return mMultiDeviceMode;
}

bool Log::isInteractiveMode() {
    return mInteractiveMode;
}

void Log::setCallback(const Callback& callback) {
    std::lock_guard<std::mutex> lock(mMutex);
    mCallback = callback;
}

Log::Callback Log::getCallback() {
    std::lock_guard<std::mutex> lock(mMutex);
    return mCallback;
}

void Log::setDevicePrefix(const std::string& prefix) {
    std::lock_guard<std::mutex> lock(mMutex);
    mDevicePrefix = prefix;
}

std::string Log::getDevicePrefix() {
    std::lock_guard<std::mutex> lock(mMutex);
    return mDevicePrefix;
}

void Log::output(const std::string& message, bool isError) {
    std::string output;
    
    if (mMultiDeviceMode && !mDevicePrefix.empty()) {
        output = "<" + mDevicePrefix + "> " + message;
    } else {
        output = message;
    }
    
    // Call callback if registered
    if (mCallback) {
        mCallback(output);
    }
    
    // Output to console
    if (isError) {
        std::cerr << output << std::endl;
    } else {
        std::cout << output << std::endl;
    }
    
    // Flush in interactive mode
    if (mInteractiveMode) {
        std::cout.flush();
        std::cerr.flush();
    }
}

} // namespace Odin
