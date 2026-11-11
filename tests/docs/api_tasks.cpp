/**
 * @file api_tasks.cpp
 * @brief The API guide's examples, compiled by the host build (never run).
 */
#include <cstdio>

#include "as5047u.hpp"
#include "my_port.hpp"

using Sensor = as5047u::AS5047U<MyPort>;

// --8<-- [start:lifecycle]
as5047u::Error BringUp(Sensor& sensor) {
  // The one entry point: starts the chip on the first call, then returns Ok with no I/O. After a
  // failure the next call retries from the beginning.
  const auto r = sensor.EnsureInitialized();
  if (!r) {
    std::printf("AS5047U init: %s\n", as5047u::ToString(r.error));
  }
  return r.error;
}
// --8<-- [end:lifecycle]

// --8<-- [start:control-loop]
// One control-loop sample: angle and speed. Each read is two CRC-checked frames; two more only
// when the chip flags the reply. A read under a magnet warning fails with MagnetWarning.
bool Sample(Sensor& sensor, float& angle_deg, float& speed_rpm) {
  const auto a = sensor.ReadAngleDegrees();
  const auto v = sensor.ReadVelocityRpm();
  if (!a || !v) {
    return false; // never use .value of a failed read: it is 0, not a measurement
  }
  angle_deg = a.value;
  speed_rpm = v.value;
  return true;
}
// --8<-- [end:control-loop]

// --8<-- [start:zero]
as5047u::Result<uint16_t> ZeroAtTheCurrentPosition(Sensor& sensor) {
  return sensor.SetZeroHere(); // the shaft's current position now reads 0
}
// --8<-- [end:zero]

// --8<-- [start:verify]
// Off a timed path (commissioning, a rarely changed setting): read the write back as well.
as5047u::Error SetDirectionChecked(Sensor& sensor) {
  // Two more frames per register; WriteMismatch if the chip lost the write.
  return sensor.SetDirection(as5047u::Direction::CounterClockwise, as5047u::Verify::ReadBack).error;
}
// --8<-- [end:verify]

// --8<-- [start:health]
bool MagnetIsHealthy(Sensor& sensor) {
  const auto agc = sensor.ReadAgc();      // mid-range is best: 0 too strong, 255 too weak
  const auto f = sensor.ReadErrorFlags(); // clears ERRFL on the chip; also ORed into the latch
  return agc && f && !f.value.agc_warning && !f.value.mag_half;
}

// Everything ERRFL reported since the last check, including flags the driver's own reads
// consumed (a read that failed with DeviceFault or MagnetWarning read ERRFL inside). No I/O.
bool AnyFieldWarningSinceLastCheck(Sensor& sensor) {
  const as5047u::ErrorFlags seen = sensor.GetLatchedErrorFlags();
  sensor.ClearLatchedErrorFlags();
  return seen.agc_warning || seen.mag_half;
}
// --8<-- [end:health]

// --8<-- [start:reset]
// A supervision tick, off the timed path: 14 frames. A reset of the chip (a supply dip) reloads
// its settings from the OTP; the driver then reports DeviceReset, here or from any read whose
// ERRFL says the offset compensation restarted, and EnsureInitialized() writes them again.
as5047u::Error Supervise(Sensor& sensor) {
  const auto r = sensor.CheckForReset();
  if (r.error == as5047u::Error::DeviceReset) {
    return sensor.EnsureInitialized().error;
  }
  if (sensor.GetLatchedErrorFlags().flags_lost) {
    std::printf("an ERRFL read failed: a fault may have been missed\n");
    sensor.ClearLatchedErrorFlags();
  }
  return r.error;
}
// --8<-- [end:reset]

// --8<-- [start:otp]
as5047u::Error BurnSettings(Sensor& sensor) {
  as5047u::OtpReport report{};
  // Irreversible. kOtpConfirm guards against a stray call.
  const auto r = sensor.ProgramOtp(as5047u::kOtpConfirm, report);
  if (!r) {
    std::printf("OTP failed at step %u (burn %s)\n", static_cast<unsigned>(report.failed_step),
                report.burn_started ? "started: do not use the part" : "not started");
  }
  return r.error;
}
// --8<-- [end:otp]

// --8<-- [start:errors]
const char* Describe(as5047u::Error e) {
  switch (e) {
  case as5047u::Error::Bus:
  case as5047u::Error::NoDevice:
  case as5047u::Error::DeviceIdMismatch:
    return "check the wiring, chip select, supply and SPI mode 1";
  case as5047u::Error::NotReady:
    return "the chip answers but its loops did not settle: supply, magnet, startup_timeout_ms";
  case as5047u::Error::MagnetWarning:
    return "magnet too far, too close or missing: check ReadAgc() and the air gap";
  case as5047u::Error::Crc:
  case as5047u::Error::Framing:
    return "signal integrity: lower the SPI clock, shorten the wires";
  case as5047u::Error::DeviceFault:
    return "chip fault or no magnet: check ReadErrorFlags() and ReadDiagnostics()";
  case as5047u::Error::DeviceReset:
    return "the chip was reset (supply dip?): call EnsureInitialized() again";
  default:
    return as5047u::ToString(e);
  }
}
// --8<-- [end:errors]
