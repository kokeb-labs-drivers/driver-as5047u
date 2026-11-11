/**
 * @file test_model.cpp
 * @brief The chip model's register map against the driver's, at compile time (handbook KB-TST-030).
 *
 * tests/mock_as5047u.hpp is written from the datasheet without the driver's headers, so a constant
 * the driver gets wrong is not copied into the model. These checks make the two agree, or the build
 * fails: a difference is a mistake in one of them, found against the datasheet figure the model
 * names.
 */
#include <cstdint>

#include "as5047u.hpp"
#include "mock_as5047u.hpp"
#include "test_harness.hpp"

namespace {

namespace chip = as5047u_test::chip;
namespace reg = as5047u::reg;

// --8<-- [start:cross-check]
// Volatile registers (Figure 32 and Figures 33 to 41).
static_assert(chip::kNop == reg::kNop && chip::kErrfl == reg::kErrfl && chip::kProg == reg::kProg &&
              chip::kDia == reg::kDia && chip::kAgc == reg::kAgc);
static_assert(chip::kSinData == reg::kSinData && chip::kCosData == reg::kCosData &&
              chip::kVel == reg::kVel && chip::kMag == reg::kMag &&
              chip::kAngleUnc == reg::kAngleUnc && chip::kAngleCom == reg::kAngleCom &&
              chip::kEccS == reg::kEccChecksum);
// Non-volatile registers (Figure 42).
static_assert(chip::kDisable == reg::kDisable && chip::kZposm == reg::kZposm &&
              chip::kZposl == reg::kZposl && chip::kSettings1 == reg::kSettings1 &&
              chip::kSettings2 == reg::kSettings2 && chip::kSettings3 == reg::kSettings3 &&
              chip::kEcc == reg::kEcc && chip::kNvCount == reg::kNonVolatileCount);
// Implemented bits of each settings register (Figures 43 to 49).
static_assert(chip::kNvBits[0] == reg::NvImplementedBits(0) &&
              chip::kNvBits[1] == reg::NvImplementedBits(1) &&
              chip::kNvBits[2] == reg::NvImplementedBits(2) &&
              chip::kNvBits[3] == reg::NvImplementedBits(3) &&
              chip::kNvBits[4] == reg::NvImplementedBits(4) &&
              chip::kNvBits[5] == reg::NvImplementedBits(5) &&
              chip::kNvBits[6] == reg::NvImplementedBits(6));
// Frame words (Figures 21, 22).
static_assert(chip::kData == reg::kDataMask && chip::kRead == reg::kReadBit &&
              chip::kErrorBit == reg::kReplyErrorBit && chip::kWarningBit == reg::kReplyWarningBit);
// ERRFL (Figure 33): the flags the chip reports with each reply bit.
static_assert(chip::kAgcWarning == reg::kErrflAgcWarning && chip::kMagHalf == reg::kErrflMagHalf &&
              chip::kP2ramWarning == reg::kErrflP2ramWarning &&
              chip::kP2ramError == reg::kErrflP2ramError && chip::kFraming == reg::kErrflFraming &&
              chip::kCommandError == reg::kErrflCommand && chip::kCrcError == reg::kErrflCrc &&
              chip::kWatchdog == reg::kErrflWatchdog &&
              chip::kOffCompNotFinished == reg::kErrflOffComp &&
              chip::kCordicOverflow == reg::kErrflCordic);
static_assert(chip::kErrorFlags == (reg::kErrflFrameRejected | reg::kErrflDeviceFault));
static_assert((chip::kWarningFlags & reg::kErrflFieldWarning) == reg::kErrflFieldWarning);
// PROG (Figure 34) and DIA (Figure 35).
static_assert(chip::kProgEn == reg::kProgEnable && chip::kOtpRef == reg::kProgRefresh &&
              chip::kProgOtp == reg::kProgBurn && chip::kProgVer == reg::kProgVerify);
static_assert(chip::kDiaVdd5V == reg::kDiaVdd5V &&
              chip::kDiaLoopsFinished == reg::kDiaLoopsFinished &&
              chip::kDiaCosOffDone == reg::kDiaCosOffsetDone &&
              chip::kDiaSinOffDone == reg::kDiaSinOffsetDone &&
              chip::kDiaOffCompDone == reg::kDiaOffsetCompDone &&
              chip::kDiaAgcDone == reg::kDiaAgcDone &&
              chip::kDiaSpiCountShift == reg::kDiaSpiCountShift &&
              chip::kDiaSettled == reg::kDiaReadyValue);
// Settings fields (Figures 43 to 49).
static_assert(chip::kDisUvwOff == reg::kDisableUvwOff && chip::kDisAbiOff == reg::kDisableAbiOff &&
              chip::kDisFilter == reg::kDisableFilterOff && chip::kZposmBits == reg::kZposmMask &&
              chip::kZposlBits == reg::kZposlMask && chip::kZposlDiaEn == reg::kZposlDiaBits &&
              chip::kS1DiaEn == reg::kS1DiaBits);
static_assert(chip::kS2Iwidth == reg::kS2IndexWidth1 && chip::kS2NoiseSet == reg::kS2NoiseSet &&
              chip::kS2Dir == reg::kS2Dir && chip::kS2UvwAbi == reg::kS2UvwAbi &&
              chip::kS2DaecDis == reg::kS2DaecDis && chip::kS2AbiDec == reg::kS2AbiDecimal &&
              chip::kS2DataSelect == reg::kS2DataSelect && chip::kS2PwmOn == reg::kS2PwmOn &&
              chip::kEccEn == reg::kEccEnable && chip::kEccKey == reg::kEccChecksumMask);
// Timing (Figure 9).
static_assert(chip::kPowerOnUs == as5047u::kPowerOnTimeMs * 1000U);
// --8<-- [end:cross-check]

// The model's bitwise CRC-8 and the driver's table agree on the line patterns and a command.
static_assert(chip::Crc8(0x0000U) == as5047u::Crc8(0x0000U));
static_assert(chip::Crc8(0xFFFFU) == as5047u::Crc8(0xFFFFU));
static_assert(chip::Crc8(0x7FFFU) == as5047u::Crc8(0x7FFFU));

} // namespace

TEST_CASE("The driver's table CRC-8 equals the model's and its own bitwise CRC for every word") {
  int mismatches = 0;
  for (uint32_t w = 0; w <= 0xFFFFU; ++w) {
    const auto word = static_cast<uint16_t>(w);
    if (as5047u::Crc8(word) != chip::Crc8(word) ||
        as5047u::Crc8(word) != as5047u::detail::Crc8Bitwise(word)) {
      ++mismatches;
    }
  }
  CHECK_EQ(mismatches, 0);
}
