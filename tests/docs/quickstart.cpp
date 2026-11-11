/**
 * @file quickstart.cpp
 * @brief The Quickstart page's example, compiled by the host build (never run).
 */
#include "my_port.hpp"

// --8<-- [start:quickstart]
#include "as5047u.hpp"

int read_shaft_angle(MyPort& port, float& degrees) {
  as5047u::AS5047U<MyPort> sensor(port); // no I/O yet

  if (auto r = sensor.EnsureInitialized(); !r) { // 1. bus up, wait for ready, Config{} written
    return static_cast<int>(r.error);            //    Bus or NoDevice: check wiring, SPI mode 1
  }
  const auto a = sensor.ReadAngleDegrees(); // 2. compensated angle, CRC-checked frame
  if (!a) {
    return static_cast<int>(a.error); //    MagnetWarning: magnet missing; DeviceFault: chip
  }
  degrees = a.value; // 3. [0, 360), 14-bit resolution (0.022 degrees)
  return 0;
}
// --8<-- [end:quickstart]
