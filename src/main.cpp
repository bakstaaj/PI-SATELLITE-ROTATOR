#include "rotator/tcp_server.hpp"
#include "rotator/web_server.hpp"
#include "rotator/witmotion.hpp"

#include <array>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
std::atomic_bool stopping{false};
void handle_signal(int) { stopping.store(true); }

enum class SensorAxis : std::size_t { roll = 0, pitch = 1, yaw = 2 };

struct Options {
    std::uint16_t port{4553};
    std::uint16_t web_port{8080};
    bool web_enabled{true};
    std::string sensor_device;
    unsigned int sensor_baud{115200};
    SensorAxis azimuth_axis{SensorAxis::yaw};
    SensorAxis elevation_axis{SensorAxis::roll};
    double azimuth_offset{0.0};
    double elevation_offset{0.0};
    double azimuth_sign{1.0};
    double elevation_sign{1.0};
};

SensorAxis parse_axis(const std::string& value) {
    if (value == "roll") {
        return SensorAxis::roll;
    }
    if (value == "pitch") {
        return SensorAxis::pitch;
    }
    if (value == "yaw") {
        return SensorAxis::yaw;
    }
    throw std::invalid_argument("axis must be roll, pitch, or yaw");
}

int integer(const char* value, const std::string& name) {
    try {
        std::size_t used = 0;
        const int parsed = std::stoi(value, &used);
        if (used != std::string(value).size()) {
            throw std::invalid_argument(name);
        }
        return parsed;
    } catch (const std::exception&) {
        throw std::invalid_argument("invalid " + name + ": " + value);
    }
}

double number(const char* value, const std::string& name) {
    try {
        std::size_t used = 0;
        const double parsed = std::stod(value, &used);
        if (used != std::string(value).size()) {
            throw std::invalid_argument(name);
        }
        return parsed;
    } catch (const std::exception&) {
        throw std::invalid_argument("invalid " + name + ": " + value);
    }
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--port" && i + 1 < argc) {
            const int value = integer(argv[++i], "TCP port");
            if (value < 1 || value > 65535) {
                throw std::invalid_argument("TCP port must be 1-65535");
            }
            options.port = static_cast<std::uint16_t>(value);
        } else if (argument == "--web-port" && i + 1 < argc) {
            const int value = integer(argv[++i], "web port");
            if (value < 1 || value > 65535) {
                throw std::invalid_argument("web port must be 1-65535");
            }
            options.web_port = static_cast<std::uint16_t>(value);
        } else if (argument == "--no-web") {
            options.web_enabled = false;
        } else if (argument == "--sensor" && i + 1 < argc) {
            options.sensor_device = argv[++i];
        } else if (argument == "--sensor-baud" && i + 1 < argc) {
            const int value = integer(argv[++i], "sensor baud rate");
            if (value <= 0) {
                throw std::invalid_argument("sensor baud rate must be positive");
            }
            options.sensor_baud = static_cast<unsigned int>(value);
        } else if (argument == "--az-axis" && i + 1 < argc) {
            options.azimuth_axis = parse_axis(argv[++i]);
        } else if (argument == "--el-axis" && i + 1 < argc) {
            options.elevation_axis = parse_axis(argv[++i]);
        } else if (argument == "--az-offset" && i + 1 < argc) {
            options.azimuth_offset = number(argv[++i], "azimuth offset");
        } else if (argument == "--el-offset" && i + 1 < argc) {
            options.elevation_offset = number(argv[++i], "elevation offset");
        } else if (argument == "--az-invert") {
            options.azimuth_sign = -1.0;
        } else if (argument == "--el-invert") {
            options.elevation_sign = -1.0;
        } else {
            throw std::invalid_argument("unknown or incomplete option: " + argument);
        }
    }
    return options;
}

void print_usage() {
    std::cerr
        << "usage: pi-satellite-rotator [--port PORT] [--sensor DEVICE]\n"
        << "       [--web-port PORT] [--no-web]\n"
        << "       [--sensor-baud RATE] [--az-axis roll|pitch|yaw]\n"
        << "       [--el-axis roll|pitch|yaw] [--az-offset DEG] [--el-offset DEG]\n"
        << "       [--az-invert] [--el-invert]\n";
}

void read_sensor(rotator::SerialPort port, const Options& options,
                 rotator::RotatorController& controller) {
    try {
        rotator::WitFrameDecoder decoder;
        rotator::WitSample sample;
        std::array<std::uint8_t, 256> bytes{};
        std::size_t dropped_elevation_frames = 0;
        bool elevation_mapping_valid = true;
        auto next_mapping_warning = std::chrono::steady_clock::now();
        while (!stopping.load()) {
            const auto count = port.read(bytes, std::chrono::milliseconds(200));
            for (const auto& frame : decoder.push(std::span(bytes).first(count))) {
                if (!rotator::apply_wit_frame(frame, sample) ||
                    (frame.type != static_cast<std::uint8_t>(rotator::WitFrameType::angle) &&
                     frame.type !=
                         static_cast<std::uint8_t>(rotator::WitFrameType::combined_motion))) {
                    continue;
                }
                const double azimuth = rotator::normalize_azimuth(
                    options.azimuth_sign *
                        sample.angle_degrees[static_cast<std::size_t>(options.azimuth_axis)] +
                    options.azimuth_offset);
                const double elevation =
                    options.elevation_sign *
                        sample.angle_degrees[static_cast<std::size_t>(options.elevation_axis)] +
                    options.elevation_offset;
                if (!controller.update_feedback(azimuth, elevation)) {
                    ++dropped_elevation_frames;
                    elevation_mapping_valid = false;
                    const auto now = std::chrono::steady_clock::now();
                    if (now >= next_mapping_warning) {
                        std::cerr << "WT901 elevation mapping is outside 0-180 degrees; dropped "
                                  << dropped_elevation_frames
                                  << " frame(s). Check --el-axis, --el-offset, and --el-invert\n";
                        dropped_elevation_frames = 0;
                        next_mapping_warning = now + std::chrono::seconds(10);
                    }
                } else if (!elevation_mapping_valid) {
                    std::cerr << "WT901 elevation mapping recovered\n";
                    elevation_mapping_valid = true;
                    dropped_elevation_frames = 0;
                }
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "WT901 reader stopped: " << error.what() << '\n';
        controller.stop();
        stopping.store(true);
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);

        rotator::RotatorController controller;
        std::thread sensor_thread;
        std::thread web_thread;
        if (!options.sensor_device.empty()) {
            rotator::SerialPort sensor(options.sensor_device, options.sensor_baud);
            controller.enable_external_feedback();
            sensor_thread = std::thread(read_sensor, std::move(sensor), std::cref(options),
                                        std::ref(controller));
            std::cout << "WT901 input: " << options.sensor_device << " at "
                      << options.sensor_baud << " baud\n";
        }

        if (options.web_enabled) {
            web_thread = std::thread([&options] {
                rotator::WebServer web(options.web_port, "127.0.0.1", options.port);
                if (web.run(stopping) != 0) {
                    stopping.store(true);
                }
            });
        }

        rotator::TcpServer server(options.port, controller);
        const int result = server.run(stopping);
        stopping.store(true);
        if (sensor_thread.joinable()) {
            sensor_thread.join();
        }
        if (web_thread.joinable()) {
            web_thread.join();
        }
        return result;
    } catch (const std::exception& error) {
        std::cerr << "pi-satellite-rotator: " << error.what() << '\n';
        print_usage();
        return 2;
    }
}
