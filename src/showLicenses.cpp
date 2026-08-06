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

Full notices for every third-party component, including the complete licence
texts, are in THIRD-PARTY-NOTICES.md alongside the source.

LICENSES
--------

MIT License (Odin4)
Copyright (c) 2024-2026 MhAhmadAli

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

MIT License (Heimdall)
Copyright (c) 2010-2017 Benjamin Dobell, Glass Echidna
https://github.com/Benjamin-Dobell/Heimdall

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

libusb (LGPL-2.1-or-later)
https://libusb.info/

Used as a shared library; this program contains no libusb code. libusb remains
under the LGPL, and nothing in Odin4's licence limits your rights under it.
You may obtain the libusb source from https://github.com/libusb/libusb, modify
it, and use your modified build with this program by replacing the installed
libusb-1.0 shared library. Odin4's own source is available so it can also be
rebuilt from scratch.

Full text: https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html
On Debian-based systems: /usr/share/common-licenses/LGPL-2.1

libusb may in turn use libudev (LGPL-2.1-or-later) on Linux; the same terms
and relinking rights apply to it.

---

OpenSSL (Apache License 2.0)
Copyright (c) The OpenSSL Project Authors
https://www.openssl.org/

Provides MD5 and SHA-256 unless this build was configured against Crypto++.

Licensed under the Apache License, Version 2.0. You may obtain a copy of the
License at https://www.apache.org/licenses/LICENSE-2.0. Unless required by
applicable law or agreed to in writing, software distributed under the License
is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
KIND, either express or implied.

(OpenSSL 1.x is instead covered by the dual OpenSSL/SSLeay licence.)

---

Crypto++ Library (Boost Software License 1.0)
https://www.cryptopp.com/

Optional alternative hash backend, used when detected at build time. The
library as a compilation is under the Boost Software License 1.0; the
individual source files are released into the public domain by their authors.

Full text: https://www.boost.org/LICENSE_1_0.txt

---

zlib (zlib License)
Copyright (c) 1995-2024 Jean-loup Gailly and Mark Adler
https://zlib.net/

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the
use of this software. Permission is granted to anyone to use this software for
any purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the restrictions stated in the zlib
licence.

---

LZ4 (BSD 2-Clause License)
Copyright (c) 2011-2020 Yann Collet
https://github.com/lz4/lz4

Only the LZ4 library (liblz4) is linked, under BSD-2-Clause. The separate lz4
command line programs are GPL-2.0 and are neither used nor distributed here.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the conditions of the BSD 2-Clause
licence are met. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES ARE DISCLAIMED.

---

DISCLAIMER
----------

This tool is provided for EDUCATIONAL PURPOSES ONLY.
Use at your own risk. The authors are not responsible for any damage caused by
using this software. Always backup your data before flashing firmware.

The MIT License above covers this project's own source code. It does not grant
any rights in Samsung's original software, and reverse engineering or
redistributing software derived from a proprietary binary may be restricted in
your jurisdiction independently of this licence.

Samsung and Odin are trademarks of Samsung Electronics Co., Ltd. This project
is not affiliated with, endorsed by, or sponsored by Samsung.
)" << std::endl;
}
