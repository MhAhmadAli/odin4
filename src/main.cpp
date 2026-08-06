/*
 * Odin4 - Samsung Firmware Flashing Tool for Linux
 * Main entry point
 * 
 * Reconstructed from decompiled binary
 * Version: 1.2.1-dc05e3ea
 */

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
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
constexpr const char* DEVELOPER = "MhAhmadAli";

// Forward declarations
void showLicenses();

void printUsage() {
    std::cout << "\n"
              << "Odin4 - Samsung Firmware Flashing Tool\n"
              << "Version: " << VERSION << "\n"
              << "Developer: " << DEVELOPER << "\n"
              << "========================================\n"
              << "\n"
              << "Usage: odin4 [options] ...\n"
              << "\n"
              << "Commands:\n"
              << "  -l                  List downloadable devices\n"
              << "  -h                  Show this help message\n"
              << "  -v                  Show version\n"
              << "  -w                  Show licenses\n"
              << "\n"
              << "Firmware Options:\n"
              << "  -b <file>           Add Bootloader (BL)\n"
              << "  -a <file>           Add AP (Android System)\n"
              << "  -c <file>           Add CP (Modem/Radio)\n"
              << "  -s <file>           Add CSC (Consumer Software Customization)\n"
              << "  -u <file>           Add UMS (User Mass Storage)\n"
              << "  -V <file>           Validate home binary with PIT file\n"
              << "\n"
              << "Flashing Options:\n"
              << "  -d <path>           Specify device path (auto-detects if omitted)\n"
              << "  -e                  Erase NAND before flashing\n"
              << "  --reboot            Reboot to normal mode after flashing\n"
              << "  --power-off         Power off after flashing, to boot a freshly\n"
              << "                      flashed recovery with the key combo\n"
              << "  --redownload        Reboot to download mode (if supported)\n"
              << "\n"
              << "----------------------------------------\n"
              << "Device Setup (Linux):\n"
              << "  1. Create udev rule: /etc/udev/rules.d/51-android.rules\n"
              << "     SUBSYSTEM==\"usb\", ATTR{idVendor}==\"04e8\", MODE=\"0666\", GROUP=\"plugdev\"\n"
              << "  2. Unload legacy module if needed:\n"
              << "     sudo rmmod cdc_acm\n"
              << "\n"
              << "Examples:\n"
              << "  # Flash full firmware:\n"
              << "  odin4 -b BL.tar -a AP.tar -c CP.tar -s CSC.tar\n"
              << "\n"
              << "  # List and select specific device:\n"
              << "  odin4 -l\n"
              << "  odin4 -b BL.tar -a AP.tar -d /dev/bus/usb/001/004\n"
              << "\n"
              << "  # Flash a custom recovery and power off, so the system does not\n"
              << "  # boot first and restore the stock one:\n"
              << "  odin4 -a twrp.tar --power-off\n"
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

void downloadThread(const std::string& devicePath,
                    FirmwareData firmware,
                    bool redownload,
                    bool reboot,
                    bool powerOff,
                    std::atomic<int>& successCount) {
    Log::setDevicePrefix(devicePath);

    DownloadEngine engine(devicePath, &firmware);
    engine.setRebootAfterDownload(reboot);
    engine.setPowerOffAfterDownload(powerOff);

    bool result;
    if (redownload) {
        result = engine.redownload();
    } else {
        result = engine.download();
    }

    if (result) {
        successCount++;
    }
}

int main(int argc, char** argv) {
    if (argc == 1) {
        std::cout << "Usage: odin4 -h" << std::endl;
        return 1;
    }

    // Development Warning
    std::cerr << "WARNING: This tool is for EDUCATIONAL PURPOSES ONLY and is NOT FULLY TESTED.\n"
              << "Use at your own risk. Incorrect usage may BRICK your device.\n"
              << std::endl;
    
    // Initialize
    std::vector<std::string> devicePaths;
    FirmwareData firmware;
    bool redownload = false;
    bool reboot = false;
    bool powerOff = false;
    bool haveFirmware = false;

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
        
        // Options that take a file or path argument. Reporting a missing
        // argument as an "illegal option" was actively misleading.
        if (arg == "-b" || arg == "-a" || arg == "-c" || arg == "-s" ||
            arg == "-u" || arg == "-V" || arg == "-d") {
            if (i + 1 >= argc) {
                std::cerr << "odin4: option " << arg << " requires an argument" << std::endl;
                return 1;
            }

            std::string value = argv[++i];
            bool ok = true;

            if (arg == "-b") {
                ok = firmware.setBootloader(value);
            } else if (arg == "-a") {
                ok = firmware.setAP(value);
            } else if (arg == "-c") {
                ok = firmware.setCP(value);
            } else if (arg == "-s") {
                ok = firmware.setCSC(value);
            } else if (arg == "-u") {
                ok = firmware.setUMS(value);
            } else if (arg == "-V") {
                ok = firmware.setPIT(value);
            } else {
                // -d may be repeated for multi-device flashing, but the same
                // device must not be opened twice.
                if (std::find(devicePaths.begin(), devicePaths.end(), value) ==
                    devicePaths.end()) {
                    devicePaths.push_back(value);
                } else {
                    std::cerr << "odin4: duplicate device " << value << ", ignoring"
                              << std::endl;
                }
            }

            if (!ok) {
                return 1;
            }

            if (arg != "-d") {
                haveFirmware = true;
            }
            continue;
        }

        if (arg == "-e") {
            firmware.setErase(true);
            continue;
        }

        if (arg == "--reboot") {
            std::cout << "Reboot into normal mode" << std::endl;
            reboot = true;
            continue;
        }

        if (arg == "--power-off") {
            std::cout << "Power off after flashing" << std::endl;
            powerOff = true;
            continue;
        }

        if (arg == "--redownload") {
            std::cout << "Reboot into download mode if it possible (not working in normal case)"
                      << std::endl;
            redownload = true;
            continue;
        }

        // Unknown option
        std::cerr << "odin4: illegal option " << arg << std::endl;
        return 1;
    }

    // The device can do one thing when the session ends, not two.
    if (reboot && powerOff) {
        std::cerr << "odin4: --reboot and --power-off are mutually exclusive" << std::endl;
        return 1;
    }

    // Refuse to talk to a device with nothing to do. Previously "odin4 -e"
    // opened a session, erased nothing and reported success.
    // --reboot, --power-off and --redownload are jobs in their own right.
    if (!haveFirmware && !redownload && !reboot && !powerOff) {
        std::cerr << "odin4: nothing to do (see odin4 -h)" << std::endl;
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
        engine.setRebootAfterDownload(reboot);
        engine.setPowerOffAfterDownload(powerOff);

        bool result;
        if (redownload) {
            result = engine.redownload();
        } else {
            result = engine.download();
        }

        if (result && powerOff && engine.poweredOff()) {
            std::cout << "\nDevice powered off. To boot a recovery you just flashed,\n"
                      << "hold the recovery key combo from cold rather than letting the\n"
                      << "system start: on most models Volume Up + Power (add Bixby on\n"
                      << "older ones), releasing Power when the logo appears.\n"
                      << std::endl;
        }

        return result ? 0 : 1;
    }
    
    // Multi-device mode
    Log::setMultiDeviceMode(true);
    Log::info("main", "Multi-device mode: " + std::to_string(devicePaths.size()) + " devices");
    
    std::vector<std::thread> threads;
    std::atomic<int> successCount(0);

    for (const auto& path : devicePaths) {
        threads.emplace_back(downloadThread, path, firmware, redownload, reboot,
                            powerOff, std::ref(successCount));
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
