#include "rotator/motor_backend.hpp"

namespace rotator {

std::string NullMotorDriver::name() const { return "simulator"; }

void NullMotorDriver::apply(MotorCommand command) { last_command_ = command; }

void NullMotorDriver::stop() { last_command_ = MotorCommand{}; }

MotorCommand NullMotorDriver::last_command() const { return last_command_; }

}  // namespace rotator
