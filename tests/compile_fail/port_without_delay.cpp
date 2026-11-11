/**
 * @file port_without_delay.cpp
 * @brief Must NOT compile: start-up polls DIA once per millisecond and the OTP burn is polled the
 * same way, so every port needs DelayUs().
 *
 * EXPECT: DelayUs
 */
#include "as5047u.hpp"
#include "mock_as5047u.hpp"

int main() {
  as5047u_test::BarePort port;
  as5047u::AS5047U<as5047u_test::BarePort> sensor(port);
  return static_cast<int>(sensor.EnsureInitialized().error);
}
