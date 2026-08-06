# Third-Party Notices

Odin4 itself is distributed under the MIT License (see [LICENSE](LICENSE)).
This file records the components it incorporates or links against, and the
notices those components require.

`odin4 -w` prints a condensed version of these notices at runtime.

## Summary

| Component | Used how | License |
|-----------|----------|---------|
| Heimdall | Source reference for the protocol and PIT layout | MIT |
| libusb | Dynamically linked | LGPL-2.1-or-later |
| OpenSSL (libcrypto) | Dynamically linked, default hash backend | Apache-2.0 |
| Crypto++ | Dynamically linked, optional hash backend | Boost Software License 1.0 |
| zlib | Dynamically linked | zlib License |
| LZ4 (liblz4) | Dynamically linked, optional | BSD-2-Clause |

Exactly one of OpenSSL or Crypto++ is used for MD5/SHA-256, selected at build
time: the Makefile prefers Crypto++ when `pkg-config` finds it and otherwise
falls back to OpenSSL. A binary built with the default configuration links
OpenSSL.

---

## Heimdall

Copyright (c) 2010-2017 Benjamin Dobell, Glass Echidna
<https://github.com/Benjamin-Dobell/Heimdall>

The download protocol command codes and the PIT structure layout used here are
derived from Heimdall's `libpit` and packet definitions.

```
MIT License

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
```

---

## libusb

<https://libusb.info/> — GNU Lesser General Public License, version 2.1 or
(at your option) any later version.

Odin4 uses libusb as a **shared library** and contains no libusb code. Nothing
about Odin4's own MIT terms restricts your rights under the LGPL with respect
to libusb:

- libusb is licensed under the LGPL-2.1-or-later, not under Odin4's licence.
- You may obtain the libusb source from <https://github.com/libusb/libusb>.
- You may modify libusb and relink Odin4 against your modified version. Odin4
  links it dynamically, so replacing the installed `libusb-1.0` shared object
  is sufficient; no relinking of Odin4 is required. Odin4's own source is
  available so it can also be rebuilt from scratch.

The full LGPL-2.1 text is available at
<https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html> and on Debian-based
systems at `/usr/share/common-licenses/LGPL-2.1`.

Note that libusb may itself pull in `libudev` (LGPL-2.1-or-later) on Linux.
The same terms and relinking rights apply to it.

---

## OpenSSL

Copyright (c) The OpenSSL Project Authors — <https://www.openssl.org/>

Used for MD5 and SHA-256 when Crypto++ is not available at build time, which
is the default configuration. OpenSSL 3.x is licensed under the Apache License,
Version 2.0.

```
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```

If you link OpenSSL 1.x instead, that release is covered by the dual
OpenSSL / SSLeay licence rather than Apache-2.0, and its own notice applies.

---

## Crypto++

<https://www.cryptopp.com/> — Boost Software License 1.0.

Optional; used for MD5 and SHA-256 when detected at build time
(`-DHAVE_CRYPTOPP`). The Crypto++ library as a compilation is distributed
under the Boost Software License 1.0; the individual source files are released
into the public domain by their authors.

```
Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
```

---

## zlib

Copyright (c) 1995-2024 Jean-loup Gailly and Mark Adler — <https://zlib.net/>

Used to decompress `.gz` firmware containers.

```
This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

---

## LZ4

Copyright (c) 2011-2020 Yann Collet — <https://github.com/lz4/lz4>

Odin4 links only the **LZ4 library** (`liblz4`), which is BSD-2-Clause. The
separate `lz4` command line programs in that project are GPL-2.0 and are not
used or distributed here.

```
BSD 2-Clause License

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

---

## Provenance and trademarks

Odin4 is a reconstruction produced by studying Samsung's `odin4` binary
alongside the open-source Heimdall project. The MIT License above covers this
project's own source code; it does not and cannot grant rights in Samsung's
original software. Reverse engineering and redistribution of software derived
from a proprietary binary may be restricted in your jurisdiction, independently
of this project's licence.

Samsung and Odin are trademarks of Samsung Electronics Co., Ltd. This project
is not affiliated with, endorsed by, or sponsored by Samsung.
