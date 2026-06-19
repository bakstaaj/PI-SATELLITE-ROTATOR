#include "rotator/witmotion.hpp"

#include <array>
#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using namespace std::chrono_literals;

struct Options {
    std::string command;
    std::string device{"/dev/ttyUSB0"};
    unsigned int baud{115200};
    unsigned int seconds{10};
};

void usage() {
    std::cerr
        << "usage: witmotion-tool COMMAND [--device PATH] [--baud RATE] [--seconds N]\n"
        << "commands:\n"
        << "  monitor              print decoded sensor values\n"
        << "  raw                  print unparsed serial bytes in hexadecimal\n"
        << "  calibrate-accel      calibrate while sensor is level and stationary\n"
        << "  calibrate-magnetic   rotate all three axes during the selected duration\n";
}

unsigned int positive_integer(const char* text, const std::string& name) {
    try {
        const unsigned long value = std::stoul(text);
        if (value == 0 || value > 1000000UL) {
            throw std::out_of_range(name);
        }
        return static_cast<unsigned int>(value);
    } catch (const std::exception&) {
        throw std::invalid_argument("invalid " + name + ": " + text);
    }
}

Options parse_options(int argc, char** argv) {
    if (argc < 2) {
        throw std::invalid_argument("missing command");
    }
    Options options;
    options.command = argv[1];
    if (options.command == "calibrate-magnetic") {
        options.seconds = 60;
    }
    for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--device" && i + 1 < argc) {
            options.device = argv[++i];
        } else if (argument == "--baud" && i + 1 < argc) {
            options.baud = positive_integer(argv[++i], "baud rate");
        } else if (argument == "--seconds" && i + 1 < argc) {
            options.seconds = positive_integer(argv[++i], "duration");
        } else {
            throw std::invalid_argument("unknown or incomplete option: " + argument);
        }
    }
    return options;
}

void send(rotator::SerialPort& port, const std::array<std::uint8_t, 5>& command) {
    port.write_all(command);
    std::this_thread::sleep_for(100ms);
}

bool wait_for_sensor(rotator::SerialPort& port, std::chrono::seconds timeout) {
    rotator::WitFrameDecoder decoder;
    std::array<std::uint8_t, 256> bytes{};
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto count = port.read(bytes, 200ms);
        if (!decoder.push(std::span(bytes).first(count)).empty()) {
            return true;
        }
    }
    return false;
}

void monitor(rotator::SerialPort& port, unsigned int seconds) {
    rotator::WitFrameDecoder decoder;
    rotator::WitSample sample;
    std::array<std::uint8_t, 256> bytes{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    auto next_print = std::chrono::steady_clock::now();
    bool received = false;
    std::size_t byte_count = 0;

    std::cout << "roll,pitch,yaw,accel_x,accel_y,accel_z,mag_x,mag_y,mag_z\n";
    while (std::chrono::steady_clock::now() < deadline) {
        const auto count = port.read(bytes, 200ms);
        byte_count += count;
        for (const auto& frame : decoder.push(std::span(bytes).first(count))) {
            received = apply_wit_frame(frame, sample) || received;
        }
        const auto now = std::chrono::steady_clock::now();
        if (sample.has_angle && now >= next_print) {
            std::cout << std::fixed << std::setprecision(3) << sample.angle_degrees[0] << ','
                      << sample.angle_degrees[1] << ',' << sample.angle_degrees[2] << ','
                      << sample.acceleration_g[0] << ',' << sample.acceleration_g[1] << ','
                      << sample.acceleration_g[2] << ',' << sample.magnetic_field[0] << ','
                      << sample.magnetic_field[1] << ',' << sample.magnetic_field[2] << '\n';
            next_print = now + 250ms;
        }
    }
    if (!received) {
        if (byte_count == 0) {
            throw std::runtime_error(
                "serial port produced no bytes; turn the sensor switch on and verify the device path");
        }
        throw std::runtime_error("received " + std::to_string(byte_count) +
                                 " serial bytes but no valid WitMotion frames; run the raw command");
    }
}

void raw_monitor(rotator::SerialPort& port, unsigned int seconds) {
    std::array<std::uint8_t, 256> bytes{};
    std::size_t byte_count = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto count = port.read(bytes, 200ms);
        for (std::size_t i = 0; i < count; ++i) {
            if (byte_count % 16 == 0) {
                std::cout << std::setw(8) << std::setfill('0') << std::hex << byte_count << ": ";
            }
            std::cout << std::setw(2) << static_cast<unsigned int>(bytes[i]) << ' ';
            ++byte_count;
            if (byte_count % 16 == 0) {
                std::cout << '\n';
            }
        }
    }
    if (byte_count % 16 != 0) {
        std::cout << '\n';
    }
    std::cout << std::dec << byte_count << " bytes received\n";
}

void calibrate_accelerometer(rotator::SerialPort& port) {
    if (!wait_for_sensor(port, 3s)) {
        throw std::runtime_error("sensor did not produce valid frames; calibration not started");
    }
    std::cout << "Keep the WT901 level, motionless, and away from vibration.\n"
              << "Starting accelerometer calibration...\n";
    send(port, rotator::wit_unlock_command());
    send(port, rotator::wit_accelerometer_calibration_command());
    std::this_thread::sleep_for(5500ms);
    send(port, rotator::wit_save_command());
    std::cout << "Accelerometer calibration saved. Verify that stationary Z acceleration is near 1 g.\n";
}

void calibrate_magnetic(rotator::SerialPort& port, unsigned int seconds) {
    if (!wait_for_sensor(port, 3s)) {
        throw std::runtime_error("sensor did not produce valid frames; calibration not started");
    }
    std::cout << "Keep the sensor at least 20 cm from iron and magnetic objects.\n"
              << "For the next " << seconds
              << " seconds, slowly rotate it through a full turn around X, Y, and Z.\n";
    send(port, rotator::wit_unlock_command());
    send(port, rotator::wit_magnetic_calibration_command());
    try {
        monitor(port, seconds);
    } catch (...) {
        try {
            send(port, rotator::wit_unlock_command());
            send(port, rotator::wit_normal_command());
            send(port, rotator::wit_save_command());
        } catch (const std::exception&) {
        }
        throw;
    }
    send(port, rotator::wit_unlock_command());
    send(port, rotator::wit_normal_command());
    send(port, rotator::wit_save_command());
    std::cout << "Magnetic calibration saved. Point sensor Y toward north and verify yaw near zero.\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        rotator::SerialPort port(options.device, options.baud);
        if (options.command == "monitor") {
            monitor(port, options.seconds);
        } else if (options.command == "raw") {
            raw_monitor(port, options.seconds);
        } else if (options.command == "calibrate-accel") {
            calibrate_accelerometer(port);
        } else if (options.command == "calibrate-magnetic") {
            calibrate_magnetic(port, options.seconds);
        } else {
            usage();
            return 2;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "witmotion-tool: " << error.what() << '\n';
        usage();
        return 1;
    }
}
