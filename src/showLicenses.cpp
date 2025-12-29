/*
 * Odin4 - Samsung Firmware Flashing Tool for Linux
 * License display
 */

#include <iostream>

// Not in namespace so it can be called from main
void showLicenses() {
    std::cout << R"(
Odin4 - Samsung Firmware Flashing Tool for Linux
=================================================

Reconstructed by MhAhmadAli
https://github.com/MhAhmadAli

This software is a reconstruction based on:
- Heimdall by Benjamin Dobell (MIT License)
- Original Samsung Odin4 tool

LICENSES
--------

MIT License (Heimdall)
Copyright (c) 2010-2017 Benjamin Dobell, Glass Echidna

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

---

Crypto++ Library (Boost Software License 1.0)
https://www.cryptopp.com/

---

libusb (LGPL-2.1)
https://libusb.info/

---

zlib (zlib License)
https://zlib.net/

---

LZ4 (BSD 2-Clause License)
https://github.com/lz4/lz4

---

DISCLAIMER
----------

This tool is provided for educational and legitimate device maintenance purposes.
Use at your own risk. The authors are not responsible for any damage caused by
using this software. Always backup your data before flashing firmware.

Samsung and Odin are trademarks of Samsung Electronics Co., Ltd.
)" << std::endl;
}
