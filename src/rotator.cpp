#include "rotator/rotator.hpp"

#include <algorithm>
#include <cmath>
#include <exception>

namespace rotator {
namespace {
constexpr double deadband = 0.5;

double azimuth_error(double target, double actual) {
    double error = target - actual;
    while (error > 180.0) {
        error -= 360.0;
    }
    while (error < -180.0) {
        error += 360.0;
    }
    return error;
}

double normalize_azimuth(double value) {
    while (value >= 360.0) {
        value -= 360.0;
    }
    while (value < 0.0) {
        value += 360.0;
    }
    return value;
}

AxisDirection direction_from_error(double error) {
    if (std::abs(error) <= deadband) {
        return AxisDirection::stopped;
    }
    return error > 0.0 ? AxisDirection::positive : AxisDirection::negative;
}

}  // namespace

bool RotatorController::set_target(std::optional<double> azimuth,
                                   std::optional<double> elevation,
                                   std::string& error) {
    if (azimuth && (*azimuth < 0.0 || *azimuth > 359.0)) {
        error = "azimuth must be between 0 and 359 degrees";
        return false;
    }
    if (elevation && (*elevation < 0.0 || *elevation > 180.0)) {
        error = "elevation must be between 0 and 180 degrees";
        return false;
    }

    std::lock_guard lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    if (motor_fault_) {
        error = "motor backend fault: " + motor_fault_reason_;
        return false;
    }
    if (external_feedback_ && !feedback_received_) {
        error = "valid sensor feedback is required before motion commands";
        return false;
    }
    if (sensor_maintenance_locked(now)) {
        error = "sensor maintenance in progress";
        stop_motor_locked();
        return false;
    }
    if (feedback_stale_locked(now)) {
        error = "sensor feedback is stale";
        stop_motor_locked();
        return false;
    }
    if (azimuth) {
        target_.azimuth = *azimuth;
    }
    if (elevation) {
        target_.elevation = *elevation;
    }
    motion_commanded_ = true;
    if (!external_feedback_) {
        state_.azimuth = target_.azimuth;
        state_.elevation = target_.elevation;
        motion_commanded_ = false;
    }
    update_motion_state_locked();
    return true;
}

Position RotatorController::position() const {
    std::lock_guard lock(mutex_);
    Position result = state_;
    if (feedback_stale_locked(std::chrono::steady_clock::now())) {
        result.moving = false;
    }
    return result;
}

ControllerStatus RotatorController::status() const {
    std::lock_guard lock(mutex_);
    return status_locked(std::chrono::steady_clock::now());
}

void RotatorController::stop() {
    std::lock_guard lock(mutex_);
    motion_commanded_ = false;
    state_.moving = false;
    stop_motor_locked();
}

bool RotatorController::zero_current_position() {
    std::lock_guard lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    if (state_.moving || (external_feedback_ && !feedback_received_) ||
        sensor_maintenance_locked(now) || feedback_stale_locked(now)) {
        return false;
    }
    motion_commanded_ = false;
    stop_motor_locked();
    if (external_feedback_) {
        feedback_zero_.azimuth = raw_feedback_.azimuth;
        feedback_zero_.elevation = raw_feedback_.elevation;
    }
    state_.azimuth = 0.0;
    state_.elevation = 0.0;
    state_.moving = false;
    target_ = state_;
    return true;
}

void RotatorController::enable_external_feedback() {
    std::lock_guard lock(mutex_);
    external_feedback_ = true;
    motion_commanded_ = false;
    state_.moving = false;
    feedback_received_ = false;
    last_feedback_error_.clear();
    stop_motor_locked();
}

void RotatorController::set_feedback_timeout(std::chrono::milliseconds timeout) {
    std::lock_guard lock(mutex_);
    feedback_timeout_ = timeout.count() > 0 ? timeout : std::chrono::milliseconds(1000);
}

void RotatorController::set_motor_driver(std::shared_ptr<MotorDriver> driver) {
    std::lock_guard lock(mutex_);
    motor_driver_ = std::move(driver);
    motor_backend_ = motor_driver_ ? motor_driver_->name() : "simulator";
    stop_motor_locked();
}

void RotatorController::service_safety() {
    std::lock_guard lock(mutex_);
    update_motion_state_locked();
}

bool RotatorController::request_sensor_action(SensorAction action, std::string& error) {
    std::lock_guard lock(mutex_);
    if (!external_feedback_) {
        error = "WT901 sensor is not enabled";
        return false;
    }
    if (state_.moving || motion_commanded_) {
        error = "stop motion before sensor calibration";
        return false;
    }
    pending_sensor_action_ = action;
    return true;
}

std::optional<SensorAction> RotatorController::take_sensor_action() {
    std::lock_guard lock(mutex_);
    auto action = pending_sensor_action_;
    pending_sensor_action_.reset();
    return action;
}


void RotatorController::begin_sensor_maintenance(std::chrono::milliseconds duration,
                                                 std::string reason) {
    std::lock_guard lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    sensor_maintenance_until_ = now + (duration.count() > 0 ? duration
                                                             : std::chrono::milliseconds(1000));
    sensor_maintenance_reason_ = std::move(reason);
    motion_commanded_ = false;
    state_.moving = false;
    stop_motor_locked();
}

bool RotatorController::update_feedback(double azimuth, double elevation) {
    std::lock_guard lock(mutex_);
    const auto now = std::chrono::steady_clock::now();

    sensor_frame_received_ = true;
    last_sensor_frame_time_ = now;

    if (azimuth < 0.0 || azimuth >= 360.0 || elevation < -180.0 || elevation > 180.0) {
        last_feedback_error_ = "raw sensor feedback outside expected attitude range";
        return false;
    }

    const double mapped_azimuth = normalize_azimuth(azimuth - feedback_zero_.azimuth);
    const double mapped_elevation = elevation - feedback_zero_.elevation;
    constexpr double measured_elevation_lower_limit = -90.0;
    constexpr double measured_elevation_upper_limit = 180.0;
    if (mapped_elevation < measured_elevation_lower_limit ||
        mapped_elevation > measured_elevation_upper_limit) {
        last_feedback_error_ = "mapped measured elevation outside -90 to 180 degrees";
        return false;
    }

    raw_feedback_.azimuth = azimuth;
    raw_feedback_.elevation = elevation;
    feedback_received_ = true;
    last_feedback_time_ = now;
    sensor_maintenance_until_ = {};
    sensor_maintenance_reason_.clear();
    last_feedback_error_.clear();
    state_.azimuth = mapped_azimuth;
    state_.elevation = mapped_elevation;
    update_motion_state_locked();
    return true;
}

void RotatorController::update_motion_state_locked() {
    const auto now = std::chrono::steady_clock::now();
    const bool stale = feedback_stale_locked(now);
    state_.moving = external_feedback_ && motion_commanded_ && feedback_received_ && !stale &&
                    !motor_fault_ &&
                    (std::abs(azimuth_error(target_.azimuth, state_.azimuth)) > deadband ||
                     std::abs(target_.elevation - state_.elevation) > deadband);
    apply_motor_command_locked();
}

void RotatorController::apply_motor_command_locked() {
    if (!motor_driver_) {
        return;
    }

    MotorCommand command;
    if (state_.moving) {
        command.azimuth = direction_from_error(azimuth_error(target_.azimuth, state_.azimuth));
        command.elevation = direction_from_error(target_.elevation - state_.elevation);
    }

    try {
        if (command.any_motion()) {
            motor_driver_->apply(command);
        } else {
            motor_driver_->stop();
        }
    } catch (const std::exception& error) {
        motor_fault_ = true;
        motor_fault_reason_ = error.what();
        motion_commanded_ = false;
        state_.moving = false;
    }
}

void RotatorController::stop_motor_locked() {
    if (!motor_driver_) {
        return;
    }

    try {
        motor_driver_->stop();
    } catch (const std::exception& error) {
        motor_fault_ = true;
        motor_fault_reason_ = error.what();
    }
}

bool RotatorController::feedback_stale_locked(
    std::chrono::steady_clock::time_point now) const {
    return external_feedback_ && feedback_received_ &&
           now - last_feedback_time_ > feedback_timeout_;
}

bool RotatorController::sensor_maintenance_locked(
    std::chrono::steady_clock::time_point now) const {
    return sensor_maintenance_until_ != std::chrono::steady_clock::time_point{} &&
           now <= sensor_maintenance_until_;
}

ControllerStatus RotatorController::status_locked(
    std::chrono::steady_clock::time_point now) const {
    ControllerStatus status;
    status.azimuth = state_.azimuth;
    status.elevation = state_.elevation;
    status.target_azimuth = target_.azimuth;
    status.target_elevation = target_.elevation;
    status.external_feedback = external_feedback_;
    status.feedback_received = feedback_received_;
    status.sensor_stream_received = sensor_frame_received_;
    if (sensor_frame_received_) {
        status.sensor_stream_age_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_sensor_frame_time_)
                .count();
    }
    status.feedback_stale = feedback_stale_locked(now);
    status.sensor_maintenance = sensor_maintenance_locked(now);
    if (status.sensor_maintenance) {
        status.sensor_maintenance_reason = sensor_maintenance_reason_;
    }
    status.moving = state_.moving && !status.feedback_stale && !motor_fault_ &&
                    !status.sensor_maintenance;
    status.motor_backend = motor_backend_;
    status.motor_fault = motor_fault_;
    status.motor_fault_reason = motor_fault_reason_;
    if (feedback_received_) {
        status.feedback_age_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_feedback_time_)
                .count();
    }
    status.fault = (status.feedback_stale && !status.sensor_maintenance) || status.motor_fault;
    if (status.sensor_maintenance) {
        status.fault_reason.clear();
    } else if (status.feedback_stale) {
        status.fault_reason = "stale sensor feedback";
    } else if (status.motor_fault) {
        status.fault_reason = "motor backend fault: " + motor_fault_reason_;
    } else {
        status.fault_reason = last_feedback_error_;
    }
    return status;
}

}  // namespace rotator
