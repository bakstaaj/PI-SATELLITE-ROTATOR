#pragma once

#include "rotator/motor_backend.hpp"

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace rotator {

struct Position {
    double azimuth{0.0};
    double elevation{0.0};
    bool moving{false};
};

enum class SensorAction {
    calibrate_accelerometer,
    magnetic_calibration_start,
    magnetic_calibration_finish
};

struct ControllerStatus {
    double azimuth{0.0};
    double elevation{0.0};
    double target_azimuth{0.0};
    double target_elevation{0.0};
    bool moving{false};
    bool external_feedback{false};
    bool feedback_received{false};
    bool feedback_stale{false};
    long long feedback_age_ms{-1};
    bool sensor_stream_received{false};
    long long sensor_stream_age_ms{-1};
    bool fault{false};
    std::string fault_reason;
    std::string motor_backend{"simulator"};
    bool motor_fault{false};
    std::string motor_fault_reason;
    bool sensor_maintenance{false};
    std::string sensor_maintenance_reason;
};

class RotatorController {
public:
    bool set_target(std::optional<double> azimuth,
                    std::optional<double> elevation,
                    std::string& error);
    Position position() const;
    ControllerStatus status() const;
    void stop();
    bool zero_current_position();
    void enable_external_feedback();
    void set_feedback_timeout(std::chrono::milliseconds timeout);
    void set_motor_driver(std::shared_ptr<MotorDriver> driver);
    void service_safety();
    bool update_feedback(double azimuth, double elevation);
    bool request_sensor_action(SensorAction action, std::string& error);
    std::optional<SensorAction> take_sensor_action();
    void begin_sensor_maintenance(std::chrono::milliseconds duration,
                                  std::string reason);

private:
    void update_motion_state_locked();
    void apply_motor_command_locked();
    void stop_motor_locked();
    bool feedback_stale_locked(std::chrono::steady_clock::time_point now) const;
    bool sensor_maintenance_locked(std::chrono::steady_clock::time_point now) const;
    ControllerStatus status_locked(std::chrono::steady_clock::time_point now) const;

    mutable std::mutex mutex_;
    Position state_;
    Position target_;
    Position raw_feedback_;
    Position feedback_zero_;
    std::chrono::steady_clock::time_point last_feedback_time_{};
    std::chrono::steady_clock::time_point last_sensor_frame_time_{};
    std::chrono::milliseconds feedback_timeout_{1000};
    std::string last_feedback_error_;
    std::optional<SensorAction> pending_sensor_action_;
    std::chrono::steady_clock::time_point sensor_maintenance_until_{};
    std::string sensor_maintenance_reason_;
    std::shared_ptr<MotorDriver> motor_driver_;
    std::string motor_backend_{"simulator"};
    bool external_feedback_{false};
    bool feedback_received_{false};
    bool sensor_frame_received_{false};
    bool motion_commanded_{false};
    bool motor_fault_{false};
    std::string motor_fault_reason_;
};

}  // namespace rotator
