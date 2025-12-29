/*
 * Odin4 - Samsung Firmware Flashing Tool for Linux
 * Main entry point
 * 
 * Reconstructed from decompiled binary
 * Version: 1.2.1-dc05e3ea
 */

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstring>
#include <unistd.h>

#include "DownloadEngine.h"
#include "FirmwareData.h"
#include "UsbDevice.h"
#include "Log.h"
#include "OdinException.h"

using namespace Odin;

// Version info
constexpr const char* VERSION = "1.2.1-dc05e3ea";

// Forward declarations
void showLicenses();

void printUsage() {
    std::cout << "Usage: odin4 [args...]\n"
              << "Odin4 downloader. odin4 version " << VERSION << "\n"
              << " -v        Show version\n"
              << " -w        Show License\n"
              << " -b        Add Bootloader file\n"
              << " -a        Add AP image file\n"
              << " -c        Add CP image file\n"
              << " -s        Add CSC file\n"
              << " -u        Add UMS file\n"
              << " -e        Set Nand erase option\n"
              << " -V        Home binary validation check with pit file\n"
              << " --reboot  Reboot into normal mode\n"
              << " --redownload   Reboot into download mode if it possible\n"
              << " -d        Set a device path (detect automatically without this option)\n"
              << " -l        Show downloadable devices path\n"
              << "\n"
              << "IMPORTANT: You must set up your system to detect your device on LINUX host.\n"
              << " create this file: /etc/udev/rules.d/51-android.rules\n"
              << " to add a line to the file:\n"
              << " SUBSYSTEM==\"usb\", ATTR{idVendor}==\"04e8\", MODE=\"0666\", GROUP=\"plugdev\"\n"
              << "   (http://developer.android.com/tools/device.html)\n"
              << " And you maybe need to unload a module cdc_acm before downloading.\n"
              << "   $sudo rmmod cdc_acm\n"
              << " OR\n"
              << "   echo \"blacklist cdc_acm\" > /etc/modprobe.d/cdc_acm-blacklist.conf\n"
              << "\n"
              << "Example:\n"
              << "$odin4 -b BL_XXXX.tar.md5 -a AP_XXXX.tar.md5 -c CP_XXXX.tar.md5 -s CSC_XXXX.tar.md5\n"
              << "Example (Select One Device):\n"
              << "$odin4 -l\n"
              << "PATH_OF_DEVICE_A\n"
              << "PATH_OF_DEVICE_B\n"
              << "$odin4 -b BL_XXXX.tar.md5 -a AP_XXXX.tar.md5 -c CP_XXXX.tar.md5 -s CSC_XXXX.tar.md5 -d PATH_OF_DEVICE_A\n"
              << "\n";
}

std::vector<std::string> listDevices() {
    auto devices = UsbDevice::listDevices();
    std::vector<std::string> paths;
    
    for (const auto& dev : devices) {
        std::cout << dev.path << std::endl;
        paths.push_back(dev.path);
    }
    
    return paths;
}

struct ThreadResult {
    bool success;
    std::string devicePath;
};

void downloadThread(const std::string& devicePath, 
                    FirmwareData firmware,
                    bool redownload,
                    std::atomic<int>& successCount,
                    std::mutex& mutex) {
    Log::setDevicePrefix(devicePath);
    
    DownloadEngine engine(devicePath, &firmware);
    
    bool result;
    if (redownload) {
        result = engine.redownload();
    } else {
        result = engine.download();
    }
    
    if (result) {
        std::lock_guard<std::mutex> lock(mutex);
        successCount++;
    }
}

int main(int argc, char** argv) {
    if (argc == 1) {
        std::cout << "Usage: odin4 -h" << std::endl;
        return 1;
    }

    // Development Warning
    std::cerr << "WARNING: This tool is under development and NOT FULLY TESTED.\n"
              << "Use at your own risk. Incorrect usage may BRICK your device.\n"
              << std::endl;
    
    // Initialize
    std::vector<std::string> devicePaths;
    FirmwareData firmware;
    bool redownload = false;
    
    // Check if stdin is a terminal
    bool isInteractive = isatty(fileno(stdin)) != 0;
    Log::setInteractiveMode(isInteractive);
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage();
            return 0;
        }
        
        if (arg == "-v") {
            std::cout << "odin4 version " << VERSION << std::endl;
            return 0;
        }
        
        if (arg == "-w") {
            showLicenses();
            return 0;
        }
        
        if (arg == "-l") {
            listDevices();
            return 0;
        }
        
        if (arg == "-b" && i + 1 < argc) {
            if (!firmware.setBootloader(argv[++i])) {
                return 1;
            }
            continue;
        }
        
        if (arg == "-a" && i + 1 < argc) {
            if (!firmware.setAP(argv[++i])) {
                return 1;
            }
            continue;
        }
        
        if (arg == "-c" && i + 1 < argc) {
            if (!firmware.setCP(argv[++i])) {
                return 1;
            }
            continue;
        }
        
        if (arg == "-s" && i + 1 < argc) {
            if (!firmware.setCSC(argv[++i])) {
                return 1;
            }
            continue;
        }
        
        if (arg == "-u" && i + 1 < argc) {
            if (!firmware.setUMS(argv[++i])) {
                return 1;
            }
            continue;
        }
        
        if (arg == "-V" && i + 1 < argc) {
            if (!firmware.setPIT(argv[++i])) {
                return 1;
            }
            continue;
        }
        
        if (arg == "-e") {
            firmware.setErase(true);
            continue;
        }
        
        if (arg == "-d" && i + 1 < argc) {
            devicePaths.push_back(argv[++i]);
            continue;
        }
        
        if (arg == "--reboot") {
            std::cout << "Reboot into normal mode" << std::endl;
            continue;
        }
        
        if (arg == "--redownload") {
            std::cout << "Reboot into download mode if it possible (not working in normal case)" 
                      << std::endl;
            redownload = true;
            continue;
        }
        
        // Unknown option
        std::cout << "odin4: illegal option " << arg << std::endl;
        return 1;
    }
    
    // Auto-detect devices if none specified
    if (devicePaths.empty()) {
        auto devices = UsbDevice::listDevices();
        for (const auto& dev : devices) {
            devicePaths.push_back(dev.path);
        }
        
        if (devicePaths.empty()) {
            Log::error("main", "cannot find device");
            return 1;
        }
    }
    
    // Single device mode
    if (devicePaths.size() == 1) {
        Log::info("main", "Starting download on: " + devicePaths[0]);
        
        DownloadEngine engine(devicePaths[0], &firmware);
        
        bool result;
        if (redownload) {
            result = engine.redownload();
        } else {
            result = engine.download();
        }
        
        return result ? 0 : 1;
    }
    
    // Multi-device mode
    Log::setMultiDeviceMode(true);
    Log::info("main", "Multi-device mode: " + std::to_string(devicePaths.size()) + " devices");
    
    std::vector<std::thread> threads;
    std::atomic<int> successCount(0);
    std::mutex mutex;
    
    for (const auto& path : devicePaths) {
        threads.emplace_back(downloadThread, path, firmware, redownload,
                            std::ref(successCount), std::ref(mutex));
    }
    
    // Wait for all threads
    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }
    
    int total = static_cast<int>(devicePaths.size());
    int failed = total - successCount.load();
    
    Log::info("main", "All threads completed. (succeed " + std::to_string(successCount.load()) + 
              " / failed " + std::to_string(failed) + ")");
    
    return (successCount.load() == total) ? 0 : 1;
}
