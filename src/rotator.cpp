#include "rotator/rotator.hpp"

#include <algorithm>
#include <cmath>

namespace rotator {
namespace {
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
    if (external_feedback_ && !feedback_received_) {
        error = "valid sensor feedback is required before motion commands";
        return false;
    }
    if (feedback_stale_locked(now)) {
        error = "sensor feedback is stale";
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
}

bool RotatorController::zero_current_position() {
    std::lock_guard lock(mutex_);
    if (state_.moving || (external_feedback_ && !feedback_received_) ||
        feedback_stale_locked(std::chrono::steady_clock::now())) {
        return false;
    }
    motion_commanded_ = false;
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
}

void RotatorController::set_feedback_timeout(std::chrono::milliseconds timeout) {
    std::lock_guard lock(mutex_);
    feedback_timeout_ = timeout.count() > 0 ? timeout : std::chrono::milliseconds(1000);
}

bool RotatorController::update_feedback(double azimuth, double elevation) {
    if (azimuth < 0.0 || azimuth >= 360.0 || elevation < 0.0 || elevation > 180.0) {
        std::lock_guard lock(mutex_);
        last_feedback_error_ = "feedback outside sensor range";
        return false;
    }

    std::lock_guard lock(mutex_);
    const double mapped_azimuth = normalize_azimuth(azimuth - feedback_zero_.azimuth);
    const double mapped_elevation = elevation - feedback_zero_.elevation;
    constexpr double boundary_tolerance = 1.0;
    if (mapped_elevation < -boundary_tolerance ||
        mapped_elevation > 180.0 + boundary_tolerance) {
        last_feedback_error_ = "mapped elevation outside 0-180 degrees";
        return false;
    }

    raw_feedback_.azimuth = azimuth;
    raw_feedback_.elevation = elevation;
    feedback_received_ = true;
    last_feedback_time_ = std::chrono::steady_clock::now();
    last_feedback_error_.clear();
    state_.azimuth = mapped_azimuth;
    state_.elevation = std::clamp(mapped_elevation, 0.0, 180.0);
    update_motion_state_locked();
    return true;
}

void RotatorController::update_motion_state_locked() {
    constexpr double deadband = 0.5;
    state_.moving = external_feedback_ && motion_commanded_ && feedback_received_ &&
                    !feedback_stale_locked(std::chrono::steady_clock::now()) &&
                    (std::abs(azimuth_error(target_.azimuth, state_.azimuth)) > deadband ||
                     std::abs(target_.elevation - state_.elevation) > deadband);
}

bool RotatorController::feedback_stale_locked(
    std::chrono::steady_clock::time_point now) const {
    return external_feedback_ && feedback_received_ &&
           now - last_feedback_time_ > feedback_timeout_;
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
    status.feedback_stale = feedback_stale_locked(now);
    status.moving = state_.moving && !status.feedback_stale;
    if (feedback_received_) {
        status.feedback_age_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_feedback_time_)
                .count();
    }
    status.fault = status.feedback_stale;
    if (status.feedback_stale) {
        status.fault_reason = "stale sensor feedback";
    } else {
        status.fault_reason = last_feedback_error_;
    }
    return status;
}

}  // namespace rotator
