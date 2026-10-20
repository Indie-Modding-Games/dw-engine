# Development image: everything needed to build and test the engine, so nothing is installed on the host.
# The host only runs the resulting binary (docs/getting-started/setup.md).
FROM ubuntu:24.04

ARG LLVM_VERSION=22
ARG VCPKG_COMMIT=664933b8d385dfc01f4e0cdff4c7c74e71f6a740

ENV DEBIAN_FRONTEND=noninteractive

# Build tools, vcpkg prerequisites and the system headers SDL3 needs for windows, input and audio.
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl wget gnupg lsb-release software-properties-common \
        git cmake ninja-build pkg-config python3 python3-venv \
        zip unzip tar autoconf autoconf-archive automake libtool bison flex \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev \
        libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libdrm-dev libgbm-dev \
        libegl1-mesa-dev libgles2-mesa-dev libasound2-dev libpulse-dev libpipewire-0.3-dev libjack-dev \
        libsndio-dev libdbus-1-dev libibus-1.0-dev libudev-dev liburing-dev \
        libvulkan-dev mesa-vulkan-drivers xvfb \
    && rm -rf /var/lib/apt/lists/*

# apt.llvm.org is slow and drops connections: retry and wait longer instead of failing the whole build.
RUN printf 'Acquire::Retries "10";\nAcquire::http::Timeout "60";\nAcquire::https::Timeout "60";\n' \
        > /etc/apt/apt.conf.d/80-retries

# Clang, clang-tidy, clang-format, libc++ and the sanitizer runtimes from apt.llvm.org, the only source of this
# version for the host's release (the binary must link against the host's glibc). Only the packages the build uses,
# split in layers so a dropped download does not throw away the ones already fetched.
RUN mkdir -p /etc/apt/keyrings \
    && wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor -o /etc/apt/keyrings/llvm.gpg \
    && echo "deb [signed-by=/etc/apt/keyrings/llvm.gpg] https://apt.llvm.org/noble/ llvm-toolchain-noble-${LLVM_VERSION} main" \
        > /etc/apt/sources.list.d/llvm.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends clang-${LLVM_VERSION} lld-${LLVM_VERSION} llvm-${LLVM_VERSION} \
    && rm -rf /var/lib/apt/lists/*
RUN apt-get update \
    && apt-get install -y --no-install-recommends libc++-${LLVM_VERSION}-dev libc++abi-${LLVM_VERSION}-dev \
        libclang-rt-${LLVM_VERSION}-dev \
    && rm -rf /var/lib/apt/lists/*
RUN apt-get update \
    && apt-get install -y --no-install-recommends clang-tidy-${LLVM_VERSION} clang-format-${LLVM_VERSION} \
    && rm -rf /var/lib/apt/lists/* \
    && for tool in clang clang++ clang-tidy clang-format run-clang-tidy llvm-ar llvm-ranlib lld ld.lld; do \
           ln -sf /usr/bin/${tool}-${LLVM_VERSION} /usr/local/bin/${tool}; \
       done

# System tools that the autotools ports (libxcrypt, through SDL's D-Bus support) need while they build.
RUN apt-get update && apt-get install -y --no-install-recommends make libltdl-dev \
    && rm -rf /var/lib/apt/lists/*

# vcpkg, pinned to the same commit as the builtin-baseline in vcpkg.json.
ENV VCPKG_ROOT=/opt/vcpkg
RUN git clone https://github.com/microsoft/vcpkg.git ${VCPKG_ROOT} \
    && git -C ${VCPKG_ROOT} checkout ${VCPKG_COMMIT} \
    && ${VCPKG_ROOT}/bootstrap-vcpkg.sh -disableMetrics \
    && chmod -R a+rwX ${VCPKG_ROOT}

# The release's CMake (3.28, installed above) does not know C++26, so the project uses the CMake that vcpkg pins and
# checks, linked in /usr/local/bin so it comes first in PATH.
RUN cmake_bin="$(dirname "$(${VCPKG_ROOT}/vcpkg fetch cmake 2>/dev/null | tail -n 1)")" \
    && test -x "${cmake_bin}/cmake" \
    && for tool in cmake ctest cpack; do ln -sf "${cmake_bin}/${tool}" /usr/local/bin/${tool}; done \
    && chmod -R a+rwX ${VCPKG_ROOT}/downloads \
    && cmake --version

ENV VCPKG_DEFAULT_BINARY_CACHE=/cache/vcpkg
ENV PATH=${VCPKG_ROOT}:${PATH}

# The compiler cache, last so adding it reuses every layer above. The presets put DWE_COMPILER_LAUNCHER in front of
# the compiler, so a compilation whose inputs have not changed comes out of the cache that scripts/dev.sh mounts.
RUN apt-get update && apt-get install -y --no-install-recommends ccache \
    && rm -rf /var/lib/apt/lists/*
ENV DWE_COMPILER_LAUNCHER=ccache
