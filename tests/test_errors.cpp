/**
 * @file test_errors.cpp
 * @brief The `Error` enum follows the standard, and its values are returned by real calls.
 *
 * - The base set (0 to 9) has the names and values every driver of the org shares.
 * - Every chip-specific value (16 and up) MUST be returned by some call: the enumerators are found
 *   through `ToString()` (-Wswitch keeps it complete), so a new value fails this test until a
 *   scenario below produces it.
 * - Base values MAY go unreturned: the AS5047U has no busy state and no optional feature, so
 *   `Busy` and `Unsupported` are never returned (the API guide lists them).
 */
#include <array>
#include <cstdio>
#include <cstring>

#include "as5047u.hpp"
#include "mock_as5047u.hpp"
#include "test_harness.hpp"

using as5047u::Error;
using as5047u_test::MinimalPort;
namespace reg = as5047u::reg;

static_assert(static_cast<int>(Error::Ok) == 0 && static_cast<int>(Error::NotInitialized) == 1 &&
                  static_cast<int>(Error::InvalidArgument) == 2 &&
                  static_cast<int>(Error::Bus) == 3 && static_cast<int>(Error::Timeout) == 4 &&
                  static_cast<int>(Error::DeviceIdMismatch) == 5 &&
                  static_cast<int>(Error::Crc) == 6 && static_cast<int>(Error::Busy) == 7 &&
                  static_cast<int>(Error::DeviceFault) == 8 &&
                  static_cast<int>(Error::Unsupported) == 9,
              "the base Error set has fixed values (standards/driver.md)");

namespace {

constexpr bool kRequireEveryBaseValue = false;
constexpr int kFirstChipSpecific = 16;

// Which Error values some scenario returned; written by Record() while the scenarios run.
std::array<bool, 256> g_seen{}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

void Record(Error e) {
  g_seen[static_cast<uint8_t>(e)] = true;
}

/// Initialise, set ERRFL to `bits`, and record what the next read returns.
void RecordFlag(uint16_t bits) {
  MinimalPort port;
  as5047u::AS5047U<MinimalPort> s(port);
  if (s.EnsureInitialized().ok()) {
    port.state.chip.errfl = bits;
    Record(s.ReadAngleCounts().error);
  }
}

void RunScenarios() {
  { // Ok, NotInitialized, InvalidArgument
    MinimalPort port;
    as5047u::AS5047U<MinimalPort> s(port);
    Record(s.ReadAngleCounts().error);
    Record(s.EnsureInitialized().error);
    Record(s.WriteRegister(reg::kProg, 0).error);
  }
  { // Bus
    MinimalPort port;
    port.state.bus_up_ok = false;
    as5047u::AS5047U<MinimalPort> s(port);
    Record(s.EnsureInitialized().error);
  }
  { // DeviceIdMismatch: something answers, but DIA has bit 10 set
    MinimalPort port;
    port.state.chip.dia = static_cast<uint16_t>(port.state.chip.dia | 0x0400U);
    as5047u::AS5047U<MinimalPort> s(port);
    Record(s.EnsureInitialized().error);
  }
  { // NoDevice: MISO floats high
    MinimalPort port;
    port.state.chip.line = as5047u_test::LineMode::FloatHigh;
    as5047u::AS5047U<MinimalPort> s(port);
    Record(s.EnsureInitialized().error);
  }
  { // NotReady: the chip answers, the loops never finish
    MinimalPort port;
    port.state.chip.never_ready = true;
    as5047u::AS5047U<MinimalPort> s(port);
    Record(s.EnsureInitialized().error);
  }
  { // MagnetWarning: an angle under an AGC warning
    MinimalPort port;
    as5047u::AS5047U<MinimalPort> s(port);
    if (s.EnsureInitialized().ok()) {
      port.state.chip.errfl = reg::kErrflAgcWarning;
      Record(s.ReadAngleCounts().error);
    }
  }
  { // DeviceReset: the chip resets and reloads a blank OTP over a non-default zero
    MinimalPort port;
    as5047u::Config c{};
    c.zero_position = 4000;
    as5047u::AS5047U<MinimalPort> s(port, c);
    if (s.EnsureInitialized().ok()) {
      port.state.chip.PowerOnReset();
      Record(s.ReadAngleCounts().error);
    }
  }
  RecordFlag(reg::kErrflCrc);     // Crc
  RecordFlag(reg::kErrflCordic);  // DeviceFault
  RecordFlag(reg::kErrflFraming); // Framing
  RecordFlag(reg::kErrflCommand); // Command
  {                               // WriteMismatch
    MinimalPort port;
    port.state.chip.ignore_writes = true;
    as5047u::Config c{};
    c.zero_position = 1;
    as5047u::AS5047U<MinimalPort> s(port, c);
    Record(s.EnsureInitialized().error);
  }
  { // Timeout and OtpVerify
    as5047u::OtpReport report{};
    MinimalPort slow;
    slow.state.chip.burn_never_finishes = true;
    as5047u::AS5047U<MinimalPort> a(slow);
    if (a.EnsureInitialized().ok()) {
      Record(a.ProgramOtp(as5047u::kOtpConfirm, report).error);
    }
    MinimalPort weak;
    weak.state.chip.guard_band_fails = true;
    as5047u::AS5047U<MinimalPort> b(weak);
    if (b.EnsureInitialized().ok()) {
      Record(b.ProgramOtp(as5047u::kOtpConfirm, report).error);
    }
  }
}

} // namespace

TEST_CASE("Every chip-specific Error value is returned by some call") {
  RunScenarios();
  int enumerators = 0;
  for (int v = 0; v < 256; ++v) {
    const auto e = static_cast<Error>(v);
    if (std::strcmp(as5047u::ToString(e), "Unknown") == 0) {
      continue;
    }
    ++enumerators;
    const bool required = v >= kFirstChipSpecific || kRequireEveryBaseValue;
    const bool seen = g_seen[static_cast<std::size_t>(v)];
    if (!seen) {
      std::printf("    %s: Error::%s is never returned\n", required ? "FAIL" : "note",
                  as5047u::ToString(e));
    }
    CHECK(seen || !required);
    CHECK(v < 10 || v >= kFirstChipSpecific); // 10 to 15 are reserved for the base set
  }
  CHECK(enumerators >= 2);
}
