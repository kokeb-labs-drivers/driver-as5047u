/**
 * @file test_reset.cpp
 * @brief A reset of the chip in the middle of a session (power-on reset, brown-out), and ERRFL
 * flags lost to a failed read of the clear-on-read register.
 *
 * The model's `PowerOnReset()` does what the datasheet says a hardware reset does: the soft-written
 * settings are lost and reload from the OTP ("Non-Volatile Registers (OTP)"), DIA reports the
 * loops finished only after t_pon (Figure 9) and ERRFL holds OffCompNotFinished until then
 * (Figure 33). The driver must notice, never carry on with its cached settings, and say so with
 * `DeviceReset`.
 */
#include "as5047u.hpp"
#include "mock_as5047u.hpp"
#include "test_harness.hpp"

using as5047u::Error;
using as5047u_test::MinimalPort;
namespace reg = as5047u::reg;
namespace chip = as5047u_test::chip;

namespace {

constexpr uint16_t kRawAngle = 5000;
constexpr uint16_t kZero = 4000;
constexpr uint16_t kAngleConfigured = 7384; // (16384 - 5000) - 4000: counter-clockwise, zero 4000
constexpr int kSettingsCheckFrames = 14;    // DISABLE to ECC, two frames each

as5047u::Config Configured() {
  as5047u::Config c{};
  c.zero_position = kZero;
  c.direction = as5047u::Direction::CounterClockwise;
  return c;
}

/// A started driver with a non-default zero and direction, over a chip whose OTP is blank.
struct Rig {
  MinimalPort port;
  as5047u::AS5047U<MinimalPort> s{port, Configured()};
  Rig() {
    port.state.chip.raw_angle = kRawAngle;
    REQUIRE(s.EnsureInitialized().ok());
    s.ClearLatchedErrorFlags();
  }
  void PastPowerOn() {
    port.DelayUs(chip::kPowerOnUs);
  }
};

} // namespace

TEST_CASE("Model: a power-on reset reloads the OTP, restarts t_pon and raises OffCompNotFinished") {
  as5047u_test::As5047uModel m;
  m.otp[1] = 0x12;
  m.nv = {1, 2, 3, 4, 5, 6, 7};
  m.prog = 1;
  m.PowerOnReset();
  CHECK(m.nv == m.otp);
  CHECK_EQ(m.prog, 0);
  CHECK_EQ(m.errfl, chip::kOffCompNotFinished);
  CHECK(!m.Ready());
  CHECK_EQ(m.resets, 1);
  m.Elapse(chip::kPowerOnUs);
  CHECK(m.Ready());
}

TEST_CASE("A reset that reloads a blank OTP is DeviceReset, and EnsureInitialized restores it") {
  // The auditor's repro B: before, the angle moved from 7384 to 5000 with an ok read.
  Rig rig;
  auto before = rig.s.ReadAngleCounts();
  REQUIRE(before.ok());
  CHECK_EQ(before.value, kAngleConfigured);
  rig.port.state.chip.PowerOnReset();
  rig.PastPowerOn();
  const int frames = rig.port.state.transfers;
  const auto a = rig.s.ReadAngleCounts();
  CHECK_EQ(a.error, Error::DeviceReset);
  CHECK_EQ(a.value, 0);
  CHECK_EQ(rig.port.state.transfers - frames, 4 + kSettingsCheckFrames);
  CHECK(!rig.s.IsInitialized());
  CHECK(rig.s.GetLatchedErrorFlags().offset_comp);
  CHECK_EQ(rig.s.ReadAngleCounts().error, Error::NotInitialized);
  CHECK_EQ(rig.s.GetConfig().zero_position, kZero); // the configuration to write again
  REQUIRE(rig.s.EnsureInitialized().ok());
  const auto after = rig.s.ReadAngleCounts();
  REQUIRE(after.ok());
  CHECK_EQ(after.value, kAngleConfigured);
}

TEST_CASE("A read during t_pon after a reset is DeviceReset, not a value") {
  Rig rig;
  rig.port.state.chip.PowerOnReset(); // the settings check itself reads flagged replies
  CHECK_EQ(rig.s.ReadAngleCounts().error, Error::DeviceReset);
  CHECK(!rig.s.IsInitialized());
  CHECK_EQ(rig.s.EnsureInitialized().error, Error::Ok); // waits out t_pon, then writes again
  CHECK_EQ(rig.s.ReadAngleCounts().value, kAngleConfigured);
}

TEST_CASE("A setter after a reset writes nothing from the cache and returns DeviceReset") {
  Rig rig;
  rig.port.state.chip.PowerOnReset();
  rig.PastPowerOn();
  CHECK_EQ(rig.s.SetDirection(as5047u::Direction::Clockwise).error, Error::DeviceReset);
  CHECK(rig.port.state.chip.nv == rig.port.state.chip.otp); // still what the reset loaded
  CHECK(!rig.s.IsInitialized());
}

TEST_CASE("ReadErrorFlags after a reset is DeviceReset; the flag stays in the latch") {
  Rig rig;
  rig.port.state.chip.PowerOnReset();
  rig.PastPowerOn();
  CHECK_EQ(rig.s.ReadErrorFlags().error, Error::DeviceReset);
  CHECK(rig.s.GetLatchedErrorFlags().offset_comp);
  CHECK(rig.s.GetLastErrorFlags().offset_comp);
  CHECK(!rig.s.IsInitialized());
}

TEST_CASE("A watchdog flag with the settings changed is DeviceReset") {
  Rig rig;
  rig.port.state.chip.nv = rig.port.state.chip.otp;
  rig.port.state.chip.errfl = reg::kErrflWatchdog;
  CHECK_EQ(rig.s.ReadRegister(reg::kMag).error, Error::DeviceReset);
  CHECK(!rig.s.IsInitialized());
}

TEST_CASE("OffCompNotFinished with the settings unchanged stays DeviceFault, still initialised") {
  Rig rig;
  rig.port.state.chip.errfl = reg::kErrflOffComp;
  const int frames = rig.port.state.transfers;
  CHECK_EQ(rig.s.ReadAngleCounts().error, Error::DeviceFault);
  CHECK_EQ(rig.port.state.transfers - frames, 4 + kSettingsCheckFrames);
  CHECK(rig.s.IsInitialized());
  CHECK(rig.s.ReadAngleCounts().ok());
}

TEST_CASE("Raw writes to a settings register do not look like a reset") {
  Rig rig;
  REQUIRE(rig.s.WriteRegister(reg::kZposm, 5).ok());
  rig.port.state.chip.errfl = reg::kErrflWatchdog;
  CHECK_EQ(rig.s.WriteRegister(reg::kSettings3, 1).error, Error::DeviceFault);
  CHECK(rig.s.IsInitialized());
  CHECK(rig.s.CheckForReset().ok());
}

TEST_CASE("A burned part whose OTP holds the configuration survives a reset") {
  MinimalPort port;
  port.state.chip.otp = {0, 0x3E, 0x20, 0, 0x04, 0, 0}; // zero 4000, counter-clockwise
  port.state.chip.otp_burned = true;
  port.state.chip.nv = port.state.chip.otp;
  port.state.chip.raw_angle = kRawAngle;
  as5047u::Config c{};
  c.write_settings = false;
  as5047u::AS5047U<MinimalPort> s(port, c);
  REQUIRE(s.EnsureInitialized().ok());
  CHECK_EQ(s.GetConfig().zero_position, kZero);
  port.state.chip.PowerOnReset();
  port.DelayUs(chip::kPowerOnUs);
  CHECK_EQ(s.ReadAngleCounts().error, Error::DeviceFault); // offset compensation was restarting
  CHECK(s.IsInitialized());
  const auto a = s.ReadAngleCounts();
  REQUIRE(a.ok());
  CHECK_EQ(a.value, kAngleConfigured);
}

TEST_CASE("CheckForReset finds a reload that ERRFL did not report, in 14 frames") {
  Rig rig;
  int frames = rig.port.state.transfers;
  CHECK(rig.s.CheckForReset().ok());
  CHECK_EQ(rig.port.state.transfers - frames, kSettingsCheckFrames);
  CHECK(rig.s.IsInitialized());
  rig.port.state.chip.nv = rig.port.state.chip.otp; // a reload with no flag: the auditor's repro B
  frames = rig.port.state.transfers;
  CHECK_EQ(rig.s.CheckForReset().error, Error::DeviceReset);
  CHECK_EQ(rig.port.state.transfers - frames, kSettingsCheckFrames);
  CHECK(!rig.s.IsInitialized());
  CHECK_EQ(rig.s.CheckForReset().error, Error::NotInitialized);
  REQUIRE(rig.s.EnsureInitialized().ok());
  CHECK_EQ(rig.s.ReadAngleCounts().value, kAngleConfigured);
}

TEST_CASE("CheckForReset: a failed read keeps the driver initialised, unless ERRFL hints a reset") {
  Rig rig;
  rig.port.state.fail_transfer = rig.port.state.transfers + 1;
  CHECK_EQ(rig.s.CheckForReset().error, Error::Bus);
  CHECK(rig.s.IsInitialized());
  rig.port.state.chip.errfl = reg::kErrflCordic;
  CHECK_EQ(rig.s.CheckForReset().error, Error::DeviceFault);
  CHECK(rig.s.IsInitialized());
  rig.port.state.chip.PowerOnReset(); // within t_pon: every settings read is flagged
  CHECK_EQ(rig.s.CheckForReset().error, Error::DeviceReset);
  CHECK(!rig.s.IsInitialized());
}

TEST_CASE("An ERRFL read whose reply fails its CRC sets flags_lost") {
  // The auditor's repro A: ERRFL is cleared by the read whose reply was corrupted.
  MinimalPort p;
  as5047u::AS5047U<MinimalPort> d(p);
  REQUIRE(d.EnsureInitialized().ok());
  d.ClearLatchedErrorFlags();
  CHECK(!d.GetLatchedErrorFlags().flags_lost);
  p.state.chip.errfl = chip::kCordicOverflow;
  p.state.chip.corrupt_reply_frame = p.state.chip.frames + 3; // the NOP that carries ERRFL
  CHECK_EQ(d.ReadAngleCounts().error, Error::Crc);
  CHECK_EQ(p.state.chip.errfl, 0); // gone from the chip
  CHECK(d.GetLatchedErrorFlags().flags_lost);
  CHECK(!d.GetLatchedErrorFlags().cordic_overflow); // never seen
  CHECK(d.ReadAngleCounts().ok());
  CHECK(d.GetLatchedErrorFlags().flags_lost); // until cleared
  d.ClearLatchedErrorFlags();
  CHECK(!d.GetLatchedErrorFlags().flags_lost);
}

TEST_CASE("flags_lost: set by a reserved bit or a failed transfer, cleared by a new start") {
  MinimalPort p;
  as5047u::AS5047U<MinimalPort> d(p);
  REQUIRE(d.EnsureInitialized().ok());
  p.state.chip.errfl = reg::kErrflAgcWarning;
  REQUIRE(d.ReadErrorFlags().ok());
  CHECK(!d.GetLatchedErrorFlags().flags_lost); // a good read loses nothing
  p.state.chip.errfl = reg::kErrflReserved;
  CHECK_EQ(d.ReadErrorFlags().error, Error::NoDevice);
  CHECK(d.GetLatchedErrorFlags().flags_lost);
  d.ClearLatchedErrorFlags();
  p.state.fail_transfer = p.state.transfers + 1; // the NOP after the ERRFL command
  CHECK_EQ(d.ReadRegister(reg::kErrfl).error, Error::Bus);
  CHECK(d.GetLatchedErrorFlags().flags_lost);
  CHECK(!d.GetLastErrorFlags().flags_lost); // the latch only
  REQUIRE(d.Deinitialize().ok());
  REQUIRE(d.EnsureInitialized().ok());
  CHECK(!d.GetLatchedErrorFlags().flags_lost);
}

TEST_CASE("A reset whose only flag is lost to a corrupted ERRFL reply is still DeviceReset") {
  // The reviewer's probe C: ERRFL clears on read, so the corrupted read took OffCompNotFinished.
  Rig rig;
  rig.port.state.chip.PowerOnReset();
  rig.PastPowerOn();
  rig.port.state.chip.corrupt_reply_frame = rig.port.state.chip.frames + 3; // the ERRFL NOP
  const int frames = rig.port.state.transfers;
  CHECK_EQ(rig.s.ReadAngleCounts().error, Error::DeviceReset);
  CHECK_EQ(rig.port.state.transfers - frames, 4 + kSettingsCheckFrames);
  CHECK(rig.s.GetLatchedErrorFlags().flags_lost);
  CHECK(!rig.s.GetLatchedErrorFlags().offset_comp); // never seen
  CHECK(!rig.s.IsInitialized());
}

TEST_CASE("A lost ERRFL with the settings unchanged keeps the call's error and costs 14 frames") {
  Rig rig;
  rig.port.state.chip.errfl = reg::kErrflCordic;
  rig.port.state.chip.corrupt_reply_frame = rig.port.state.chip.frames + 3;
  const int frames = rig.port.state.transfers;
  CHECK_EQ(rig.s.ReadAngleCounts().error, Error::Crc);
  CHECK_EQ(rig.port.state.transfers - frames, 4 + kSettingsCheckFrames);
  CHECK(rig.s.IsInitialized());
  const int next = rig.port.state.transfers;
  CHECK(rig.s.ReadAngleCounts().ok());
  CHECK_EQ(rig.port.state.transfers - next, 2); // nothing carried over
}

TEST_CASE("A lost ERRFL whose compare cannot be read: the call's error, the compare next time") {
  Rig rig;
  rig.port.state.chip.errfl = reg::kErrflCordic;
  rig.port.state.chip.corrupt_reply_frame = rig.port.state.chip.frames + 3;
  rig.port.state.fail_transfer = rig.port.state.transfers + 4; // the compare's first frame
  CHECK_EQ(rig.s.ReadAngleCounts().error, Error::Crc);
  CHECK(rig.s.IsInitialized());
  rig.port.state.chip.PowerOnReset(); // the reset the lost flags might have shown
  rig.PastPowerOn();
  rig.port.state.chip.errfl = 0;                               // and its flag gone
  CHECK_EQ(rig.s.ReadAngleCounts().error, Error::DeviceReset); // the pending compare finds it
  CHECK(!rig.s.IsInitialized());
}

TEST_CASE("Raw writes of unimplemented bits do not look like a reset") {
  // The reviewer's probe D: DISABLE implements bits 6..0, ZPOSM 7..0 (Figures 43, 44).
  Rig rig;
  REQUIRE(rig.s.WriteRegister(reg::kZposm, 0x3FFF).ok());
  REQUIRE(rig.s.WriteRegister(reg::kDisable, 0x3FFF).ok());
  CHECK_EQ(rig.port.state.chip.nv[1], 0xFF);
  CHECK_EQ(rig.port.state.chip.nv[0], 0x7F);
  CHECK(rig.s.CheckForReset().ok());
  CHECK(rig.s.IsInitialized());
}

TEST_CASE("A reset hint raised by the read-back after a failed write is resolved in that call") {
  Rig rig;
  rig.port.state.chip.corrupt_reply_frame = rig.port.state.chip.frames + 1; // the data frame
  rig.port.state.chip.fault_on_read_of = chip::kDisable; // the read-back's first read is flagged
  rig.port.state.chip.fault_bits = static_cast<uint16_t>(chip::kWatchdog);
  CHECK_EQ(rig.s.WriteRegister(reg::kSettings3, 2).error, Error::DeviceReset);
  CHECK(!rig.s.IsInitialized());
  rig.port.state.chip.fault_on_read_of = -1;
  REQUIRE(rig.s.EnsureInitialized().ok());
  const int frames = rig.port.state.transfers;
  CHECK(rig.s.ReadAngleCounts().ok());
  CHECK_EQ(rig.port.state.transfers - frames, 2); // nothing carried over
}

TEST_CASE("While the compare after a lost ERRFL cannot be read, no call returns a value") {
  // The reviewer's probe 3: a reset, its flag lost to a corrupted ERRFL reply, the compare that
  // follows failed, then a frame error at every offset of the next call.
  for (int k = 0; k < 20; ++k) {
    for (const bool bus : {false, true}) {
      Rig rig;
      rig.port.state.chip.PowerOnReset();
      rig.PastPowerOn();
      rig.port.state.chip.corrupt_reply_frame = rig.port.state.chip.frames + 3; // the ERRFL NOP
      rig.port.state.fail_transfer = rig.port.state.transfers + 5; // inside the compare
      CHECK_EQ(rig.s.ReadAngleCounts().error, Error::Crc);         // call 1: its own error
      CHECK(rig.s.IsInitialized());
      if (bus) {
        rig.port.state.fail_transfer = rig.port.state.transfers + k;
      } else {
        rig.port.state.chip.corrupt_reply_frame = rig.port.state.chip.frames + k;
      }
      const auto b = rig.s.ReadAngleCounts(); // call 2
      CHECK(!b.ok());                         // never the shifted angle 5000
      CHECK_EQ(b.value, 0);
      // Without faults the compare is read: it finds the reset (if call 2 had not already).
      rig.port.state.fail_transfer = -1;
      rig.port.state.chip.corrupt_reply_frame = -1;
      const auto c = rig.s.ReadAngleCounts();
      CHECK((c.error == Error::DeviceReset) || (c.error == Error::NotInitialized));
      REQUIRE(rig.s.EnsureInitialized().ok());
      CHECK_EQ(rig.s.ReadAngleCounts().value, kAngleConfigured);
    }
  }
}

TEST_CASE("A pending compare that passes lets the call return its value") {
  Rig rig;
  rig.port.state.chip.errfl = reg::kErrflCordic;
  rig.port.state.chip.corrupt_reply_frame = rig.port.state.chip.frames + 3;
  rig.port.state.fail_transfer = rig.port.state.transfers + 4;
  CHECK_EQ(rig.s.ReadAngleCounts().error, Error::Crc);
  rig.port.state.fail_transfer = rig.port.state.transfers + 3; // the next compare fails too
  CHECK_EQ(rig.s.ReadAngleCounts().error, Error::Bus);
  const int frames = rig.port.state.transfers;
  const auto a = rig.s.ReadAngleCounts(); // the compare passes: unchanged settings
  REQUIRE(a.ok());
  CHECK_EQ(a.value, kAngleConfigured);
  CHECK(rig.port.state.transfers - frames >= 2 + kSettingsCheckFrames);
  const int next = rig.port.state.transfers;
  CHECK(rig.s.ReadAngleCounts().ok());
  CHECK_EQ(rig.port.state.transfers - next, 2); // resolved: nothing pending
}
