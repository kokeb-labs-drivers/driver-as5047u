/**
 * @file test_as5047u.cpp
 * @brief Behaviour of the AS5047U driver against the frame-level chip model.
 */
#include <cstring>

#include "as5047u.hpp"
#include "mock_as5047u.hpp"
#include "test_harness.hpp"

using as5047u::Config;
using as5047u::Error;
using as5047u::FrameFormat;
using as5047u::Verify;
using as5047u_test::FullPort;
using as5047u_test::MinimalPort;
namespace reg = as5047u::reg;

namespace {

constexpr bool Near(float a, float b, float tolerance) {
  return (a - b) < tolerance && (b - a) < tolerance;
}

Config WithFrame(FrameFormat f) {
  Config c{};
  c.frame = f;
  return c;
}

} // namespace

// --- Pure functions ----------------------------------------------------------------------------

static_assert(as5047u::Crc8(0x0000) == 0xF1, "CRC-8 of a zero word");
static_assert(as5047u::Crc8(0xFFFF) == 0x74, "CRC-8 of an all-ones word");
static_assert(as5047u::detail::SignExtend14(0x1FFF) == 8191, "largest positive velocity");
static_assert(as5047u::detail::SignExtend14(0x2000) == -8192, "most negative velocity");
static_assert(as5047u::detail::SignExtend14(0x3FFF) == -1, "minus one");
static_assert(as5047u::Validate(Config{}) == Error::Ok, "the defaults are valid");

TEST_CASE("Validate rejects every out-of-range field") {
  const auto bad = [](auto mutate) {
    Config c{};
    mutate(c);
    return as5047u::Validate(c) == Error::InvalidArgument;
  };
  // Values that are not enumerators, on purpose: Validate() must reject them.
  // NOLINTBEGIN(clang-analyzer-optin.core.EnumCastOutOfRange)
  CHECK(bad([](Config& c) { c.frame = static_cast<FrameFormat>(3); }));
  CHECK(bad([](Config& c) { c.direction = static_cast<as5047u::Direction>(2); }));
  CHECK(bad([](Config& c) { c.incremental = static_cast<as5047u::IncrementalOutput>(2); }));
  CHECK(bad([](Config& c) { c.index_width = static_cast<as5047u::IndexWidth>(2); }));
  // NOLINTEND(clang-analyzer-optin.core.EnumCastOutOfRange)
  CHECK(bad([](Config& c) { c.zero_position = 16384; }));
  CHECK(bad([](Config& c) { c.filter_k_min_code = 8; }));
  CHECK(bad([](Config& c) { c.filter_k_max_code = 8; }));
  CHECK(bad([](Config& c) { c.abi_resolution_code = 8; }));
  CHECK(bad([](Config& c) { c.hysteresis_code = 4; }));
  CHECK(bad([](Config& c) { c.uvw_pole_pairs = 0; }));
  CHECK(bad([](Config& c) { c.uvw_pole_pairs = 8; }));
}

TEST_CASE("Unit conversions") {
  CHECK(Near(as5047u::CountsToDegrees(0), 0.0F, 1e-6F));
  CHECK(Near(as5047u::CountsToDegrees(4096), 90.0F, 1e-3F));
  CHECK(Near(as5047u::CountsToDegrees(16383), 359.978F, 1e-2F));
  CHECK(Near(as5047u::CountsToRadians(8192), as5047u::kPi, 1e-4F));
  CHECK(std::strcmp(as5047u::ToString(Error::OtpVerify), "OtpVerify") == 0);
  CHECK(std::strcmp(as5047u::ToString(static_cast<Error>(200)), "Unknown") == 0);
}

// --- Lifecycle ---------------------------------------------------------------------------------

TEST_CASE("Every I/O call fails with NotInitialized before EnsureInitialized, and does no I/O") {
  MinimalPort port;
  as5047u::AS5047U<MinimalPort> s(port);
  as5047u::OtpReport report{};
  CHECK_EQ(s.ReadAngleCounts().error, Error::NotInitialized);
  CHECK_EQ(s.ReadErrorFlags().error, Error::NotInitialized);
  CHECK_EQ(s.ReadNonVolatile().error, Error::NotInitialized);
  CHECK_EQ(s.WriteRegister(reg::kZposm, 1).error, Error::NotInitialized);
  CHECK_EQ(s.ProgramOtp(as5047u::kOtpConfirm, report).error, Error::NotInitialized);
  CHECK_EQ(port.state.transfers, 0);
}

TEST_CASE("EnsureInitialized writes the configuration and reads every register back") {
  FullPort port;
  Config c{};
  c.direction = as5047u::Direction::CounterClockwise;
  c.zero_position = 0x1234;
  c.dynamic_compensation = false;
  c.adaptive_filter = false;
  c.filter_k_min_code = 5;
  c.filter_k_max_code = 2;
  c.incremental = as5047u::IncrementalOutput::Uvw;
  c.abi_enable = false;
  c.uvw_enable = false;
  c.pwm_enable = true;
  c.abi_resolution_code = 4;
  c.abi_decimal = true;
  c.index_width = as5047u::IndexWidth::OneLsb;
  c.uvw_pole_pairs = 7;
  c.hysteresis_code = 3;
  c.high_temperature_noise = true;
  port.state.chip.nv[2] = 0xC0; // Dia1_en and Dia2_en are kept
  as5047u::AS5047U<FullPort> s(port, c);
  REQUIRE(s.EnsureInitialized().ok());
  CHECK(s.IsInitialized());
  const auto& nv = port.state.chip.nv;
  CHECK_EQ(nv[0], uint32_t{reg::kDisableUvwOff} | reg::kDisableAbiOff | reg::kDisableFilterOff);
  CHECK_EQ(nv[1], 0x1234U >> 6U);
  CHECK_EQ(nv[2], 0xC0U | (0x1234U & 0x3FU));
  CHECK_EQ(nv[3], (5U << 3U) | 2U);
  CHECK_EQ(nv[4], 0xFFU & ~uint32_t{reg::kS2DataSelect});
  CHECK_EQ(nv[5], 6U | (3U << 3U) | (4U << 5U));
  CHECK_EQ(s.GetConfig().zero_position, 0x1234);
  CHECK(port.log_lines > 0);
  CHECK_EQ(port.error_lines, 0);
}

TEST_CASE("Config{} on a fresh part writes the reset values") {
  MinimalPort port;
  as5047u::AS5047U<MinimalPort> s(port);
  REQUIRE(s.EnsureInitialized().ok());
  for (const uint16_t v : port.state.chip.nv) {
    CHECK_EQ(v, 0);
  }
}

TEST_CASE("write_settings = false leaves an OTP-burned part alone") {
  MinimalPort port;
  port.state.chip.nv[4] = reg::kS2Dir;
  Config c{};
  c.write_settings = false;
  as5047u::AS5047U<MinimalPort> s(port, c);
  REQUIRE(s.EnsureInitialized().ok());
  CHECK_EQ(port.state.chip.nv[4], reg::kS2Dir);
  const auto nv = s.ReadNonVolatile();
  REQUIRE(nv.ok());
  CHECK_EQ(nv.value[4], reg::kS2Dir);
}

TEST_CASE("EnsureInitialized rejects a bad stored config before any I/O, and reports the bus") {
  MinimalPort port;
  Config c{};
  c.uvw_pole_pairs = 0;
  as5047u::AS5047U<MinimalPort> s(port, c);
  CHECK_EQ(s.EnsureInitialized().error, Error::InvalidArgument);
  CHECK_EQ(port.state.transfers, 0);
  CHECK_EQ(s.ApplyConfig(c).error, Error::InvalidArgument); // not stored either
  REQUIRE(s.ApplyConfig({}).ok());                          // stored, no I/O
  CHECK_EQ(port.state.transfers, 0);
  port.state.bus_up_ok = false;
  CHECK_EQ(s.EnsureInitialized().error, Error::Bus);
  CHECK(!s.IsInitialized());
}

TEST_CASE("EnsureInitialized is the one entry point: once, idempotent, retried after a failure") {
  MinimalPort port;
  port.state.chip.line = as5047u_test::LineMode::FloatHigh;
  as5047u::AS5047U<MinimalPort> s(port);
  CHECK_EQ(s.EnsureInitialized().error, Error::NoDevice);
  CHECK(!s.IsInitialized());
  port.state.chip.line =
      as5047u_test::LineMode::Normal; // the chip comes up: the next call starts from scratch
  REQUIRE(s.EnsureInitialized().ok());
  const int transfers = port.state.transfers;
  for (int i = 0; i < 3; ++i) {
    CHECK(s.EnsureInitialized().ok());
  }
  CHECK_EQ(port.state.transfers, transfers); // no I/O once started
  CHECK(s.Deinitialize().ok());
  CHECK_EQ(s.ReadAngleCounts().error, Error::NotInitialized); // reads never start the chip
  CHECK_EQ(port.state.transfers, transfers);
  REQUIRE(s.EnsureInitialized().ok()); // restart after Deinitialize
  CHECK(port.state.transfers > transfers);
}

TEST_CASE("ApplyConfig before the start stores the config for EnsureInitialized") {
  MinimalPort port;
  as5047u::AS5047U<MinimalPort> s(port);
  Config c{};
  c.zero_position = 321;
  REQUIRE(s.ApplyConfig(c).ok());
  CHECK_EQ(port.state.transfers, 0);
  CHECK_EQ(s.GetConfig().zero_position, 321);
  REQUIRE(s.EnsureInitialized().ok());
  CHECK_EQ(port.state.chip.nv[1], 321 >> 6);
  CHECK_EQ(port.state.chip.nv[2], 321 & 0x3F);
}

TEST_CASE("A missing chip is NoDevice in every frame format, after the start-up deadline") {
  for (const auto f : {FrameFormat::Bits16, FrameFormat::Bits24, FrameFormat::Bits32}) {
    MinimalPort port;
    port.state.chip.line = as5047u_test::LineMode::FloatHigh;
    as5047u::AS5047U<MinimalPort> s(port, WithFrame(f));
    CHECK_EQ(s.EnsureInitialized().error, Error::NoDevice);
    CHECK_EQ(port.delayed_us, as5047u::kPowerOnTimeMs * 1000U); // waited the deadline, no longer
  }
  MinimalPort low;
  low.state.chip.dia = 0; // a DIA that never reads anything but 0
  as5047u::AS5047U<MinimalPort> s(low, WithFrame(FrameFormat::Bits16));
  CHECK_EQ(s.EnsureInitialized().error, Error::NoDevice);
}

TEST_CASE("EnsureInitialized and Deinitialize") {
  MinimalPort port;
  as5047u::AS5047U<MinimalPort> s(port);
  REQUIRE(s.EnsureInitialized().ok());
  const int transfers = port.state.transfers;
  CHECK(s.EnsureInitialized().ok());
  CHECK_EQ(port.state.transfers, transfers);
  CHECK(s.Deinitialize().ok());
  CHECK(!s.IsInitialized());
  CHECK_EQ(s.ReadAngleCounts().error, Error::NotInitialized);
}

// --- Frames --------------------------------------------------------------------------------------

TEST_CASE("Reads work in every frame format, writes are always CRC-protected") {
  for (const auto f : {FrameFormat::Bits16, FrameFormat::Bits24, FrameFormat::Bits32}) {
    MinimalPort port;
    port.state.chip.raw_angle = 1000;
    Config c = WithFrame(f);
    c.pad_byte = 0x5A;
    as5047u::AS5047U<MinimalPort> s(port, c);
    REQUIRE(s.EnsureInitialized().ok());
    const auto a = s.ReadAngleCounts();
    REQUIRE(a.ok());
    CHECK_EQ(a.value, 1000);
    CHECK_EQ(port.state.last_len,
             f == FrameFormat::Bits16 ? 2U : (f == FrameFormat::Bits24 ? 3U : 4U));
    REQUIRE(s.WriteRegister(reg::kZposm, 0x12).ok());
    CHECK_EQ(port.state.chip.nv[1], 0x12);
    CHECK_EQ(port.state.chip.crc_errors_seen, 0);
  }
}

TEST_CASE("A corrupted reply CRC fails the read with Crc") {
  MinimalPort port;
  as5047u::AS5047U<MinimalPort> s(port);
  REQUIRE(s.EnsureInitialized().ok());
  port.state.chip.corrupt_reply_frame = port.state.chip.frames + 1; // the data frame
  CHECK_EQ(s.ReadAngleCounts().error, Error::Crc);
  CHECK(s.ReadAngleCounts().ok());
}

TEST_CASE("The reply error bit is resolved through ERRFL") {
  struct Case {
    uint16_t bits;
    Error expected;
  };
  for (const Case k :
       {Case{reg::kErrflCrc, Error::Crc}, Case{reg::kErrflFraming, Error::Framing},
        Case{reg::kErrflCommand, Error::Command}, Case{reg::kErrflCordic, Error::DeviceFault},
        Case{reg::kErrflWatchdog, Error::DeviceFault}}) {
    MinimalPort port;
    as5047u::AS5047U<MinimalPort> s(port);
    REQUIRE(s.EnsureInitialized().ok());
    port.state.chip.errfl = k.bits;
    CHECK_EQ(s.ReadMagnitude().error, k.expected);
    CHECK_EQ(s.GetLastErrorFlags().raw, k.bits);
    CHECK(s.ReadMagnitude().ok()); // ERRFL was cleared by the driver's read of it
  }
}

TEST_CASE("Warnings never fail a read but are reported by ReadErrorFlags") {
  MinimalPort port;
  as5047u::AS5047U<MinimalPort> s(port);
  REQUIRE(s.EnsureInitialized().ok());
  port.state.chip.errfl = reg::kErrflAgcWarning | reg::kErrflMagHalf;
  CHECK(s.ReadAgc().ok());
  const auto f = s.ReadErrorFlags();
  REQUIRE(f.ok());
  CHECK(f.value.agc_warning);
  CHECK(f.value.mag_half);
  CHECK(!f.value.crc);
  CHECK_EQ(port.state.chip.errfl, 0);
  const auto again = s.ReadErrorFlags();
  REQUIRE(again.ok());
  CHECK_EQ(again.value.raw, 0);
}

TEST_CASE("ERRFL with the error bit but only warnings does not fail the access") {
  CHECK_EQ(as5047u::detail::ClassifyErrorFlags(reg::kErrflP2ramWarning), Error::Ok);
  CHECK_EQ(as5047u::detail::ClassifyErrorFlags(reg::kErrflP2ramError), Error::DeviceFault);
  CHECK_EQ(as5047u::detail::ClassifyErrorFlags(reg::kErrflOffComp), Error::DeviceFault);
}

TEST_CASE("A read of an unknown address is a Command error") {
  MinimalPort port;
  as5047u::AS5047U<MinimalPort> s(port);
  REQUIRE(s.EnsureInitialized().ok());
  CHECK_EQ(s.ReadRegister(0x0100).error, Error::Command);
  CHECK_EQ(s.ReadRegister(0x4000).error, Error::InvalidArgument);
}

TEST_CASE("WriteRegister refuses PROG and out-of-range values, and verifies when asked to") {
  MinimalPort port;
  as5047u::AS5047U<MinimalPort> s(port);
  REQUIRE(s.EnsureInitialized().ok());
  CHECK_EQ(s.WriteRegister(reg::kProg, reg::kProgBurn).error, Error::InvalidArgument);
  CHECK_EQ(s.WriteRegister(0x4000, 0).error, Error::InvalidArgument);
  CHECK_EQ(s.WriteRegister(reg::kZposm, 0x4000).error, Error::InvalidArgument);
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): not an enumerator, on purpose
  const auto bad = static_cast<as5047u::Verify>(2);
  const int before = port.state.transfers;
  CHECK_EQ(s.WriteRegister(reg::kZposm, 1, bad).error, Error::InvalidArgument);
  CHECK_EQ(s.ApplyConfig({}, bad).error, Error::InvalidArgument);
  CHECK_EQ(port.state.transfers, before); // refused before any I/O
  REQUIRE(s.WriteRegister(reg::kZposm, 0x12, Verify::ReadBack).ok());
  CHECK_EQ(port.state.transfers - before, 4); // command, data, read-back command, NOP
  port.state.chip.ignore_writes = true;
  CHECK_EQ(s.WriteRegister(reg::kZposm, 0x13, Verify::ReadBack).error, Error::WriteMismatch);
}

TEST_CASE("By default a run-time write is the two frames the datasheet needs, with no read-back") {
  MinimalPort port;
  as5047u::AS5047U<MinimalPort> s(port);
  REQUIRE(s.EnsureInitialized().ok());
  int before = port.state.transfers;
  REQUIRE(s.WriteRegister(reg::kZposm, 0x12).ok());
  CHECK_EQ(port.state.transfers - before, 2); // command and data only
  CHECK_EQ(port.state.chip.nv[1], 0x12);
  // ApplyConfig: the read of DISABLE to ECC (7 reads, 14 frames), then 6 writes of 2 frames.
  Config c{};
  c.zero_position = 0x40;
  before = port.state.transfers;
  REQUIRE(s.ApplyConfig(c).ok());
  CHECK_EQ(port.state.transfers - before, 14 + 12);
  before = port.state.transfers;
  REQUIRE(s.SetDirection(as5047u::Direction::CounterClockwise).ok());
  CHECK_EQ(port.state.transfers - before, 14 + 12);
  // A lost write goes unnoticed by default; with Verify::ReadBack the same kind of write fails.
  port.state.chip.ignore_writes = true;
  REQUIRE(s.WriteRegister(reg::kZposm, 0x21).ok());
  REQUIRE(s.SetZeroPosition(0x80).ok()); // ZPOSM 1 -> 2, lost
  before = port.state.transfers;
  CHECK_EQ(s.SetZeroPosition(0xC0, Verify::ReadBack).error, Error::WriteMismatch);
  // DISABLE, then ZPOSM, each read back; then the settings read back after the failed write.
  CHECK_EQ(port.state.transfers - before, 14 + 4 + 4 + 14);
  CHECK_EQ(s.GetConfig().zero_position, 0x40); // what the chip holds, not the lost 0x80 or 0xC0
  CHECK_EQ(s.SetDirection(as5047u::Direction::Clockwise, Verify::ReadBack).error,
           Error::WriteMismatch);
  CHECK_EQ(s.SetZeroHere(Verify::ReadBack).error, Error::WriteMismatch);
}

// --- Measurement ---------------------------------------------------------------------------------

TEST_CASE("Angle, velocity, AGC, magnitude and diagnostics") {
  MinimalPort port;
  auto& chip = port.state.chip;
  chip.raw_angle = 4096;
  chip.daec_offset = 10;
  chip.velocity = 0x3FFE; // -2 counts
  chip.agc = 77;
  chip.magnitude = 3210;
  chip.dia |= reg::kDiaVdd5V | (2U << reg::kDiaSpiCountShift);
  as5047u::AS5047U<MinimalPort> s(port);
  REQUIRE(s.EnsureInitialized().ok());
  CHECK_EQ(s.ReadAngleCounts().value, 4106);
  CHECK_EQ(s.ReadUncompensatedAngleCounts().value, 4096);
  CHECK(Near(s.ReadAngleDegrees().value, 4106.0F * 360.0F / 16384.0F, 1e-3F));
  CHECK(Near(s.ReadAngleRadians().value, 4106.0F * 2.0F * as5047u::kPi / 16384.0F, 1e-4F));
  CHECK_EQ(s.ReadVelocityCounts().value, -2);
  CHECK(Near(s.ReadVelocityDegPerSec().value, -48.282F, 1e-3F));
  CHECK(Near(s.ReadVelocityRpm().value, -8.047F, 1e-3F));
  CHECK_EQ(s.ReadAgc().value, 77);
  CHECK_EQ(s.ReadMagnitude().value, 3210);
  const auto d = s.ReadDiagnostics();
  REQUIRE(d.ok());
  CHECK(d.value.vdd_5v);
  CHECK(d.value.loops_finished);
  CHECK(d.value.agc_done);
  CHECK(d.value.offset_comp_done);
  CHECK(!d.value.cordic_overflow);
  CHECK_EQ(d.value.spi_frame_count, 2);
}

TEST_CASE("Failed reads return T{} and the error") {
  MinimalPort port;
  as5047u::AS5047U<MinimalPort> s(port);
  REQUIRE(s.EnsureInitialized().ok());
  port.state.chip.errfl = reg::kErrflCordic;
  const auto deg = s.ReadAngleDegrees();
  CHECK_EQ(deg.error, Error::DeviceFault);
  CHECK(Near(deg.value, 0.0F, 1e-9F));
  port.state.chip.errfl = reg::kErrflCordic;
  CHECK_EQ(s.ReadAngleRadians().error, Error::DeviceFault);
  port.state.chip.errfl = reg::kErrflCordic;
  CHECK_EQ(s.ReadVelocityRpm().error, Error::DeviceFault);
  port.state.chip.errfl = reg::kErrflCordic;
  CHECK_EQ(s.ReadAgc().error, Error::DeviceFault);
  port.state.chip.errfl = reg::kErrflCordic;
  CHECK_EQ(s.ReadDiagnostics().error, Error::DeviceFault);
}

TEST_CASE("Direction and DAEC change what the angle registers read") {
  MinimalPort port;
  port.state.chip.raw_angle = 1000;
  port.state.chip.daec_offset = 5;
  as5047u::AS5047U<MinimalPort> s(port);
  REQUIRE(s.EnsureInitialized().ok());
  REQUIRE(s.SetDirection(as5047u::Direction::CounterClockwise).ok());
  CHECK_EQ(s.ReadUncompensatedAngleCounts().value, 16384 - 1000);
  Config c = s.GetConfig();
  c.dynamic_compensation = false;
  REQUIRE(s.ApplyConfig(c).ok());
  CHECK_EQ(s.ReadAngleCounts().value, 16384 - 1000);
  c.uvw_pole_pairs = 9;
  CHECK_EQ(s.ApplyConfig(c).error, Error::InvalidArgument);
}

TEST_CASE("SetZeroHere makes the current position read 0, in either direction") {
  for (const auto dir : {as5047u::Direction::Clockwise, as5047u::Direction::CounterClockwise}) {
    MinimalPort port;
    port.state.chip.raw_angle = 5000;
    Config c{};
    c.direction = dir;
    c.zero_position = 777; // an old zero must not leak into the new one
    as5047u::AS5047U<MinimalPort> s(port, c);
    REQUIRE(s.EnsureInitialized().ok());
    const auto z = s.SetZeroHere();
    REQUIRE(z.ok());
    CHECK_EQ(s.ReadAngleCounts().value, 0);
    CHECK_EQ(s.GetConfig().zero_position, z.value);
    port.state.chip.raw_angle = dir == as5047u::Direction::Clockwise ? 5100 : 4900;
    CHECK_EQ(s.ReadAngleCounts().value, 100);
  }
}

TEST_CASE("SetZeroHere reports a failed angle read and puts the old zero back") {
  MinimalPort port;
  Config c{};
  c.zero_position = 777;
  as5047u::AS5047U<MinimalPort> s(port, c);
  REQUIRE(s.EnsureInitialized().ok());
  port.state.chip.fault_on_read_of = reg::kAngleCom;
  port.state.chip.fault_bits = reg::kErrflCordic;
  CHECK_EQ(s.SetZeroHere().error, Error::DeviceFault);
  CHECK_EQ(s.GetConfig().zero_position, 777);
  port.state.chip.fault_on_read_of = -1;
  port.state.chip.raw_angle = 877;
  CHECK_EQ(s.ReadAngleCounts().value, 100); // the chip holds the old zero again
}

TEST_CASE("SetZeroHere before EnsureInitialized changes nothing") {
  MinimalPort port;
  Config c{};
  c.zero_position = 777;
  as5047u::AS5047U<MinimalPort> s(port, c);
  CHECK_EQ(s.SetZeroHere().error, Error::NotInitialized);
  CHECK_EQ(s.GetConfig().zero_position, 777);
  CHECK_EQ(port.state.transfers, 0);
}

// --- OTP -----------------------------------------------------------------------------------------

TEST_CASE("ProgramOtp burns the settings and verifies them") {
  FullPort port;
  Config c{};
  c.zero_position = 0x0ABC;
  c.direction = as5047u::Direction::CounterClockwise;
  as5047u::AS5047U<FullPort> s(port, c);
  REQUIRE(s.EnsureInitialized().ok());
  as5047u::OtpReport report{};
  const auto r = s.ProgramOtp(as5047u::kOtpConfirm, report);
  CHECK_EQ(r.error, Error::Ok);
  CHECK_EQ(report.failed_step, 0);
  CHECK(report.burn_started);
  CHECK(port.state.chip.otp_burned);
  CHECK_EQ(port.state.chip.otp[1], 0x0ABC >> 6);
  CHECK((port.state.chip.otp[6] & reg::kEccEnable) != 0U);
  CHECK(report.written == port.state.chip.otp);
  CHECK(port.delayed_us >= 5000U);
}

TEST_CASE("ProgramOtp needs the confirmation value") {
  MinimalPort port;
  as5047u::AS5047U<MinimalPort> s(port);
  REQUIRE(s.EnsureInitialized().ok());
  as5047u::OtpReport report{};
  CHECK_EQ(s.ProgramOtp(as5047u::OtpBurnConfirm{0}, report).error, Error::InvalidArgument);
  CHECK(!report.burn_started);
  CHECK(!port.state.chip.otp_burned);
}

TEST_CASE("ProgramOtp stops before the burn when a register does not hold its value") {
  MinimalPort port;
  as5047u::AS5047U<MinimalPort> s(port);
  REQUIRE(s.EnsureInitialized().ok());
  port.state.chip.ignore_writes = true;
  port.state.chip.ignore_write_address = reg::kEcc;
  as5047u::OtpReport report{};
  CHECK_EQ(s.ProgramOtp(as5047u::kOtpConfirm, report).error, Error::WriteMismatch);
  CHECK_EQ(report.failed_step, 7);
  CHECK(!report.burn_started);
}

TEST_CASE("ProgramOtp times out when the burn never finishes") {
  MinimalPort port;
  as5047u::AS5047U<MinimalPort> s(port);
  REQUIRE(s.EnsureInitialized().ok());
  port.state.chip.burn_never_finishes = true;
  as5047u::OtpReport report{};
  CHECK_EQ(s.ProgramOtp(as5047u::kOtpConfirm, report).error, Error::Timeout);
  CHECK_EQ(report.failed_step, 14);
  CHECK(report.burn_started);
  CHECK_EQ(port.delayed_us, as5047u::kOtpBurnTimeoutMs * 1000U);
}

TEST_CASE("ProgramOtp reports a failed guard-band test as OtpVerify") {
  MinimalPort port;
  as5047u::AS5047U<MinimalPort> s(port);
  REQUIRE(s.EnsureInitialized().ok());
  port.state.chip.guard_band_fails = true;
  as5047u::OtpReport report{};
  CHECK_EQ(s.ProgramOtp(as5047u::kOtpConfirm, report).error, Error::OtpVerify);
  CHECK_EQ(report.failed_step, 19);
}

TEST_CASE("ProgramOtp reports a device fault while polling the burn") {
  MinimalPort port;
  as5047u::AS5047U<MinimalPort> s(port);
  REQUIRE(s.EnsureInitialized().ok());
  port.state.chip.fault_on_read_of = reg::kProg;
  port.state.chip.fault_bits = reg::kErrflWatchdog;
  as5047u::OtpReport report{};
  CHECK_EQ(s.ProgramOtp(as5047u::kOtpConfirm, report).error, Error::DeviceFault);
  CHECK(report.failed_step >= 12);
}

TEST_CASE("Failures are logged through the optional Log hook") {
  FullPort port;
  Config bad{};
  bad.hysteresis_code = 9;
  as5047u::AS5047U<FullPort> s(port, bad);
  CHECK_EQ(s.EnsureInitialized().error, Error::InvalidArgument);
  REQUIRE(s.ApplyConfig({}).ok());
  port.state.bus_up_ok = false;
  CHECK_EQ(s.EnsureInitialized().error, Error::Bus);
  port.state.bus_up_ok = true;
  port.state.chip.line = as5047u_test::LineMode::FloatHigh;
  CHECK_EQ(s.EnsureInitialized().error, Error::NoDevice);
  port.state.chip.line = as5047u_test::LineMode::Normal;
  port.state.chip.ignore_writes = true;
  Config c{};
  c.zero_position = 5;
  REQUIRE(s.ApplyConfig(c).ok());
  CHECK_EQ(s.EnsureInitialized().error, Error::WriteMismatch);
  CHECK_EQ(port.error_lines, 4);
  port.state.chip.ignore_writes = false;
  REQUIRE(s.EnsureInitialized().ok());
  as5047u::OtpReport report{};
  port.state.chip.guard_band_fails = true;
  CHECK_EQ(s.ProgramOtp(as5047u::kOtpConfirm, report).error, Error::OtpVerify);
  CHECK_EQ(port.error_lines, 5);
}

TEST_CASE("write_settings = false reads the chip's settings, so a setter changes only its field") {
  MinimalPort port;
  auto& nv = port.state.chip.nv;
  nv[0] = reg::kDisableAbiOff;
  nv[1] = 0x12;
  nv[2] = 0xC0 | 0x05;
  nv[3] = (3U << reg::kS1KminShift) | 4U;
  nv[4] = static_cast<uint16_t>(uint32_t{reg::kS2Dir} | reg::kS2PwmOn | reg::kS2DataSelect |
                                reg::kS2UvwAbi);
  nv[5] = 7U | (2U << reg::kS3HysShift) | (5U << reg::kS3AbiresShift); // UVWPP 7 = 7 pole pairs
  Config c{};
  c.write_settings = false;
  as5047u::AS5047U<MinimalPort> s(port, c);
  REQUIRE(s.EnsureInitialized().ok());
  const Config& got = s.GetConfig();
  CHECK(!got.abi_enable);
  CHECK(got.uvw_enable);
  CHECK_EQ(got.zero_position, (0x12U << 6U) | 0x05U);
  CHECK_EQ(got.filter_k_min_code, 3);
  CHECK_EQ(got.filter_k_max_code, 4);
  CHECK_EQ(got.direction, as5047u::Direction::CounterClockwise);
  CHECK(got.pwm_enable);
  CHECK(got.uncompensated_at_angle_register);
  CHECK_EQ(got.incremental, as5047u::IncrementalOutput::Uvw);
  CHECK_EQ(got.uvw_pole_pairs, 7);
  CHECK_EQ(got.hysteresis_code, 2);
  CHECK_EQ(got.abi_resolution_code, 5);
  REQUIRE(s.SetZeroPosition(100).ok());
  CHECK_EQ(nv[0], reg::kDisableAbiOff);
  CHECK_EQ(nv[3], (3U << reg::kS1KminShift) | 4U);
  CHECK_EQ(nv[4], uint32_t{reg::kS2Dir} | reg::kS2PwmOn | reg::kS2DataSelect | reg::kS2UvwAbi);
  CHECK_EQ(nv[5], 6U | (2U << reg::kS3HysShift) | (5U << reg::kS3AbiresShift));
  CHECK_EQ(nv[2] & reg::kZposlDiaBits, 0xC0);
}

TEST_CASE("write_settings = false reports a failed read of the settings") {
  MinimalPort port;
  Config c{};
  c.write_settings = false;
  port.state.chip.fault_on_read_of = reg::kSettings2;
  port.state.chip.fault_bits = reg::kErrflCordic;
  as5047u::AS5047U<MinimalPort> s(port, c);
  CHECK_EQ(s.EnsureInitialized().error, Error::DeviceFault);
}

TEST_CASE("ProgramOtp uses CRC frames even when reads are 16-bit") {
  MinimalPort port;
  as5047u::AS5047U<MinimalPort> s(port, WithFrame(FrameFormat::Bits16));
  REQUIRE(s.EnsureInitialized().ok());
  const int before = port.state.chip.frames;
  port.state.shortest_len = 0;
  as5047u::OtpReport report{};
  REQUIRE(s.ProgramOtp(as5047u::kOtpConfirm, report).ok());
  CHECK(port.state.chip.frames > before);
  CHECK_EQ(port.state.shortest_len, 3U);
  CHECK_EQ(s.GetConfig().frame, FrameFormat::Bits16); // restored afterwards
  CHECK_EQ(s.ReadAngleCounts().error, Error::Ok);
  CHECK_EQ(port.state.last_len, 2U);
}

TEST_CASE("Data_select makes the angle register read the uncompensated angle") {
  MinimalPort port;
  port.state.chip.raw_angle = 2000;
  port.state.chip.daec_offset = 7;
  as5047u::AS5047U<MinimalPort> s(port);
  REQUIRE(s.EnsureInitialized().ok());
  CHECK_EQ(s.ReadAngleCounts().value, 2007);
  Config c = s.GetConfig();
  c.uncompensated_at_angle_register = true;
  REQUIRE(s.ApplyConfig(c).ok());
  CHECK_EQ(s.ReadAngleCounts().value, 2000);
}
