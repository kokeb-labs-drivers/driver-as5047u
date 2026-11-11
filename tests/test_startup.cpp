/**
 * @file test_startup.cpp
 * @brief Start-up readiness and the line patterns of KB-CPP-116 on every path.
 *
 * `EnsureInitialized()` polls DIA until every magnetic loop reports finished, with a deadline from
 * `Config` (datasheet Figure 9, t_pon; Figure 35, DIA), and accepts a ready DIA only when the next
 * read confirms it. MISO stuck low, floating high, looped back to MOSI, or a chip that cannot hear
 * MOSI never pass start-up and never give a value on a CRC-protected read.
 */
#include "as5047u.hpp"
#include "mock_as5047u.hpp"
#include "test_harness.hpp"

using as5047u::Config;
using as5047u::Error;
using as5047u::FrameFormat;
using as5047u_test::LineMode;
using as5047u_test::MinimalPort;
namespace reg = as5047u::reg;

namespace {

Config WithFrame(FrameFormat f) {
  Config c{};
  c.frame = f;
  return c;
}

constexpr LineMode kBrokenLines[] = {LineMode::StuckLow, LineMode::FloatHigh, LineMode::Echo,
                                     LineMode::Deaf};
constexpr FrameFormat kFormats[] = {FrameFormat::Bits16, FrameFormat::Bits24, FrameFormat::Bits32};

} // namespace

TEST_CASE("A chip that is already up starts with no wait: DIA, its confirmation, ERRFL") {
  MinimalPort port;
  as5047u::AS5047U<MinimalPort> s(port);
  REQUIRE(s.EnsureInitialized().ok());
  CHECK_EQ(port.delayed_us, 0U);
  CHECK_EQ(port.state.chip.dia_reads, 2);
  CHECK_EQ(port.state.chip.errfl_reads, 1);
}

TEST_CASE("Start-up waits for DIA to report ready, polling once per millisecond") {
  MinimalPort port;
  port.state.chip.power_on_us = 4500; // the loops settle 4.5 ms after the first frame
  as5047u::AS5047U<MinimalPort> s(port);
  REQUIRE(s.EnsureInitialized().ok());
  CHECK_EQ(port.delayed_us, 5000U); // never returns before the chip is ready
  CHECK_EQ(port.state.chip.dia_reads, 5 + 2);
}

TEST_CASE("Start-up gives up at the Config deadline with NotReady, and never waits longer") {
  for (const uint32_t timeout : {0U, 1U, 10U, 37U}) {
    MinimalPort port;
    port.state.chip.never_ready = true;
    Config c{};
    c.startup_timeout_ms = timeout;
    as5047u::AS5047U<MinimalPort> s(port, c);
    CHECK_EQ(s.EnsureInitialized().error, Error::NotReady);
    CHECK_EQ(port.delayed_us, uint64_t{timeout} * 1000U);
    CHECK(!s.IsInitialized());
  }
}

TEST_CASE("A chip ready before the deadline is accepted, one ready just after it is not") {
  MinimalPort early;
  early.state.chip.power_on_us = 9000;
  as5047u::AS5047U<MinimalPort> a(early);
  CHECK(a.EnsureInitialized().ok());
  MinimalPort late;
  late.state.chip.power_on_us = 10'001;
  as5047u::AS5047U<MinimalPort> b(late);
  CHECK_EQ(b.EnsureInitialized().error, Error::NotReady);
}

TEST_CASE("A ready DIA counts only when the next read confirms it") {
  MinimalPort port;
  // Frames 0 and 1 read DIA (ready); frame 3 is the reply of the confirmation: break its CRC.
  port.state.chip.corrupt_reply_frame = 3;
  as5047u::AS5047U<MinimalPort> s(port);
  REQUIRE(s.EnsureInitialized().ok());
  CHECK_EQ(port.delayed_us, 1000U); // the failed confirmation cost one poll
  CHECK_EQ(port.state.chip.dia_reads, 4);
}

TEST_CASE("Validate bounds the start-up deadline") {
  Config c{};
  c.startup_timeout_ms = as5047u::kStartupTimeoutMaxMs;
  CHECK_EQ(as5047u::Validate(c), Error::Ok);
  c.startup_timeout_ms = as5047u::kStartupTimeoutMaxMs + 1U;
  CHECK_EQ(as5047u::Validate(c), Error::InvalidArgument);
  MinimalPort port;
  as5047u::AS5047U<MinimalPort> s(port, c);
  CHECK_EQ(s.EnsureInitialized().error, Error::InvalidArgument);
  CHECK_EQ(port.state.transfers, 0);
}

TEST_CASE("A DIA with a bit that reads 0 on an AS5047U is DeviceIdMismatch") {
  MinimalPort port;
  port.state.chip.dia = static_cast<uint16_t>(reg::kDiaReadyValue | (1U << 10U));
  as5047u::AS5047U<MinimalPort> s(port);
  CHECK_EQ(s.EnsureInitialized().error, Error::DeviceIdMismatch);
}

TEST_CASE("No broken line passes start-up, in any frame format") {
  for (const auto f : kFormats) {
    for (const auto line : kBrokenLines) {
      MinimalPort port;
      port.state.chip.line = line;
      as5047u::AS5047U<MinimalPort> s(port, WithFrame(f));
      const auto r = s.EnsureInitialized();
      CHECK(!r);
      CHECK(!s.IsInitialized());
      // Stuck, floating and deaf lines are NoDevice; an echo of a 32-bit frame shifts the pad
      // byte into the reply, so its CRC fails.
      const bool shifted_echo = line == LineMode::Echo && f == FrameFormat::Bits32;
      CHECK_EQ(r.error, shifted_echo ? Error::Crc : Error::NoDevice);
    }
  }
}

TEST_CASE("After start-up, no broken line gives a value on a CRC-protected read") {
  for (const auto f : {FrameFormat::Bits24, FrameFormat::Bits32}) {
    for (const auto line : kBrokenLines) {
      MinimalPort port;
      port.state.chip.raw_angle = 1234;
      as5047u::AS5047U<MinimalPort> s(port, WithFrame(f));
      REQUIRE(s.EnsureInitialized().ok());
      port.state.chip.line = line;
      CHECK(!s.ReadAngleCounts());
      CHECK(!s.ReadUncompensatedAngleCounts());
      CHECK(!s.ReadAngleDegrees());
      CHECK(!s.ReadVelocityCounts());
      CHECK(!s.ReadAgc());
      CHECK(!s.ReadMagnitude());
      CHECK(!s.ReadDiagnostics());
      CHECK(!s.ReadErrorFlags());
      CHECK(!s.ReadNonVolatile());
      CHECK(!s.ReadRegister(reg::kSettings2));
      // A write without read-back cannot tell a looped-back MOSI from the chip (the echo of the
      // data frame carries no flag); with Verify::ReadBack it can.
      CHECK(!s.WriteRegister(reg::kZposm, 1, as5047u::Verify::ReadBack));
    }
  }
}

TEST_CASE("With 16-bit frames, floating and looped-back lines still give no angle") {
  // MISO stuck low, or a chip that hears MOSI as all zeros, reads as angle 0 with no flag in a
  // 16-bit frame: without a CRC nothing tells it from a real 0 (docs/limitations.md). The other
  // line patterns are caught.
  for (const auto line : {LineMode::FloatHigh, LineMode::Echo}) {
    MinimalPort port;
    port.state.chip.raw_angle = 1234;
    as5047u::AS5047U<MinimalPort> s(port, WithFrame(FrameFormat::Bits16));
    REQUIRE(s.EnsureInitialized().ok());
    port.state.chip.line = line;
    const auto r = s.ReadAngleCounts();
    CHECK_EQ(r.error, Error::NoDevice);
  }
}
