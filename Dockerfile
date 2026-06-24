FROM debian:bookworm-slim AS native-test
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
    && cmake --build /build \
    && ctest --test-dir /build --output-on-failure

FROM debian:trixie-slim AS rpi64-cross-build
RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake ninja-build gcc-aarch64-linux-gnu g++-aarch64-linux-gnu file \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B /build-rpi64 -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=/src/cmake/aarch64-linux-gnu.cmake \
        -DBUILD_TESTING=OFF \
    && cmake --build /build-rpi64 \
    && file /build-rpi64/pi-satellite-rotator /build-rpi64/witmotion-tool

FROM balenalib/raspberry-pi-debian:bookworm-build AS rpi-zero-32-cross-build
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build file binutils \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B /build-rpi-zero-32 -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF \
    && cmake --build /build-rpi-zero-32 \
    && file /build-rpi-zero-32/pi-satellite-rotator /build-rpi-zero-32/witmotion-tool \
    && readelf -A /build-rpi-zero-32/pi-satellite-rotator | tee /tmp/pi-zero.attrs \
    && ! grep -q 'Tag_CPU_arch: v7' /tmp/pi-zero.attrs \
    && ! grep -q 'Tag_THUMB_ISA_use: Thumb-2' /tmp/pi-zero.attrs

FROM scratch AS artifact
COPY --from=rpi64-cross-build /build-rpi64/pi-satellite-rotator /
COPY --from=rpi64-cross-build /build-rpi64/witmotion-tool /

FROM scratch AS artifact-rpi64
COPY --from=rpi64-cross-build /build-rpi64/pi-satellite-rotator /
COPY --from=rpi64-cross-build /build-rpi64/witmotion-tool /

FROM scratch AS artifact-rpi-zero-32
COPY --from=rpi-zero-32-cross-build /build-rpi-zero-32/pi-satellite-rotator /
COPY --from=rpi-zero-32-cross-build /build-rpi-zero-32/witmotion-tool /
