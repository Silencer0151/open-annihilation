# Attributions

Open Annihilation is licensed under the GNU General Public License version 3
only (GPL-3.0-only; see [`LICENSE`](LICENSE)). The release packages also
contain the third-party software listed below. Each component keeps its own
licence, and the notices those licences require are reproduced here. Full
licence texts are in the [`licenses/`](licenses/) folder of this repository.
A build copies this file, `LICENSE` and the `licenses/` folder beside
`open-annihilation`, `oa-intro` and `oa-tool`, and on macOS into the game's
bundle, `open-annihilation.app/Contents/Resources`, so the links below also
resolve there.

No Total Annihilation or Total Annihilation: Kingdoms game data is included
in this repository or in the release packages.

## What each package contains

| Component | Version | Licence | macOS | Windows | Linux |
|---|---|---|---|---|---|
| [SDL](#sdl) | 3.4.16 | zlib | static | static | static |
| [zlib](#zlib) | 1.3.1 | zlib | static | static | static |
| [stb_vorbis](#stb_vorbis) | 1.22, changed | public domain or MIT | static | static | static |
| [dr_mp3 and dr_flac](#dr_mp3-and-dr_flac) | 0.7.3 and 0.13.3, with later fixes | public domain or MIT No Attribution | static | static | static |
| [mingw-w64 runtime and winpthreads](#mingw-w64-runtime-and-winpthreads) | 15.0.0 | ZPL 2.1, MIT, BSD | | static | |
| [LLVM runtime libraries](#llvm-runtime-libraries) (libc++, libc++abi, libunwind, compiler-rt) | 23.1.2 | Apache 2.0 with LLVM Exceptions | | static | |

The operating system's own libraries, such as the C and C++ runtimes and
the graphics, audio and windowing libraries, are not included.

## Builds from this source

The table above describes the release packages. A build made from this
repository with CMake links these components as follows:

- stb_vorbis, dr_mp3 and dr_flac, which decode the music, are kept in
  [`third_party/`](third_party/) and compiled into every build. The movies
  and the rest of the sound are decoded by the engine's own code.
- When CMake is pointed at the SDL that `tools/bootstrap_sdl.py` installs,
  as `run.sh` and the README do, SDL 3.4.16 is linked statically. Otherwise
  the build takes whichever SDL 3.2 or later CMake finds, which may be a
  shared library.
- On macOS and Linux, zlib is the system's, except in the macOS release
  build below: the executables link the zlib CMake finds, and the build
  copies none beside the executables.
- The native Windows build that the README describes, with vcpkg's
  `zlib:x64-windows`, links that zlib as a DLL and copies it beside
  `oa-tool.exe`.
- The Windows cross-build, `tools/build_windows.sh`, builds zlib 1.3.1 and
  SDL for the target and links them statically.
- The macOS release build, `tools/release_macos.sh`, builds zlib 1.3.1 and
  SDL 3.4.16 for arm64 and x86_64 as static libraries with
  `tools/bootstrap_macos_deps.py` and links them into the application, as
  the macOS package in the table above has them.
- The movies and music need no other library; the `ffmpeg` program that
  video capture and the director's renders start (docs/capture.md) is a
  separate program that no package contains.

## SDL

SDL 3.4.16, from <https://www.libsdl.org/release/SDL3-3.4.16.tar.gz>
(SHA-256 `7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68`),
unmodified and linked statically.

> Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>
>
> This software is provided 'as-is', without any express or implied
> warranty. In no event will the authors be held liable for any damages
> arising from the use of this software.
>
> Permission is granted to anyone to use this software for any purpose,
> including commercial applications, and to alter it and redistribute it
> freely, subject to the following restrictions:
>
> 1. The origin of this software must not be misrepresented; you must not
>    claim that you wrote the original software. If you use this software
>    in a product, an acknowledgment in the product documentation would be
>    appreciated but is not required.
> 2. Altered source versions must be plainly marked as such, and must not be
>    misrepresented as being the original software.
> 3. This notice may not be removed or altered from any source distribution.

SDL contains code from other projects:

- **yuv2rgb** (BSD 3-Clause), Copyright (c) 2016, Adrien Descamps. All
  rights reserved. Redistributions in binary form must reproduce the
  copyright notice, the list of conditions and the disclaimer; the full text
  is in [`licenses/SDL-yuv2rgb.txt`](licenses/SDL-yuv2rgb.txt).
- **HIDAPI**, used under its original licence: "HIDAPI - Multi-Platform
  library for communication with HID devices. Copyright 2009, Alan Ott,
  Signal 11 Software. All Rights Reserved. This software may be used by
  anyone for any reason so long as the copyright notice in the source files
  remains intact."
- **fdlibm**: "Copyright (C) 1993 by Sun Microsystems, Inc. All rights
  reserved. Developed at SunPro, a Sun Microsystems, Inc. business.
  Permission to use, copy, modify, and distribute this software is freely
  granted, provided that this notice is preserved."
- **stb_image**, **miniz**, Doug Lea's **malloc** and the IBM VGA font are
  in the public domain.
- **Linux only:** SDL's X11 and Wayland support contains code under
  MIT-style licences (keysym conversion, EDID parsing, XSETTINGS and the
  Wayland protocol files). Their notices are in
  [`licenses/SDL-linux.txt`](licenses/SDL-linux.txt).

## stb_vorbis

stb_vorbis 1.22, the Ogg Vorbis decoder of the stb libraries, from
<https://github.com/nothings/stb>, decodes `.ogg` music files. It is
compiled into the game from [`third_party/stb_vorbis/`](third_party/stb_vorbis/),
with one change that its [README](third_party/stb_vorbis/README.md)
describes. It is in the public domain, or, at your choice, licensed under
the MIT licence; both texts are in
[`licenses/stb_vorbis-LICENSE.txt`](licenses/stb_vorbis-LICENSE.txt).

> Copyright (c) 2017 Sean Barrett
>
> Permission is hereby granted, free of charge, to any person obtaining a
> copy of this software and associated documentation files (the
> "Software"), to deal in the Software without restriction, including
> without limitation the rights to use, copy, modify, merge, publish,
> distribute, sublicense, and/or sell copies of the Software, and to permit
> persons to whom the Software is furnished to do so, subject to the
> following conditions: The above copyright notice and this permission
> notice shall be included in all copies or substantial portions of the
> Software.

## dr_mp3 and dr_flac

dr_mp3 and dr_flac, David Reid's MP3 and FLAC decoders, from
<https://github.com/mackron/dr_libs>, decode `.mp3` and `.flac` music
files. They are compiled into the game unchanged from
[`third_party/dr_libs/`](third_party/dr_libs/). They are in the public
domain, or, at your choice, licensed under the MIT No Attribution licence
(Copyright 2020 David Reid); both texts are in
[`licenses/dr_libs-LICENSE.txt`](licenses/dr_libs-LICENSE.txt). dr_mp3 is
based on minimp3, which is in the public domain (CC0).

## zlib

All packages link zlib 1.3.1 statically, from
<https://github.com/madler/zlib/releases/> (SHA-256
`9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23`).

> Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler
>
> This software is provided 'as-is', without any express or implied
> warranty. In no event will the authors be held liable for any damages
> arising from the use of this software.
>
> Permission is granted to anyone to use this software for any purpose,
> including commercial applications, and to alter it and redistribute it
> freely, subject to the following restrictions:
>
> 1. The origin of this software must not be misrepresented; you must not
>    claim that you wrote the original software. If you use this software
>    in a product, an acknowledgment in the product documentation would be
>    appreciated but is not required.
> 2. Altered source versions must be plainly marked as such, and must not be
>    misrepresented as being the original software.
> 3. This notice may not be removed or altered from any source distribution.

## mingw-w64 runtime and winpthreads

The Windows packages are linked statically against the mingw-w64 15.0.0
runtime (Zope Public License 2.1, with parts in the public domain or under
BSD licences; Copyright (c) 2009-2013 by the mingw-w64 project) and its
winpthreads library (MIT; Copyright (c) 2011-2016 mingw-w64 project; parts
(C) 2010 Lockless Inc., BSD 3-Clause). Their full notices and disclaimers
are in [`licenses/mingw-w64.txt`](licenses/mingw-w64.txt).

## LLVM runtime libraries

The Windows packages (x64, x86 and the experimental ARM64 package) are
built with the LLVM toolchain llvm-mingw 20260922, with LLVM 23.1.2, and
link LLVM's C++ standard library and its support libraries (libc++,
libc++abi, libunwind and compiler-rt) statically. These are licensed under
the Apache License 2.0 with LLVM Exceptions, which place no requirements on
programs that embed them in compiled form.
