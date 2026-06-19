#include "rotator/witmotion.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool near(double actual, double expected, double tolerance = 0.01) {
    return std::abs(actual - expected) <= tolerance;
}

std::array<std::uint8_t, 11> frame(std::uint8_t type,
                                   std::array<std::int16_t, 4> values) {
    std::array<std::uint8_t, 11> bytes{};
    bytes[0] = 0x55;
    bytes[1] = type;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto value = static_cast<std::uint16_t>(values[i]);
        bytes[2 + i * 2] = static_cast<std::uint8_t>(value & 0xFFU);
        bytes[3 + i * 2] = static_cast<std::uint8_t>(value >> 8U);
    }
    for (std::size_t i = 0; i < 10; ++i) {
        bytes[10] = static_cast<std::uint8_t>(bytes[10] + bytes[i]);
    }
    return bytes;
}

std::array<std::uint8_t, 20> combined_frame(std::array<std::int16_t, 9> values) {
    std::array<std::uint8_t, 20> bytes{};
    bytes[0] = 0x55;
    bytes[1] = 0x61;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto value = static_cast<std::uint16_t>(values[i]);
        bytes[2 + i * 2] = static_cast<std::uint8_t>(value & 0xFFU);
        bytes[3 + i * 2] = static_cast<std::uint8_t>(value >> 8U);
    }
    return bytes;
}
}

int main() {
    using namespace rotator;
    WitFrameDecoder decoder;
    const auto angle_bytes = frame(0x53, {16384, -8192, 32767, 0});
    const auto frames = decoder.push(angle_bytes);
    require(frames.size() == 1, "decode one angle frame");
    WitSample sample;
    require(apply_wit_frame(frames.front(), sample), "apply angle frame");
    require(near(sample.angle_degrees[0], 90.0), "roll scaling");
    require(near(sample.angle_degrees[1], -45.0), "pitch scaling");
    require(near(sample.angle_degrees[2], 179.9945), "yaw scaling");

    auto corrupted = angle_bytes;
    corrupted[10] ^= 0x01;
    require(decoder.push(corrupted).empty(), "reject bad checksum");

    std::array<std::uint8_t, 14> noisy{};
    noisy[0] = 0x12;
    noisy[1] = 0x55;
    noisy[2] = 0x00;
    std::copy(angle_bytes.begin(), angle_bytes.end(), noisy.begin() + 3);
    require(decoder.push(noisy).size() == 1, "resynchronize after noise");

    const auto combined = combined_frame({17, -34, 2042, 0, 0, 0, -1213, -879, 3359});
    const auto combined_frames = decoder.push(combined);
    require(combined_frames.size() == 1, "decode combined BLECL frame");
    WitSample combined_sample;
    require(apply_wit_frame(combined_frames.front(), combined_sample),
            "apply combined BLECL frame");
    require(near(combined_sample.acceleration_g[2], 0.9971), "combined acceleration");
    require(near(combined_sample.angle_degrees[0], -6.6632), "combined roll");
    require(near(combined_sample.angle_degrees[1], -4.8285), "combined pitch");
    require(near(combined_sample.angle_degrees[2], 18.4515), "combined yaw");

    require(wit_unlock_command() ==
                std::array<std::uint8_t, 5>{0xFF, 0xAA, 0x69, 0x88, 0xB5},
            "unlock command");
    require(wit_accelerometer_calibration_command() ==
                std::array<std::uint8_t, 5>{0xFF, 0xAA, 0x01, 0x01, 0x00},
            "accelerometer calibration command");
    require(wit_magnetic_calibration_command() ==
                std::array<std::uint8_t, 5>{0xFF, 0xAA, 0x01, 0x07, 0x00},
            "magnetic calibration command");
    require(near(normalize_azimuth(-1.0), 359.0), "negative azimuth wrap");
    require(near(normalize_azimuth(361.0), 1.0), "positive azimuth wrap");

    std::cout << "All WitMotion tests passed\n";
    return 0;
}
