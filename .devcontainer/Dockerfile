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

# The Windows build from Linux (PRESET=windows-cross): LLVM's MSVC-style tools, and xwin, which fetches the MSVC CRT
# and STL and the Windows SDK into a volume (scripts/dev.sh windows-sdk). The image has only the tool: the files
# themselves come with Microsoft's license, which whoever runs windows-sdk accepts.
ARG XWIN_VERSION=0.6.5
RUN for tool in clang-cl lld-link llvm-lib llvm-rc llvm-mt; do \
           ln -sf /usr/bin/${tool}-${LLVM_VERSION} /usr/local/bin/${tool}; \
       done \
    && curl -fsSL "https://github.com/Jake-Shadle/xwin/releases/download/${XWIN_VERSION}/xwin-${XWIN_VERSION}-x86_64-unknown-linux-musl.tar.gz" \
        | tar -xz -C /usr/local/bin --strip-components=1 "xwin-${XWIN_VERSION}-x86_64-unknown-linux-musl/xwin" \
    && xwin --version

# The formatters and linters of Lua and shell (scripts/dev.sh format and lint), from the official
# releases, pinned by version and checksum, and clangd for the editors in the dev container
# (.devcontainer/devcontainer.json). luacheck's release binary aborts in a container (its threads
# ask for a scheduling policy), so it runs from its source rock on the release's Lua 5.4 with the
# two libraries it needs.
ARG STYLUA_VERSION=2.6.0
ARG STYLUA_SHA256=48ee7db28346f96f4bd545e190ab080a88a7b5a664e97649cf5d5c60426f402d
ARG LUACHECK_VERSION=1.2.0
ARG LUACHECK_SHA256=4bad582d6c70cbb948fa79bc362786d8c86633fe0c26ed7383c8a134a83d3c46
ARG SHFMT_VERSION=3.14.1
ARG SHFMT_SHA256=76e77641faa025814b77f153b29796b8e6fa2fca03e0c76a691608b86c7ea7bf
ARG SHELLCHECK_VERSION=0.11.0
ARG SHELLCHECK_SHA256=b7af85e41cc99489dcc21d66c6d5f3685138f06d34651e6d34b42ec6d54fe6f6
RUN apt-get update && apt-get install -y --no-install-recommends lua5.4 lua-argparse lua-filesystem \
        clangd-${LLVM_VERSION} \
    && rm -rf /var/lib/apt/lists/* \
    && ln -sf /usr/bin/clangd-${LLVM_VERSION} /usr/local/bin/clangd
RUN release="https://github.com" && cd /tmp \
    && curl -fsSLo stylua.zip \
        "${release}/JohnnyMorganz/StyLua/releases/download/v${STYLUA_VERSION}/stylua-linux-x86_64-musl.zip" \
    && echo "${STYLUA_SHA256}  stylua.zip" | sha256sum -c - \
    && unzip -q stylua.zip stylua -d /usr/local/bin \
    && curl -fsSLo luacheck.rock \
        "${release}/lunarmodules/luacheck/releases/download/v${LUACHECK_VERSION}/luacheck-${LUACHECK_VERSION}-1.src.rock" \
    && echo "${LUACHECK_SHA256}  luacheck.rock" | sha256sum -c - \
    && unzip -q luacheck.rock 'luacheck/src/luacheck/*' \
    && mkdir -p /usr/local/share/lua/5.4 \
    && cp -r luacheck/src/luacheck /usr/local/share/lua/5.4/ \
    && printf '#!/usr/bin/lua5.4\nrequire "luacheck.main"\n' > /usr/local/bin/luacheck \
    && curl -fsSLo /usr/local/bin/shfmt \
        "${release}/mvdan/sh/releases/download/v${SHFMT_VERSION}/shfmt_v${SHFMT_VERSION}_linux_amd64" \
    && echo "${SHFMT_SHA256}  /usr/local/bin/shfmt" | sha256sum -c - \
    && curl -fsSLo shellcheck.tar.gz \
        "${release}/koalaman/shellcheck/releases/download/v${SHELLCHECK_VERSION}/shellcheck-v${SHELLCHECK_VERSION}.linux.x86_64.tar.gz" \
    && echo "${SHELLCHECK_SHA256}  shellcheck.tar.gz" | sha256sum -c - \
    && tar -xzf shellcheck.tar.gz --strip-components=1 -C /usr/local/bin "shellcheck-v${SHELLCHECK_VERSION}/shellcheck" \
    && rm -r stylua.zip luacheck.rock luacheck shellcheck.tar.gz \
    && chmod 755 /usr/local/bin/stylua /usr/local/bin/luacheck /usr/local/bin/shfmt /usr/local/bin/shellcheck \
    && stylua --version && luacheck --version && shfmt --version && shellcheck --version

# The documentation site (docs/, scripts/dev.sh docs): Node from its official build, checked
# against its checksum.
ARG NODE_VERSION=24.22.1
ARG NODE_SHA256=eb6728e77c576a35162c122c45ccadf224d89ff00abc8dae46cc2897c6002d7c
RUN curl -fsSLo /tmp/node.tar.gz \
        "https://nodejs.org/dist/v${NODE_VERSION}/node-v${NODE_VERSION}-linux-x64.tar.gz" \
    && echo "${NODE_SHA256}  /tmp/node.tar.gz" | sha256sum -c - \
    && tar -xzf /tmp/node.tar.gz -C /usr/local --strip-components=1 --no-same-owner \
        "node-v${NODE_VERSION}-linux-x64/bin" "node-v${NODE_VERSION}-linux-x64/lib" \
    && rm /tmp/node.tar.gz \
    && node --version && npm --version

# The site's packages (VitePress, Mermaid) at the versions of the committed lockfile, inside the
# image, without their install scripts: the repository gets no node_modules. /node_modules points
# at them, so the site's configuration and theme find them from any folder, as Node looks for
# packages up to the root.
ENV DWE_DOCS_TOOLS=/opt/docs-site NPM_CONFIG_UPDATE_NOTIFIER=false
COPY docs-site/package.json docs-site/package-lock.json ${DWE_DOCS_TOOLS}/
RUN npm ci --prefix ${DWE_DOCS_TOOLS} --ignore-scripts --no-audit --no-fund \
    && ln -s ${DWE_DOCS_TOOLS}/node_modules /node_modules \
    && npm ls --prefix ${DWE_DOCS_TOOLS} vitepress mermaid
ENV PATH=${DWE_DOCS_TOOLS}/node_modules/.bin:${PATH}
