/**
 * @file test_diagnostics.cpp
 * @brief The ERRFL latch (KB-CPP-119), the warning bit on measurements (KB-CPP-127), raw access
 * (KB-CPP-129) and the cost of the angle read.
 */
#include "as5047u.hpp"
#include "mock_as5047u.hpp"
#include "test_harness.hpp"

using as5047u::Error;
using as5047u_test::MinimalPort;
namespace reg = as5047u::reg;

namespace {

/// A started driver over a chip at `raw_angle` 1000.
struct Rig {
  MinimalPort port;
  as5047u::AS5047U<MinimalPort> s{port};
  Rig() {
    port.state.chip.raw_angle = 1000;
    REQUIRE(s.EnsureInitialized().ok());
  }
};

} // namespace

TEST_CASE("The angle read is exactly two frames when the reply carries no flag") {
  Rig rig;
  const int before = rig.port.state.transfers;
  const auto a = rig.s.ReadAngleCounts();
  REQUIRE(a.ok());
  CHECK_EQ(a.value, 1000);
  CHECK_EQ(rig.port.state.transfers - before, 2);
  CHECK_EQ(rig.s.GetLastReply(), 1000); // no flag bits
}

TEST_CASE("An angle under a magnet warning is refused with MagnetWarning and latched") {
  for (const uint16_t warning : {reg::kErrflAgcWarning, reg::kErrflMagHalf}) {
    Rig rig;
    rig.port.state.chip.errfl = warning;
    const int before = rig.port.state.transfers;
    const auto a = rig.s.ReadAngleCounts();
    CHECK_EQ(a.error, Error::MagnetWarning);
    CHECK_EQ(a.value, 0);
    CHECK_EQ(rig.port.state.transfers - before, 4);              // the read, then ERRFL
    CHECK((rig.s.GetLastReply() & reg::kReplyWarningBit) == 0U); // the ERRFL reply came last
    CHECK_EQ(rig.s.GetLatchedErrorFlags().raw, warning);
    CHECK(rig.s.ReadAngleCounts().ok()); // the warning was cleared and the field is fine again
  }
}

TEST_CASE("A field that stays out of range refuses every angle and velocity read") {
  Rig rig;
  rig.port.state.chip.errfl = reg::kErrflMagHalf;
  rig.port.state.chip.persistent_errfl = reg::kErrflMagHalf;
  for (int i = 0; i < 3; ++i) {
    CHECK_EQ(rig.s.ReadAngleCounts().error, Error::MagnetWarning);
    CHECK_EQ(rig.s.ReadUncompensatedAngleCounts().error, Error::MagnetWarning);
    CHECK_EQ(rig.s.ReadAngleRadians().error, Error::MagnetWarning);
    CHECK_EQ(rig.s.ReadVelocityRpm().error, Error::MagnetWarning);
  }
  CHECK(rig.s.GetLatchedErrorFlags().mag_half);
  // Diagnostic reads still answer under the warning: they describe the field.
  CHECK(rig.s.ReadAgc().ok());
  CHECK(rig.s.ReadMagnitude().ok());
  CHECK(rig.s.ReadDiagnostics().ok());
}

TEST_CASE("A P2RAM single-bit warning (corrected by the ECC) does not refuse the angle") {
  Rig rig;
  rig.port.state.chip.errfl = reg::kErrflP2ramWarning;
  const auto a = rig.s.ReadAngleCounts();
  REQUIRE(a.ok());
  CHECK_EQ(a.value, 1000);
  CHECK(rig.s.GetLatchedErrorFlags().p2ram_warning);
}

TEST_CASE("A flag consumed by an internal read stays in the latch until cleared") {
  Rig rig;
  rig.s.ClearLatchedErrorFlags();
  rig.port.state.chip.errfl = reg::kErrflCordic;
  CHECK_EQ(rig.s.ReadMagnitude().error, Error::DeviceFault); // the driver read ERRFL inside
  const auto now = rig.s.ReadErrorFlags();
  REQUIRE(now.ok());
  CHECK_EQ(now.value.raw, 0);                          // gone from the chip
  CHECK(rig.s.GetLatchedErrorFlags().cordic_overflow); // kept by the driver
  CHECK_EQ(rig.s.GetLastErrorFlags().raw, 0);
  rig.s.ClearLatchedErrorFlags();
  CHECK_EQ(rig.s.GetLatchedErrorFlags().raw, 0);
}

TEST_CASE("Every read of ERRFL feeds the latch: ReadErrorFlags, ReadRegister, writes") {
  Rig rig;
  rig.s.ClearLatchedErrorFlags();
  rig.port.state.chip.errfl = reg::kErrflAgcWarning;
  const auto raw = rig.s.ReadRegister(reg::kErrfl);
  REQUIRE(raw.ok());
  CHECK_EQ(raw.value, reg::kErrflAgcWarning);
  CHECK(rig.s.GetLatchedErrorFlags().agc_warning);
  rig.port.state.chip.errfl = reg::kErrflWatchdog;
  CHECK_EQ(rig.s.WriteRegister(reg::kZposm, 1).error, Error::DeviceFault);
  CHECK(rig.s.GetLatchedErrorFlags().watchdog);
  CHECK(rig.s.GetLatchedErrorFlags().agc_warning); // ORed, not overwritten
  rig.port.state.chip.errfl = reg::kErrflMagHalf;
  REQUIRE(rig.s.ReadErrorFlags().ok());
  CHECK_EQ(rig.s.GetLatchedErrorFlags().raw,
           uint32_t{reg::kErrflAgcWarning} | reg::kErrflWatchdog | reg::kErrflMagHalf);
}

TEST_CASE("Start-up empties the latch, then keeps what the power-up left in ERRFL") {
  MinimalPort port;
  port.state.chip.errfl = reg::kErrflOffComp | reg::kErrflFraming;
  as5047u::AS5047U<MinimalPort> s(port);
  REQUIRE(s.EnsureInitialized().ok()); // the start-up read of ERRFL cleared them on the chip
  CHECK_EQ(port.state.chip.errfl, 0);
  CHECK(s.GetLatchedErrorFlags().offset_comp);
  CHECK(s.GetLatchedErrorFlags().framing);
  REQUIRE(s.Deinitialize().ok());
  REQUIRE(s.EnsureInitialized().ok());
  CHECK_EQ(s.GetLatchedErrorFlags().raw, 0); // emptied by the new start
}

TEST_CASE("An error bit with no error in ERRFL is NoDevice, never a value") {
  Rig rig;
  // ERRFL holds only a warning, so a reply with the error bit cannot come from the chip.
  rig.port.state.chip.line = as5047u_test::LineMode::Echo;
  CHECK_EQ(rig.s.ReadAngleCounts().error, Error::NoDevice);
  CHECK_EQ(rig.s.ReadMagnitude().error, Error::NoDevice);
}

TEST_CASE("An ERRFL with its reserved bit set is NoDevice") {
  Rig rig;
  rig.port.state.chip.errfl = reg::kErrflReserved;
  CHECK_EQ(rig.s.ReadErrorFlags().error, Error::NoDevice);
}

TEST_CASE("GetLastReply keeps the raw reply word, flags included") {
  Rig rig;
  rig.port.state.chip.errfl = reg::kErrflP2ramWarning; // a warning alone: no ERRFL read
  REQUIRE(rig.s.ReadRegister(reg::kAgc).ok());
  // The last frame is the NOP of the AGC read: the warning bit, with the AGC value.
  CHECK_EQ(rig.s.GetLastReply(), reg::kReplyWarningBit | 128U);
  rig.port.state.chip.errfl = reg::kErrflCordic;
  CHECK_EQ(rig.s.ReadRegister(reg::kAgc).error, Error::DeviceFault);
  // Now the last frame is the NOP of the ERRFL read: the ERRFL value; the model computes the
  // flags after the read cleared ERRFL, so none is set.
  CHECK_EQ(rig.s.GetLastReply(), reg::kErrflCordic);
  CHECK_EQ(rig.s.GetLastErrorFlags().raw, reg::kErrflCordic);
}

TEST_CASE("Config compares field by field") {
  as5047u::Config a{};
  as5047u::Config b{};
  CHECK(a == b);
  b.startup_timeout_ms = 3;
  CHECK(!(a == b));
}

TEST_CASE("A chip that flags warnings with the error bit gives the same results") {
  // The datasheet does not say which ERRFL flags drive which reply bit: the driver judges by ERRFL.
  MinimalPort port;
  port.state.chip.warnings_set_error_bit = true;
  port.state.chip.errfl = reg::kErrflMagHalf; // left by the power-up
  as5047u::AS5047U<MinimalPort> s(port);
  REQUIRE(s.EnsureInitialized().ok());
  CHECK(s.GetLatchedErrorFlags().mag_half);
  port.state.chip.errfl = reg::kErrflAgcWarning;
  CHECK_EQ(s.ReadAngleCounts().error, Error::MagnetWarning);
  port.state.chip.errfl = reg::kErrflAgcWarning;
  CHECK(s.ReadAgc().ok());
  port.state.chip.errfl = reg::kErrflP2ramWarning;
  CHECK(s.ReadAngleCounts().ok());
  CHECK(s.GetLatchedErrorFlags().p2ram_warning);
  port.state.chip.errfl = reg::kErrflAgcWarning; // a write whose reply carries only a warning
  CHECK(s.WriteRegister(reg::kSettings3, 1).ok());
  CHECK(s.CheckForReset().ok()); // the chip holds what was written
}

TEST_CASE("A failed ApplyConfig leaves GetConfig() as it was, frame format included") {
  MinimalPort port;
  as5047u::AS5047U<MinimalPort> s(port);
  REQUIRE(s.EnsureInitialized().ok());
  as5047u::Config c{};
  c.frame = as5047u::FrameFormat::Bits32;
  c.zero_position = 5;
  port.state.fail_transfer = port.state.transfers + 3;
  CHECK_EQ(s.ApplyConfig(c).error, Error::Bus);
  CHECK(s.GetConfig() == as5047u::Config{});
}
