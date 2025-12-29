# Odin4 - Samsung Firmware Flashing Tool

A reconstructed version of Samsung's Odin4 firmware flashing tool, based on:
- Decompiled binary analysis (Ghidra)
- Heimdall open-source reference (MIT License)

## Version
1.2.1-dc05e3ea

## Features

- Flash firmware to Samsung devices in Download Mode
- Support for multiple file types:
  - `.tar.md5` - TAR archives with MD5 checksum
  - `.lz4` - LZ4 compressed files
  - `.gz` - GZIP compressed files
  - `.bin` - Raw binary files
- Multi-device flashing support (parallel)
- PIT (Partition Information Table) handling
- MD5/SHA256 verification

## Requirements

### macOS

```bash
brew install libusb lz4 openssl
# Optional for better crypto support:
brew install cryptopp pkg-config
```

### Linux (Debian/Ubuntu)

```bash
sudo apt install libusb-1.0-0-dev liblz4-dev zlib1g-dev libcrypto++-dev
```

### Linux (Fedora)

```bash
sudo dnf install libusb1-devel lz4-devel zlib-devel cryptopp-devel
```

## Building

### With CMake (recommended)

```bash
mkdir build && cd build
cmake ..
make
```

### With Make (fallback)

```bash
make
```

## Installation

```bash
sudo make install
```

Or manually copy the `odin4` binary to your PATH.

## Usage

```bash
# Show help
odin4 -h

# Show version
odin4 -v

# List available devices
odin4 -l

# Flash firmware
odin4 -b BL_XXXX.tar.md5 -a AP_XXXX.tar.md5 -c CP_XXXX.tar.md5 -s CSC_XXXX.tar.md5

# Flash to specific device
odin4 -b BL.tar.md5 -a AP.tar.md5 -d /dev/bus/usb/001/002

# Enable NAND erase
odin4 -e -b BL.tar.md5 -a AP.tar.md5

# Flash with PIT file
odin4 -V partition.pit -b BL.tar.md5 -a AP.tar.md5
```

## Options

| Option | Description |
|--------|-------------|
| `-h` | Show help |
| `-v` | Show version |
| `-w` | Show licenses |
| `-l` | List downloadable devices |
| `-b FILE` | Add Bootloader file |
| `-a FILE` | Add AP (Android) image file |
| `-c FILE` | Add CP (Modem) image file |
| `-s FILE` | Add CSC file |
| `-u FILE` | Add UMS file |
| `-V FILE` | Validate with PIT file |
| `-e` | Enable NAND erase |
| `-d PATH` | Specify device path |
| `--reboot` | Reboot to normal mode after flash |
| `--redownload` | Reboot to download mode |

## udev Rules (Linux)

To access Samsung devices without root, create `/etc/udev/rules.d/51-samsung.rules`:

```
SUBSYSTEM=="usb", ATTR{idVendor}=="04e8", MODE="0666", GROUP="plugdev"
```

Then reload udev:

```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

## Troubleshooting

### Device not detected

1. Ensure device is in Download Mode (hold Volume Down + Home + Power, then Volume Up)
2. Check USB connection
3. Try `sudo rmmod cdc_acm` if using older kernel
4. Verify udev rules are installed

### Permission denied

Run with `sudo` or set up udev rules (see above).

### Build errors

Ensure all dependencies are installed. Run `make deps` to check.

## License

This software is provided under the MIT License.

See [showLicenses.cpp](src/showLicenses.cpp) for full license information including third-party libraries.

## Disclaimer

This tool is provided for educational and legitimate device maintenance purposes.
Use at your own risk. The authors are not responsible for any damage caused by
using this software. Always backup your data before flashing firmware.

Samsung and Odin are trademarks of Samsung Electronics Co., Ltd.

## Project Structure

```
odin4/
├── CMakeLists.txt          # CMake build configuration
├── Makefile                # Fallback Makefile
├── README.md               # This file
├── include/
│   ├── DownloadEngine.h    # Core protocol class
│   ├── FirmwareData.h      # Firmware parsing
│   ├── FirmwareInfo.h      # Firmware file info struct
│   ├── Log.h               # Logging utility
│   ├── Manifest.h          # Hash verification
│   ├── OdinException.h     # Exception classes
│   ├── PIT.h               # Partition table parsing
│   ├── Tar.h               # TAR archive handling
│   └── UsbDevice.h         # USB device interface
└── src/
    ├── DownloadEngine.cpp  # Protocol implementation
    ├── FirmwareData.cpp    # Firmware parsing
    ├── Log.cpp             # Logging
    ├── main.cpp            # Entry point
    ├── Manifest.cpp        # Hash calculation
    ├── PIT.cpp             # PIT handling
    ├── showLicenses.cpp    # License display
    ├── Tar.cpp             # TAR handling
    └── UsbDeviceImpl.cpp   # USB implementation
```

## Acknowledgements

- [Heimdall](https://github.com/Benjamin-Dobell/Heimdall) by Benjamin Dobell
- [Crypto++](https://www.cryptopp.com/)
- [libusb](https://libusb.info/)
