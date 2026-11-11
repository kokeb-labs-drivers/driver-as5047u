/**
 * @file not_a_port.cpp
 * @brief Must NOT compile: the driver is constrained by the port concept.
 *
 * EXPECT: Port
 */
#include "as5047u.hpp"

struct NotAPort {
  bool EnsureInitialized() noexcept {
    return true;
  }
};

int main() {
  NotAPort port;
  as5047u::AS5047U<NotAPort> sensor(port);
  return static_cast<int>(sensor.EnsureInitialized().error);
}
