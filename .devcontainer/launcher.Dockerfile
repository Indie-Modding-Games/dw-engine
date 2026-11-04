# Launcher image: what building and testing the desktop launcher (apps/launcher, Tauri 2) needs, so nothing is
# installed on the host. Same Ubuntu release as the main image, so the launcher links against the host's libraries.
FROM ubuntu:24.04

ARG NODE_MAJOR=22
ARG RUST_TOOLCHAIN=stable
ARG TAURI_CLI_VERSION=2

ENV DEBIAN_FRONTEND=noninteractive

# The system libraries Tauri needs on Linux (WebKitGTK 4.1, libsoup 3, the tray indicator, SVG icons), the D-Bus
# headers of the keyring crate and the tools of the AppImage bundler.
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl wget file git build-essential pkg-config python3 xz-utils zstd \
        libwebkit2gtk-4.1-dev libsoup-3.0-dev libgtk-3-dev libayatana-appindicator3-dev librsvg2-dev \
        libssl-dev libdbus-1-dev libxdo-dev patchelf desktop-file-utils fuse3 \
    && rm -rf /var/lib/apt/lists/*

# Node LTS for the Tauri command line (no web framework: the interface is plain HTML, CSS and JavaScript).
RUN curl -fsSL "https://deb.nodesource.com/setup_${NODE_MAJOR}.x" | bash - \
    && apt-get install -y --no-install-recommends nodejs \
    && rm -rf /var/lib/apt/lists/* \
    && npm install -g "@tauri-apps/cli@${TAURI_CLI_VERSION}" \
    && npm cache clean --force

# Rust for every user: the container runs as the host's user, so the toolchain must not live in root's home.
ENV RUSTUP_HOME=/opt/rustup CARGO_HOME=/opt/cargo PATH=/opt/cargo/bin:$PATH
RUN curl -fsSL https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain "${RUST_TOOLCHAIN}" \
        --component clippy,rustfmt \
    && mkdir -p /opt/cargo/registry /opt/cargo/git \
    && chmod -R a+rwX /opt/rustup /opt/cargo
