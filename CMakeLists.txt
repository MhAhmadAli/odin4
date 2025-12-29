cmake_minimum_required(VERSION 3.16)
project(odin4 VERSION 1.2.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
pkg_check_modules(CRYPTOPP cryptopp)
pkg_check_modules(LZ4 liblz4)
pkg_check_modules(ZLIB zlib)
pkg_check_modules(LIBARCHIVE libarchive)

# If crypto++ not found via pkg-config, try find_library
if(NOT CRYPTOPP_FOUND)
    find_library(CRYPTOPP_LIBRARIES NAMES cryptopp crypto++)
    find_path(CRYPTOPP_INCLUDE_DIRS NAMES cryptopp/sha.h crypto++/sha.h)
    if(CRYPTOPP_LIBRARIES AND CRYPTOPP_INCLUDE_DIRS)
        set(CRYPTOPP_FOUND TRUE)
    endif()
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/Log.cpp
    src/UsbDeviceImpl.cpp
    src/DownloadEngine.cpp
    src/FirmwareData.cpp
    src/Tar.cpp
    src/PIT.cpp
    src/Manifest.cpp
    src/showLicenses.cpp
)

# Header files
set(HEADERS
    include/Log.h
    include/UsbDevice.h
    include/DownloadEngine.h
    include/FirmwareData.h
    include/FirmwareInfo.h
    include/Tar.h
    include/PIT.h
    include/Manifest.h
    include/OdinException.h
)

# Create executable
add_executable(odin4 ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(odin4 PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${LIBUSB_INCLUDE_DIRS}
    ${CRYPTOPP_INCLUDE_DIRS}
    ${LZ4_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${LIBARCHIVE_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(odin4 PRIVATE
    ${LIBUSB_LIBRARIES}
    ${CRYPTOPP_LIBRARIES}
    ${LZ4_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${LIBARCHIVE_LIBRARIES}
    pthread
)

# Compiler flags
target_compile_options(odin4 PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# Version info
target_compile_definitions(odin4 PRIVATE
    ODIN4_VERSION="${PROJECT_VERSION}"
    ODIN4_VERSION_STRING="1.2.1-dc05e3ea"
)

# Install
install(TARGETS odin4 RUNTIME DESTINATION bin)

# Print configuration summary
message(STATUS "")
message(STATUS "Odin4 Configuration Summary:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  libusb: ${LIBUSB_VERSION}")
message(STATUS "  Crypto++: ${CRYPTOPP_FOUND}")
message(STATUS "  LZ4: ${LZ4_FOUND}")
message(STATUS "  zlib: ${ZLIB_FOUND}")
message(STATUS "  libarchive: ${LIBARCHIVE_FOUND}")
message(STATUS "")
