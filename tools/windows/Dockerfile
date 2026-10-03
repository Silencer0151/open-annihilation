# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# An x86-64 Linux image that cross-builds the tree for Windows with the
# mingw-w64 GCC toolchain and runs the Windows test executables under Wine.
# tools/test_windows.sh builds it and runs tools/build_windows.sh --run-tests
# inside it, with the checkout and the game install mounted at their host paths.
FROM --platform=linux/amd64 debian:trixie-slim

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        ca-certificates cmake curl gcc git libc6-dev make pkg-config python3 python3-venv xz-utils \
        g++-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64 \
        mingw-w64-tools wine wine64 \
    && rm -rf /var/lib/apt/lists/*

# The POSIX-threads variant provides std::thread and std::mutex, as the
# Homebrew toolchain does.
RUN update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix \
    && update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix

# ccache keeps compiled objects in the volume tools/test_windows.sh mounts, so a
# rebuild, or a build of another checkout, reuses them; Ninja generates new
# build trees. They sit in a layer of their own so the toolchain layer above
# stays cached when the image is rebuilt.
RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends ccache ninja-build \
    && rm -rf /var/lib/apt/lists/*

# Wine's own diagnostics stay out of test logs, and it skips the Mono and Gecko
# installers it would otherwise offer when a prefix is created.
ENV WINEDEBUG=-all WINEDLLOVERRIDES=mscoree,mshtml= XDG_RUNTIME_DIR=/tmp
