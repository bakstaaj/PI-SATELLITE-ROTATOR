#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>

namespace rotator {

struct Position {
    double azimuth{0.0};
    double elevation{0.0};
    bool moving{false};
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
    bool fault{false};
    std::string fault_reason;
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
    bool update_feedback(double azimuth, double elevation);

private:
    void update_motion_state_locked();
    bool feedback_stale_locked(std::chrono::steady_clock::time_point now) const;
    ControllerStatus status_locked(std::chrono::steady_clock::time_point now) const;

    mutable std::mutex mutex_;
    Position state_;
    Position target_;
    Position raw_feedback_;
    Position feedback_zero_;
    std::chrono::steady_clock::time_point last_feedback_time_{};
    std::chrono::milliseconds feedback_timeout_{1000};
    std::string last_feedback_error_;
    bool external_feedback_{false};
    bool feedback_received_{false};
    bool motion_commanded_{false};
};

}  // namespace rotator
