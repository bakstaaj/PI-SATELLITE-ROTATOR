FROM debian:bookworm-slim AS native-test
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
    && cmake --build /build \
    && ctest --test-dir /build --output-on-failure

FROM debian:bookworm-slim AS cross-build
RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake ninja-build gcc-aarch64-linux-gnu g++-aarch64-linux-gnu \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=/src/cmake/aarch64-linux-gnu.cmake \
        -DBUILD_TESTING=OFF \
    && cmake --build /build

FROM scratch AS artifact
COPY --from=cross-build /build/pi-satellite-rotator /
COPY --from=cross-build /build/witmotion-tool /
