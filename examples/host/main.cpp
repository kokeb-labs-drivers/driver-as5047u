/**
 * @file main.cpp
 * @brief The AS5047U driver on your PC: initialise a simulated chip and read the angle as
 * the simulated shaft turns.
 *
 * No hardware and no SDK: the host port (host_as5047u_port.hpp) connects the real driver to the
 * register model the host tests use. The same calls run on an MCU with that MCU's port
 * (examples/esp32/ for ESP-IDF). Exit code 0 when every call succeeded.
 */
#include <cstdint>
#include <cstdio>

#include "as5047u.hpp"
#include "host_as5047u_port.hpp"

int main() {
  HostAs5047uPort port;
  as5047u::AS5047U<HostAs5047uPort> sensor(port); // no I/O yet

  std::printf("AS5047U driver %s on the host\n", as5047u::GetDriverVersion());
  if (const auto r = sensor.EnsureInitialized(); !r) {
    std::printf("EnsureInitialized failed: %s\n", as5047u::ToString(r.error));
    return 1;
  }

  // The simulated shaft turns by 30 degrees between samples.
  for (int i = 0; i < 4; ++i) {
    port.state.chip.raw_angle = static_cast<uint16_t>(i * 16384 / 12);
    const auto a = sensor.ReadAngleDegrees();
    const auto agc = sensor.ReadAgc();
    if (!a || !agc) {
      std::printf("read failed: %s\n", as5047u::ToString(a ? agc.error : a.error));
      return 1;
    }
    std::printf("angle %7.2f deg  AGC %3u\n", static_cast<double>(a.value),
                static_cast<unsigned>(agc.value));
  }
  return 0;
}
