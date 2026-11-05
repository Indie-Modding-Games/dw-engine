# Wine image: runs the Windows package on Linux for a smoke test (scripts/wine_smoke.sh). Vulkan goes through Wine's
# own loader (winevulkan) to Mesa's lavapipe, so it needs no GPU; xdotool presses keys and ImageMagick takes the
# screenshots on the test's own X display.
FROM ubuntu:26.04
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends wine wine64 mesa-vulkan-drivers \
        libvulkan1 x11-utils imagemagick xdotool ca-certificates \
    && rm -rf /var/lib/apt/lists/*
