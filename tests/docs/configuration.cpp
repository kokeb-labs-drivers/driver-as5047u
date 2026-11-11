/**
 * @file configuration.cpp
 * @brief The Configuration page's examples, compiled by the host build (never run).
 */
#include "as5047u.hpp"
#include "as5047u_kconfig.hpp"
#include "my_port.hpp"

// --8<-- [start:custom]
as5047u::Config EncoderConfig() {
  as5047u::Config c{}; // reset values of an unprogrammed part
  c.direction = as5047u::Direction::CounterClockwise;
  c.abi_resolution_code = 4; // 14-bit ABI: 4096 pulses per revolution
  c.uvw_pole_pairs = 7;      // BLDC commutation for a 7-pole-pair motor
  return c;                  // as5047u::Validate(c) checks it without touching the chip
}

as5047u::Error StartAsEncoder(MyPort& port) {
  static as5047u::AS5047U<MyPort> sensor(port, EncoderConfig()); // stored, no I/O
  return sensor.EnsureInitialized().error; // InvalidArgument if the config is out of range
}
// --8<-- [end:custom]

// --8<-- [start:burned]
as5047u::Error StartBurnedPart(as5047u::AS5047U<MyPort>& sensor) {
  as5047u::Config c{};
  c.write_settings = false;                 // keep what the OTP holds and read it into GetConfig()
  if (auto r = sensor.ApplyConfig(c); !r) { // before the start: stored, no I/O
    return r.error;
  }
  return sensor.EnsureInitialized().error;
}
// --8<-- [end:burned]

// --8<-- [start:kconfig]
as5047u::Error StartFromMenuconfig(MyPort& port) {
  static as5047u::AS5047U<MyPort> sensor(port, as5047u::KconfigDefaults());
  return sensor.EnsureInitialized().error;
}
// --8<-- [end:kconfig]
