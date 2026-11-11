/**
 * @file otp_confirm_integer.cpp
 * @brief Must NOT compile: the irreversible OTP burn takes a typed confirmation, so an integer
 * cannot stand in for it (handbook KB-CPP-045, KB-CPP20-030).
 *
 * EXPECT: OtpBurnConfirm
 */
#include "as5047u.hpp"
#include "mock_as5047u.hpp"

int main() {
  as5047u_test::MinimalPort port;
  as5047u::AS5047U<as5047u_test::MinimalPort> sensor(port);
  as5047u::OtpReport report{};
  return static_cast<int>(sensor.ProgramOtp(0x0B7B'0B7BU, report).error);
}
