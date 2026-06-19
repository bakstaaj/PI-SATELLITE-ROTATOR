#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rotator {

enum class WitFrameType : std::uint8_t {
    acceleration = 0x51,
    angular_velocity = 0x52,
    angle = 0x53,
    magnetic_field = 0x54,
    combined_motion = 0x61,
};

struct WitFrame {
    std::uint8_t type{0};
    std::array<std::int16_t, 9> values{};
};

struct WitSample {
    std::array<double, 3> acceleration_g{};
    std::array<double, 3> angular_velocity_dps{};
    std::array<double, 3> angle_degrees{};
    std::array<std::int16_t, 3> magnetic_field{};
    bool has_acceleration{false};
    bool has_angular_velocity{false};
    bool has_angle{false};
    bool has_magnetic_field{false};
};

class WitFrameDecoder {
public:
    std::optional<WitFrame> push(std::uint8_t byte);
    std::vector<WitFrame> push(std::span<const std::uint8_t> bytes);

private:
    std::array<std::uint8_t, 20> buffer_{};
    std::size_t size_{0};
    std::size_t expected_size_{0};
};

bool apply_wit_frame(const WitFrame& frame, WitSample& sample);

std::array<std::uint8_t, 5> wit_write_register(std::uint8_t address,
                                               std::uint16_t value);
std::array<std::uint8_t, 5> wit_unlock_command();
std::array<std::uint8_t, 5> wit_save_command();
std::array<std::uint8_t, 5> wit_normal_command();
std::array<std::uint8_t, 5> wit_accelerometer_calibration_command();
std::array<std::uint8_t, 5> wit_magnetic_calibration_command();

class SerialPort {
public:
    explicit SerialPort(const std::string& device, unsigned int baud = 115200);
    ~SerialPort();
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;
    SerialPort(SerialPort&& other) noexcept;
    SerialPort& operator=(SerialPort&& other) noexcept;

    std::size_t read(std::span<std::uint8_t> destination,
                     std::chrono::milliseconds timeout);
    void write_all(std::span<const std::uint8_t> bytes);
    [[nodiscard]] int native_handle() const { return fd_; }

private:
    int fd_{-1};
};

double normalize_azimuth(double degrees);

}  // namespace rotator
