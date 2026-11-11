/**
 * @file host_as5047u_port.hpp
 * @brief The host port: the driver's bus hooks over a simulated AS5047U, so the example runs on any
 * PC.
 *
 * `as5047u_test::MockPortBase` (tests/mock_as5047u_bus.hpp) supplies the required bus hooks over
 * the register model the host tests use. This class adds what a real port adds: `DelayUs()` (here
 * it advances the simulated chip's clock instead of waiting) and `Log()` to the console. A port for
 * a real bus on a PC (Linux spidev, a USB bridge) implements the same hooks: see docs/porting.md.
 */
#ifndef AS5047U_HOST_AS5047U_PORT_HPP
#define AS5047U_HOST_AS5047U_PORT_HPP

#include <cstdint>
#include <cstdio>

#include "mock_as5047u.hpp"

/// The host port: simulated chip, real console.
class HostAs5047uPort : public as5047u_test::MockPortBase<HostAs5047uPort> {
public:
  /// The bus settings a real port would use; checked against the driver's bus requirements
  /// (handbook KB-CPP-123) like any port's.
  static constexpr uint32_t kClockHz = 1'000'000;
  static constexpr uint8_t kSpiMode = 1;
  static_assert(kClockHz <= as5047u::kMaxClockHz, "clock above the AS5047U's 10 MHz");
  static_assert(kSpiMode == as5047u::kSpiMode, "the AS5047U uses SPI mode 1");
  static_assert(as5047u::kMsbFirst, "a real port sends MSB first");

  /// Time passes in the simulated chip; nothing sleeps.
  void DelayUs(uint32_t us) noexcept {
    state.chip.Elapse(us);
  }

  /// Driver messages (they carry their own `as5047u:` prefix) go to the console, one line each.
  void Log(as5047u::LogLevel level, const char* message) noexcept {
    (void)level;
    (void)std::puts(message); // plain text: never pass it as a format string
  }
};

#endif // AS5047U_HOST_AS5047U_PORT_HPP
