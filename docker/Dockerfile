# syntax=docker/dockerfile:1
ARG UBUNTU_IMAGE
ARG EMSDK_IMAGE=emscripten/emsdk:7.0.0
ARG BUILD_TOOLS="build-essential ca-certificates cmake curl git glslang-tools \
    nasm ninja-build perl pkg-config python3 xz-utils zip"

# Build the applications and their static dependencies against the oldest supported glibc.
FROM ${UBUNTU_IMAGE:-ubuntu:24.04} AS linux-toolchain
ARG BUILD_TOOLS
RUN apt-get update && apt-get install -y --no-install-recommends \
    ${BUILD_TOOLS} \
    libasound2-dev \
    libdbus-1-dev \
    libdecor-0-dev \
    libdrm-dev \
    libegl-dev \
    libgbm-dev \
    libgl-dev \
    libpipewire-0.3-dev \
    libpulse-dev \
    libudev-dev \
    libwayland-dev \
    libx11-dev \
    libxcursor-dev \
    libxext-dev \
    libxfixes-dev \
    libxi-dev \
    libxkbcommon-dev \
    libxrandr-dev \
    libxss-dev \
    libxtst-dev \
    patchelf \
    && rm -rf /var/lib/apt/lists/*
COPY --chmod=755 \
    docker/install_dependency.py \
    docker/sources.txt \
    /recipes/docker/
COPY --chmod=755 scripts/build_dependency /recipes/scripts/build_dependency
ARG BUILD_JOBS=4
ENV CMAKE_BUILD_PARALLEL_LEVEL=${BUILD_JOBS}
RUN python3 -B /recipes/docker/install_dependency.py linux zlib
RUN python3 -B /recipes/docker/install_dependency.py linux sdl
RUN python3 -B /recipes/docker/install_dependency.py linux unshield
RUN python3 -B /recipes/docker/install_dependency.py linux opus
RUN python3 -B /recipes/docker/install_dependency.py linux vpx
RUN python3 -B /recipes/docker/install_dependency.py linux ffmpeg
RUN python3 -B /recipes/docker/install_dependency.py linux openssl

FROM ${UBUNTU_IMAGE:-ubuntu:26.04} AS windows-toolchain
ARG BUILD_TOOLS
RUN apt-get update && apt-get install -y --no-install-recommends \
    ${BUILD_TOOLS} \
    g++-mingw-w64-x86-64-posix \
    gcc-mingw-w64-x86-64-posix \
    && rm -rf /var/lib/apt/lists/*
COPY --chmod=755 \
    docker/install_dependency.py \
    docker/sources.txt \
    /recipes/docker/
COPY --chmod=755 scripts/build_dependency /recipes/scripts/build_dependency
COPY cmake/toolchain_mingw64.cmake /recipes/cmake/toolchain_mingw64.cmake
ARG BUILD_JOBS=4
ENV CMAKE_BUILD_PARALLEL_LEVEL=${BUILD_JOBS}
RUN python3 -B /recipes/docker/install_dependency.py windows zlib
RUN python3 -B /recipes/docker/install_dependency.py windows sdl
RUN python3 -B /recipes/docker/install_dependency.py windows unshield
RUN python3 -B /recipes/docker/install_dependency.py windows opus
RUN python3 -B /recipes/docker/install_dependency.py windows vpx
RUN python3 -B /recipes/docker/install_dependency.py windows ffmpeg
RUN python3 -B /recipes/docker/install_dependency.py windows openssl

FROM ${EMSDK_IMAGE} AS wasm-toolchain
USER root
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    cmake \
    curl \
    glslang-tools \
    make \
    ninja-build \
    perl \
    pkg-config \
    python3 \
    spirv-cross \
    xz-utils \
    && rm -rf /var/lib/apt/lists/*
COPY --chmod=755 \
    docker/install_dependency.py \
    docker/sources.txt \
    /recipes/docker/
COPY --chmod=755 scripts/build_dependency /recipes/scripts/build_dependency
ARG BUILD_JOBS=4
ENV CMAKE_BUILD_PARALLEL_LEVEL=${BUILD_JOBS}
RUN python3 -B /recipes/docker/install_dependency.py wasm sdl
RUN python3 -B /recipes/docker/install_dependency.py wasm zlib
RUN python3 -B /recipes/docker/install_dependency.py wasm opus
RUN python3 -B /recipes/docker/install_dependency.py wasm vpx
RUN python3 -B /recipes/docker/install_dependency.py wasm ffmpeg
RUN python3 -B /recipes/docker/install_dependency.py wasm unshield

# Keep the development compiler and formatter independent of the release baseline.
FROM ${UBUNTU_IMAGE:-ubuntu:26.04} AS linux-check
ARG BUILD_TOOLS
RUN apt-get update && apt-get install -y --no-install-recommends \
    ${BUILD_TOOLS} clang clang-format libclang-rt-dev \
    && rm -rf /var/lib/apt/lists/*
COPY --from=linux-toolchain /opt/linux /opt/linux
ENV CMAKE_PREFIX_PATH=/opt/linux \
    CMAKE_GENERATOR=Ninja \
    LD_LIBRARY_PATH=/opt/linux/lib
WORKDIR /src
COPY . .
RUN ./scripts/check && rm -rf build/checks
RUN ./scripts/build_openmotor -DMH_RENDER_PROFILES=software \
    -DMH_AUDIO_PROFILES=software -DMH_NETWORK_PROTOCOLS=legacy \
    && python3 -B tests/integration/profile_binary.py \
        build/linux/openmotor nm software --protocols legacy \
    && rm -rf build/linux

FROM linux-toolchain AS linux-build
WORKDIR /src
COPY . .
ARG APPLICATIONS=all
ARG BUILD_JOBS=4
ARG MH_ENABLE_WEBRTC=ON
ARG CMAKE_ARGS=
ENV APPLICATIONS=${APPLICATIONS} \
    CMAKE_BUILD_PARALLEL_LEVEL=${BUILD_JOBS} \
    MH_ENABLE_WEBRTC=${MH_ENABLE_WEBRTC} \
    CMAKE_ARGS=${CMAKE_ARGS}
RUN python3 -B docker/build.py linux

FROM windows-toolchain AS windows-build
WORKDIR /src
COPY . .
ARG APPLICATIONS=all
ARG BUILD_JOBS=4
ARG MH_ENABLE_WEBRTC=ON
ARG CMAKE_ARGS=
ENV APPLICATIONS=${APPLICATIONS} \
    CMAKE_BUILD_PARALLEL_LEVEL=${BUILD_JOBS} \
    MH_ENABLE_WEBRTC=${MH_ENABLE_WEBRTC} \
    CMAKE_ARGS=${CMAKE_ARGS}
RUN python3 -B docker/build.py windows

FROM wasm-toolchain AS wasm-build
WORKDIR /src
COPY . .
ARG BUILD_JOBS=4
ARG MH_ENABLE_WEBRTC=ON
ARG CMAKE_ARGS=
ENV CMAKE_BUILD_PARALLEL_LEVEL=${BUILD_JOBS} \
    MH_ENABLE_WEBRTC=${MH_ENABLE_WEBRTC} \
    CMAKE_ARGS=${CMAKE_ARGS}
RUN python3 -B docker/build.py wasm

FROM scratch AS linux
COPY --from=linux-build /out/ /

FROM scratch AS windows
COPY --from=windows-build /out/ /

FROM scratch AS wasm
COPY --from=wasm-build /out/ /

# Explicit --target avoids building unrequested toolchains.
FROM scratch AS all
COPY --from=linux-build /out/ /
COPY --from=windows-build /out/ /
COPY --from=wasm-build /out/ /
