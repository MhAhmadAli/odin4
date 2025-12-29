/*
 * Odin4 - Samsung Firmware Flashing Tool for Linux
 * Log - Logging utilities with multi-device support
 */

#ifndef LOG_H
#define LOG_H

#include <string>
#include <functional>
#include <mutex>

namespace Odin {

class Log {
public:
    using Callback = std::function<void(const std::string&)>;
    
    // Log output
    static void print(const std::string& tag, const std::string& message);
    static void info(const std::string& tag, const std::string& message);
    static void error(const std::string& tag, const std::string& message);
    static void debug(const std::string& tag, const std::string& message);
    
    // Mode control
    static void setMultiDeviceMode(bool enabled);
    static void setInteractiveMode(bool enabled);
    static bool isMultiDeviceMode();
    static bool isInteractiveMode();
    
    // Callback for custom log handling
    static void setCallback(const Callback& callback);
    static Callback getCallback();
    
    // Device prefix for multi-device mode
    static void setDevicePrefix(const std::string& prefix);
    static std::string getDevicePrefix();
    
private:
    static void output(const std::string& message, bool isError = false);
    
    static bool mMultiDeviceMode;
    static bool mInteractiveMode;
    static Callback mCallback;
    static std::string mDevicePrefix;
    static std::mutex mMutex;
};

} // namespace Odin

#endif // LOG_H
