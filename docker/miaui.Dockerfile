# Toolchain for the MiaUI (RG35XX) target: Debian bookworm (glibc 2.36, loads on
# MiaUI's glibc 2.37), armhf cross gcc and armhf SDL2. Also has cmake/ninja/python3
# so scripts/recomp.sh runs in it natively.
FROM debian:bookworm
RUN dpkg --add-architecture armhf && apt-get -qq update && \
    apt-get -qq install -y --no-install-recommends \
      build-essential cmake ninja-build python3 git ca-certificates file zip \
      gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf binutils-arm-linux-gnueabihf \
      libsdl2-dev:armhf && \
    rm -rf /var/lib/apt/lists/*
