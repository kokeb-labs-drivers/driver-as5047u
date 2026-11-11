/**
 * @file test_failed_writes.cpp
 * @brief A write whose reply failed may have taken effect: `GetConfig()` must not claim a value the
 * chip may not hold, and the driver never retries the write.
 *
 * The chip stores the data frame of a write when it arrives (Figure 30), whatever happens to the
 * reply on MISO, so a reply CRC error or a lost transfer leaves the register at the old value, the
 * new one, or (after a lost frame) 0.
 */
#include "as5047u.hpp"
#include "mock_as5047u.hpp"
#include "test_harness.hpp"

using as5047u::Direction;
using as5047u::Error;
using as5047u_test::MinimalPort;
namespace reg = as5047u::reg;
namespace chip = as5047u_test::chip;

namespace {

// SetDirection(): DISABLE to ECC read (14 frames), then DISABLE, ZPOSM, ZPOSL and SETTINGS1
// written (8 frames): frame 22 is the SETTINGS2 command, 23 its data frame.
constexpr int kSettings2DataFrame = 23;
constexpr int kWritesOfApplyConfig = 12; // DISABLE to SETTINGS3, two frames each

struct Rig {
  MinimalPort port;
  as5047u::AS5047U<MinimalPort> s{port};
  Rig() {
    REQUIRE(s.EnsureInitialized().ok());
  }
};

} // namespace

TEST_CASE("A write whose reply fails its CRC: GetConfig() follows what the chip took") {
  Rig rig;
  rig.port.state.chip.corrupt_reply_frame = rig.port.state.chip.frames + kSettings2DataFrame;
  const int frames = rig.port.state.transfers;
  CHECK_EQ(rig.s.SetDirection(Direction::CounterClockwise).error, Error::Crc);
  CHECK((rig.port.state.chip.nv[4] & chip::kS2Dir) != 0U);           // the chip took it
  CHECK(rig.s.GetConfig().direction == Direction::CounterClockwise); // and the driver says so
  CHECK_EQ(rig.port.state.transfers - frames, 14 + 10 + 14);         // read, writes, read-back
  CHECK(rig.s.IsInitialized());
  CHECK(rig.s.CheckForReset().ok()); // the reset check's copy follows the chip too
}

TEST_CASE("A failed write is never retried; later registers keep what the chip holds") {
  Rig rig;
  as5047u::Config c{};
  c.zero_position = 4000;
  c.direction = Direction::CounterClockwise;
  c.pwm_enable = true;
  c.uvw_pole_pairs = 5;
  rig.port.state.fail_transfer = rig.port.state.transfers + kSettings2DataFrame; // SETTINGS2 data
  const int frames = rig.port.state.transfers;
  CHECK_EQ(rig.s.ApplyConfig(c).error, Error::Bus);
  CHECK_EQ(rig.port.state.chip.burn_starts, 0);
  // ZPOSM and ZPOSL went out before the failure: the zero is the new one. SETTINGS2 was lost and
  // SETTINGS3 never sent: direction, PWM and pole pairs stay as the chip has them.
  const as5047u::Config& now = rig.s.GetConfig();
  CHECK_EQ(now.zero_position, 4000);
  CHECK(now.direction == Direction::Clockwise);
  CHECK(!now.pwm_enable);
  CHECK_EQ(now.uvw_pole_pairs, 1);
  // One write sequence, then the read-back (and its NOP resync frame): no second attempt.
  CHECK(rig.port.state.transfers - frames < 14 + kWritesOfApplyConfig + 14 + 2);
  CHECK(rig.s.CheckForReset().ok());
}

TEST_CASE("A failed write whose read-back fails too leaves the driver uninitialised") {
  Rig rig;
  rig.port.state.chip.corrupt_reply_frame = rig.port.state.chip.frames + kSettings2DataFrame;
  rig.port.state.fail_transfer = rig.port.state.transfers + kSettings2DataFrame + 1;
  CHECK_EQ(rig.s.SetDirection(Direction::CounterClockwise).error, Error::Crc);
  CHECK(!rig.s.IsInitialized());
  CHECK(rig.s.GetConfig().direction == Direction::Clockwise); // the configuration to write again
  CHECK_EQ(rig.s.ReadAngleCounts().error, Error::NotInitialized);
  REQUIRE(rig.s.EnsureInitialized().ok());
  CHECK((rig.port.state.chip.nv[4] & chip::kS2Dir) == 0U); // written again, verified
}

TEST_CASE("A write the chip rejected: GetConfig() keeps what the chip still holds") {
  Rig rig;
  rig.port.state.chip.ignore_writes = true;
  rig.port.state.chip.ignore_write_address = chip::kSettings2;
  CHECK_EQ(rig.s.SetDirection(Direction::CounterClockwise, as5047u::Verify::ReadBack).error,
           Error::WriteMismatch);
  CHECK(rig.s.GetConfig().direction == Direction::Clockwise);
  CHECK(rig.s.IsInitialized());
}

TEST_CASE("A failed raw write to a settings register refreshes the reset check's copy") {
  Rig rig;
  rig.port.state.chip.corrupt_reply_frame = rig.port.state.chip.frames + 1; // the data frame
  CHECK_EQ(rig.s.WriteRegister(reg::kSettings3, 2).error, Error::Crc);
  CHECK_EQ(rig.port.state.chip.nv[5], 2);        // the chip took it
  CHECK(rig.s.GetConfig() == as5047u::Config{}); // raw writes never change GetConfig()
  CHECK(rig.s.CheckForReset().ok());             // not mistaken for a reset
  rig.port.state.chip.corrupt_reply_frame = rig.port.state.chip.frames + 1;
  CHECK_EQ(rig.s.WriteRegister(reg::kAgc, 2).error, Error::Crc); // not a settings register
  CHECK(rig.s.IsInitialized());
}
