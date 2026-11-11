/**
 * @file test_bus_faults.cpp
 * @brief Every public operation reports a failed bus transfer as Error::Bus, whichever transfer
 * fails.
 *
 * Each operation is run once on a clean bus to count its transfers, then once per transfer with
 * exactly that transfer failing. This reaches every "the transfer failed, return the error" path in
 * the driver, which a single fault test per operation would not.
 */
#include <cstdio>

#include "as5047u.hpp"
#include "mock_as5047u.hpp"
#include "test_harness.hpp"

using as5047u::Error;
using as5047u::FrameFormat;
using as5047u_test::FullPort;
using as5047u_test::MinimalPort;
namespace reg = as5047u::reg;

namespace {

/// Runs `op` with each of its transfers failing in turn; every run must return Error::Bus.
template <typename Port, typename Op>
void SweepBusFaults(const char* name, const as5047u::Config& config, Op op) {
  int transfers = 0;
  {
    Port clean;
    as5047u::AS5047U<Port> sensor(clean, config);
    REQUIRE(sensor.EnsureInitialized().ok());
    const int before = clean.state.transfers;
    CHECK_EQ(op(sensor), Error::Ok);
    transfers = clean.state.transfers - before;
  }
  CHECK(transfers > 0);
  for (int i = 0; i < transfers; ++i) {
    Port port;
    as5047u::AS5047U<Port> sensor(port, config);
    REQUIRE(sensor.EnsureInitialized().ok());
    port.state.fail_transfer = port.state.transfers + i;
    const Error e = op(sensor);
    if (e != Error::Bus) {
      std::printf("    %s: transfer %d of %d failed, got %s\n", name, i + 1, transfers,
                  as5047u::ToString(e));
    }
    CHECK_EQ(e, Error::Bus);
  }
}

as5047u::Config Frame16() {
  as5047u::Config c{};
  c.frame = FrameFormat::Bits16;
  return c;
}

} // namespace

using S = as5047u::AS5047U<MinimalPort>;

TEST_CASE("Every transfer of every read can fail and is reported as Bus") {
  SweepBusFaults<MinimalPort>("ReadAngleCounts", {},
                              [](S& s) { return s.ReadAngleCounts().error; });
  SweepBusFaults<MinimalPort>("ReadVelocityRpm", Frame16(),
                              [](S& s) { return s.ReadVelocityRpm().error; });
  SweepBusFaults<MinimalPort>("ReadDiagnostics", {},
                              [](S& s) { return s.ReadDiagnostics().error; });
  SweepBusFaults<MinimalPort>("ReadErrorFlags", {}, [](S& s) { return s.ReadErrorFlags().error; });
  SweepBusFaults<MinimalPort>("ReadNonVolatile", {},
                              [](S& s) { return s.ReadNonVolatile().error; });
}

TEST_CASE("A transfer failing while ERRFL is fetched is reported as Bus") {
  for (int i = 0; i < 2; ++i) { // the two frames of the ERRFL read
    MinimalPort port;
    S s(port);
    REQUIRE(s.EnsureInitialized().ok());
    port.state.chip.errfl = reg::kErrflCordic;
    port.state.fail_transfer = port.state.transfers + 2 + i; // after the read's two frames
    CHECK_EQ(s.ReadAngleCounts().error, Error::Bus);
  }
}

TEST_CASE("Every transfer of every configuration change can fail and is reported as Bus") {
  SweepBusFaults<MinimalPort>("ApplyConfig", {}, [](S& s) { return s.ApplyConfig({}).error; });
  SweepBusFaults<MinimalPort>("SetZeroHere", {}, [](S& s) { return s.SetZeroHere().error; });
  SweepBusFaults<MinimalPort>("WriteRegister", Frame16(),
                              [](S& s) { return s.WriteRegister(reg::kZposm, 3).error; });
}

TEST_CASE("Every transfer of the OTP procedure can fail and is reported as Bus") {
  SweepBusFaults<MinimalPort>("ProgramOtp", {}, [](S& s) {
    as5047u::OtpReport report{};
    return s.ProgramOtp(as5047u::kOtpConfirm, report).error;
  });
}

TEST_CASE("Every transfer of Initialize can fail and is reported as Bus") {
  int transfers = 0;
  {
    MinimalPort clean;
    S s(clean);
    REQUIRE(s.EnsureInitialized().ok());
    transfers = clean.state.transfers;
  }
  for (int i = 0; i < transfers; ++i) {
    MinimalPort port;
    port.state.fail_transfer = i;
    S s(port);
    CHECK_EQ(s.EnsureInitialized().error, Error::Bus);
  }
}

TEST_CASE("A lost frame of ProgramOtp never lets a later call start a burn") {
  // Found by the fuzzer (tests/fuzz/): the data frame of "PROG = PROGOTP" fails before it reaches
  // the chip, which then takes the next command as that data and burns. The driver must send a
  // NOP read first, which the chip takes as PROG = 0.
  int transfers = 0;
  {
    FullPort clean;
    as5047u::AS5047U<FullPort> s(clean);
    REQUIRE(s.EnsureInitialized().ok());
    const int before = clean.state.transfers;
    as5047u::OtpReport report{};
    REQUIRE(s.ProgramOtp(as5047u::kOtpConfirm, report).ok());
    transfers = clean.state.transfers - before;
  }
  for (int i = 0; i < transfers; ++i) {
    FullPort port;
    as5047u::AS5047U<FullPort> s(port);
    REQUIRE(s.EnsureInitialized().ok());
    port.state.fail_transfer = port.state.transfers + i;
    as5047u::OtpReport report{};
    CHECK_EQ(s.ProgramOtp(as5047u::kOtpConfirm, report).error, Error::Bus);
    const int burns = port.state.chip.burn_starts;
    CHECK(burns == 0 || report.burn_started);
    for (int k = 0; k < 4; ++k) {
      (void)s.ReadAgc();
      (void)s.ReadAngleCounts();
    }
    port.DelayUs(10000);
    if (port.state.chip.burn_starts != burns) {
      std::printf("    transfer %d of %d failed: a later read started a burn\n", i + 1, transfers);
    }
    CHECK_EQ(port.state.chip.burn_starts, burns);
  }
}

TEST_CASE("After a failed transfer the next frame is a NOP read") {
  MinimalPort port;
  S s(port);
  REQUIRE(s.EnsureInitialized().ok());
  port.state.chip.raw_angle = 1234;
  const uint16_t before = port.state.chip.nv[reg::kSettings3 - reg::kDisable];
  port.state.fail_transfer = port.state.transfers + 1; // the data frame of the write
  CHECK_EQ(s.WriteRegister(reg::kSettings3, 0x0005).error, Error::Bus);
  const auto angle = s.ReadAngleCounts();
  REQUIRE(angle.ok());
  CHECK_EQ(angle.value, 1234); // the read command was not taken as SETTINGS3 data
  const uint16_t after = port.state.chip.nv[reg::kSettings3 - reg::kDisable];
  CHECK(after == before || after == 0); // at worst the NOP read wrote 0
}
