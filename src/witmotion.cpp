#include "rotator/witmotion.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <system_error>
#include <termios.h>
#include <unistd.h>

namespace rotator {
namespace {

std::int16_t signed_word(std::uint8_t low, std::uint8_t high) {
    const auto word = static_cast<std::uint16_t>(low) |
                      (static_cast<std::uint16_t>(high) << 8U);
    return static_cast<std::int16_t>(word);
}

speed_t baud_constant(unsigned int baud) {
    switch (baud) {
        case 9600:
            return B9600;
        case 19200:
            return B19200;
        case 38400:
            return B38400;
        case 57600:
            return B57600;
        case 115200:
            return B115200;
        case 230400:
            return B230400;
        default:
            throw std::invalid_argument("unsupported serial baud rate");
    }
}

}  // namespace

std::optional<WitFrame> WitFrameDecoder::push(std::uint8_t byte) {
    if (size_ == 0 && byte != 0x55) {
        return std::nullopt;
    }
    buffer_[size_++] = byte;
    if (size_ == 2) {
        expected_size_ = buffer_[1] ==
                                 static_cast<std::uint8_t>(WitFrameType::combined_motion)
                             ? 20
                             : 11;
    }
    if (expected_size_ == 0 || size_ < expected_size_) {
        return std::nullopt;
    }

    const bool combined =
        buffer_[1] == static_cast<std::uint8_t>(WitFrameType::combined_motion);
    if (!combined) {
        std::uint8_t checksum = 0;
        for (std::size_t i = 0; i < 10; ++i) {
            checksum = static_cast<std::uint8_t>(checksum + buffer_[i]);
        }
        if (checksum != buffer_[10]) {
            const auto end = buffer_.begin() + static_cast<std::ptrdiff_t>(expected_size_);
            const auto next = std::find(buffer_.begin() + 1, end, 0x55);
            if (next == end) {
                size_ = 0;
                expected_size_ = 0;
            } else {
                size_ = static_cast<std::size_t>(end - next);
                std::move(next, end, buffer_.begin());
                expected_size_ = size_ >= 2 &&
                                         buffer_[1] == static_cast<std::uint8_t>(
                                                           WitFrameType::combined_motion)
                                     ? 20
                                     : (size_ >= 2 ? 11 : 0);
            }
            return std::nullopt;
        }
    }

    WitFrame frame;
    frame.type = buffer_[1];
    const std::size_t value_count = combined ? 9 : 4;
    for (std::size_t i = 0; i < value_count; ++i) {
        frame.values[i] = signed_word(buffer_[2 + (i * 2)], buffer_[3 + (i * 2)]);
    }
    size_ = 0;
    expected_size_ = 0;
    return frame;
}

std::vector<WitFrame> WitFrameDecoder::push(std::span<const std::uint8_t> bytes) {
    std::vector<WitFrame> frames;
    for (const auto byte : bytes) {
        if (auto frame = push(byte)) {
            frames.push_back(*frame);
        }
    }
    return frames;
}

bool apply_wit_frame(const WitFrame& frame, WitSample& sample) {
    const auto type = static_cast<WitFrameType>(frame.type);
    if (type == WitFrameType::combined_motion) {
        for (std::size_t i = 0; i < 3; ++i) {
            sample.acceleration_g[i] = frame.values[i] / 32768.0 * 16.0;
            sample.angular_velocity_dps[i] = frame.values[i + 3] / 32768.0 * 2000.0;
            sample.angle_degrees[i] = frame.values[i + 6] / 32768.0 * 180.0;
        }
        sample.has_acceleration = true;
        sample.has_angular_velocity = true;
        sample.has_angle = true;
        return true;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        switch (type) {
            case WitFrameType::acceleration:
                sample.acceleration_g[i] = frame.values[i] / 32768.0 * 16.0;
                sample.has_acceleration = true;
                break;
            case WitFrameType::angular_velocity:
                sample.angular_velocity_dps[i] = frame.values[i] / 32768.0 * 2000.0;
                sample.has_angular_velocity = true;
                break;
            case WitFrameType::angle:
                sample.angle_degrees[i] = frame.values[i] / 32768.0 * 180.0;
                sample.has_angle = true;
                break;
            case WitFrameType::magnetic_field:
                sample.magnetic_field[i] = frame.values[i];
                sample.has_magnetic_field = true;
                break;
            case WitFrameType::combined_motion:
                break;
            default:
                return false;
        }
    }
    return true;
}

/*
 * The code below was formerly adjacent to the fixed-size decoder. Keep the
 * register-command implementation independent of either stream packet format.
 */

std::array<std::uint8_t, 5> wit_write_register(std::uint8_t address,
                                               std::uint16_t value) {
    return {0xFF, 0xAA, address, static_cast<std::uint8_t>(value & 0xFFU),
            static_cast<std::uint8_t>(value >> 8U)};
}

std::array<std::uint8_t, 5> wit_unlock_command() {
    return wit_write_register(0x69, 0xB588);
}

std::array<std::uint8_t, 5> wit_save_command() {
    return wit_write_register(0x00, 0x0000);
}

std::array<std::uint8_t, 5> wit_normal_command() {
    return wit_write_register(0x01, 0x0000);
}

std::array<std::uint8_t, 5> wit_accelerometer_calibration_command() {
    return wit_write_register(0x01, 0x0001);
}

std::array<std::uint8_t, 5> wit_magnetic_calibration_command() {
    return wit_write_register(0x01, 0x0007);
}

SerialPort::SerialPort(const std::string& device, unsigned int baud) {
    fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (fd_ < 0) {
        throw std::system_error(errno, std::generic_category(), "open " + device);
    }

    termios settings{};
    if (::tcgetattr(fd_, &settings) != 0) {
        const int error = errno;
        ::close(fd_);
        fd_ = -1;
        throw std::system_error(error, std::generic_category(), "tcgetattr " + device);
    }
    ::cfmakeraw(&settings);
    const speed_t speed = baud_constant(baud);
    ::cfsetispeed(&settings, speed);
    ::cfsetospeed(&settings, speed);
    settings.c_cflag |= CLOCAL | CREAD;
    settings.c_cflag &= ~CSTOPB;
    settings.c_cflag &= ~CRTSCTS;
    settings.c_cc[VMIN] = 0;
    settings.c_cc[VTIME] = 0;
    if (::tcsetattr(fd_, TCSANOW, &settings) != 0) {
        const int error = errno;
        ::close(fd_);
        fd_ = -1;
        throw std::system_error(error, std::generic_category(), "tcsetattr " + device);
    }
    ::tcflush(fd_, TCIOFLUSH);
}

SerialPort::~SerialPort() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

SerialPort::SerialPort(SerialPort&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

SerialPort& SerialPort::operator=(SerialPort&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

std::size_t SerialPort::read(std::span<std::uint8_t> destination,
                             std::chrono::milliseconds timeout) {
    pollfd descriptor{.fd = fd_, .events = POLLIN, .revents = 0};
    const int ready = ::poll(&descriptor, 1, static_cast<int>(timeout.count()));
    if (ready < 0) {
        if (errno == EINTR) {
            return 0;
        }
        throw std::system_error(errno, std::generic_category(), "serial poll");
    }
    if (ready == 0) {
        return 0;
    }
    const auto count = ::read(fd_, destination.data(), destination.size());
    if (count < 0) {
        if (errno == EAGAIN || errno == EINTR) {
            return 0;
        }
        throw std::system_error(errno, std::generic_category(), "serial read");
    }
    return static_cast<std::size_t>(count);
}

void SerialPort::write_all(std::span<const std::uint8_t> bytes) {
    std::size_t written = 0;
    while (written < bytes.size()) {
        const auto count = ::write(fd_, bytes.data() + written, bytes.size() - written);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::system_error(errno, std::generic_category(), "serial write");
        }
        written += static_cast<std::size_t>(count);
    }
    ::tcdrain(fd_);
}

double normalize_azimuth(double degrees) {
    double normalized = std::fmod(degrees, 360.0);
    if (normalized < 0.0) {
        normalized += 360.0;
    }
    return normalized;
}

}  // namespace rotator
