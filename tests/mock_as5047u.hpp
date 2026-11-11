/**
 * @file mock_as5047u.hpp
 * @brief A frame-level model of the AS5047U for the host tests, plus the mock ports in
 * mock_as5047u_bus.hpp.
 *
 * The model speaks the protocol of the datasheet (DS000637 v1-01, "SPI Transaction"): the frame
 * length is found from the byte count, the reply to a frame answers the previous one, MOSI CRCs
 * are checked, reads of ERRFL clear it, writes take a command and a data frame, PROG runs the OTP
 * state machine with a burn time, and DIA reports the magnetic loops finished only once t_pon has
 * passed on the model's virtual clock (`Elapse()`, driven by the mock port's `DelayUs()`). Faults
 * are injected per frame, per register, or as a line mode of MISO/MOSI.
 *
 * The model is written from the datasheet on its own (handbook KB-TST-030): it does not include
 * the driver's `as5047u_registers.hpp`, `as5047u_types.hpp` or its CRC, but carries its own
 * register map, field bits, timing and CRC-8 below (namespace `chip`, each with its datasheet
 * figure), so a wrong constant in the driver cannot hide behind the same wrong constant in the
 * model. tests/test_model.cpp checks the two maps against each other at compile time.
 */
#ifndef AS5047U_MOCK_AS5047U_HPP
#define AS5047U_MOCK_AS5047U_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace as5047u_test {

/// The chip as the datasheet describes it, independent of the driver's headers.
namespace chip {
// Volatile registers (Figure 32; ERRFL Figure 33, PROG Figure 34, DIA Figure 35, ECC_s Figure 40).
inline constexpr uint16_t kNop = 0x0000;
inline constexpr uint16_t kErrfl = 0x0001;
inline constexpr uint16_t kProg = 0x0003;
inline constexpr uint16_t kDia = 0x3FF5;
inline constexpr uint16_t kAgc = 0x3FF9;
inline constexpr uint16_t kSinData = 0x3FFA;
inline constexpr uint16_t kCosData = 0x3FFB;
inline constexpr uint16_t kVel = 0x3FFC;
inline constexpr uint16_t kMag = 0x3FFD;
inline constexpr uint16_t kAngleUnc = 0x3FFE;
inline constexpr uint16_t kAngleCom = 0x3FFF;
inline constexpr uint16_t kEccS = 0x3FD0;
// Non-volatile registers (Figure 42): DISABLE 0x0015 to ECC 0x001B.
inline constexpr uint16_t kDisable = 0x0015;
inline constexpr uint16_t kZposm = 0x0016;
inline constexpr uint16_t kZposl = 0x0017;
inline constexpr uint16_t kSettings1 = 0x0018;
inline constexpr uint16_t kSettings2 = 0x0019;
inline constexpr uint16_t kSettings3 = 0x001A;
inline constexpr uint16_t kEcc = 0x001B;
inline constexpr std::size_t kNvCount = 7;
/// The bits each of DISABLE to ECC implements (Figures 43 to 49): DISABLE 6..0, the rest 7..0.
/// The datasheet does not describe the others; the model reads them back as 0.
inline constexpr std::array<uint16_t, kNvCount> kNvBits{0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
// Frame words (Figures 21, 22): bit 14 read on MOSI; bit 15 warning, bit 14 error on MISO.
inline constexpr uint32_t kData = 0x3FFFU;
inline constexpr uint32_t kRead = 0x4000U;
inline constexpr uint32_t kErrorBit = 0x4000U;
inline constexpr uint32_t kWarningBit = 0x8000U;
// ERRFL bits (Figure 33).
inline constexpr uint32_t kAgcWarning = 1U << 0U;
inline constexpr uint32_t kMagHalf = 1U << 1U;
inline constexpr uint32_t kP2ramWarning = 1U << 2U;
inline constexpr uint32_t kP2ramError = 1U << 3U;
inline constexpr uint32_t kFraming = 1U << 4U;
inline constexpr uint32_t kCommandError = 1U << 5U;
inline constexpr uint32_t kCrcError = 1U << 6U;
inline constexpr uint32_t kWatchdog = 1U << 7U;
inline constexpr uint32_t kOffCompNotFinished = 1U << 9U;
inline constexpr uint32_t kCordicOverflow = 1U << 10U;
/// ERRFL flags the chip reports with the error bit; the rest are warnings. The datasheet names the
/// bits "Error Bit" and "Warning Bit" (Figure 22) and the flags "error" or "warning" (Figure 33)
/// but does not state the mapping: this split is inferred from the flag names. The fault mode
/// `warnings_set_error_bit` covers the other reading.
inline constexpr uint32_t kErrorFlags = kP2ramError | kFraming | kCommandError | kCrcError |
                                        kWatchdog | kOffCompNotFinished | kCordicOverflow;
inline constexpr uint32_t kWarningFlags = kAgcWarning | kMagHalf | kP2ramWarning;
// PROG bits (Figure 34).
inline constexpr uint32_t kProgEn = 1U << 0U;
inline constexpr uint32_t kOtpRef = 1U << 2U;
inline constexpr uint32_t kProgOtp = 1U << 3U;
inline constexpr uint32_t kProgVer = 1U << 6U;
// DIA bits (Figure 35) and its value once the loops have settled at 3.3 V (Figure 32: 0xX3C2).
inline constexpr uint32_t kDiaVdd5V = 1U << 0U;
inline constexpr uint32_t kDiaLoopsFinished = 1U << 1U;
inline constexpr uint32_t kDiaCosOffDone = 1U << 6U;
inline constexpr uint32_t kDiaSinOffDone = 1U << 7U;
inline constexpr uint32_t kDiaOffCompDone = 1U << 8U;
inline constexpr uint32_t kDiaAgcDone = 1U << 9U;
inline constexpr uint32_t kDiaSpiCountShift = 11U;
inline constexpr uint32_t kDiaSettled = 0x03C2U;
// Fields of the settings registers (Figures 43 to 49).
inline constexpr uint32_t kDisUvwOff = 1U << 0U;
inline constexpr uint32_t kDisAbiOff = 1U << 1U;
inline constexpr uint32_t kDisFilter = 1U << 6U;
inline constexpr uint32_t kZposmBits = 0xFFU;  // zero position bits 13..6
inline constexpr uint32_t kZposlBits = 0x3FU;  // zero position bits 5..0
inline constexpr uint32_t kZposlDiaEn = 0xC0U; // Dia1_en, Dia2_en
inline constexpr uint32_t kS1DiaEn = 0xC0U;    // Dia3_en, Dia4_en
inline constexpr uint32_t kS2Iwidth = 1U << 0U;
inline constexpr uint32_t kS2NoiseSet = 1U << 1U;
inline constexpr uint32_t kS2Dir = 1U << 2U;
inline constexpr uint32_t kS2UvwAbi = 1U << 3U;
inline constexpr uint32_t kS2DaecDis = 1U << 4U;
inline constexpr uint32_t kS2AbiDec = 1U << 5U;
inline constexpr uint32_t kS2DataSelect = 1U << 6U;
inline constexpr uint32_t kS2PwmOn = 1U << 7U;
inline constexpr uint32_t kEccEn = 1U << 7U;
inline constexpr uint32_t kEccKey = 0x7FU;
// Timing (Figure 9): t_pon, power-on to the first valid outcome.
inline constexpr uint32_t kPowerOnUs = 10'000;

/// CRC-8 of a 16-bit word (Figure 31): polynomial 0x1D, initial value 0xC4, final XOR 0xFF, not
/// reflected. Written out bit by bit here, not taken from the driver.
constexpr uint8_t Crc8(uint16_t word) noexcept {
  uint32_t crc = 0xC4U;
  for (uint32_t bit = 16U; bit > 0U; --bit) {
    const uint32_t in = (static_cast<uint32_t>(word) >> (bit - 1U)) & 1U;
    const uint32_t top = (crc >> 7U) & 1U;
    crc = (crc << 1U) & 0xFFU;
    if (in != top) {
      crc ^= 0x1DU;
    }
  }
  return static_cast<uint8_t>(crc ^ 0xFFU);
}
} // namespace chip

/// What the MISO and MOSI lines do (handbook KB-CPP-116).
enum class LineMode : uint8_t {
  Normal = 0, ///< The chip answers.
  StuckLow,   ///< MISO reads all zeros.
  FloatHigh,  ///< MISO floats and a pull-up reads all ones (no chip, or not powered).
  Echo,       ///< MISO is looped back to MOSI: every frame reads back what was sent.
  Deaf,       ///< The chip drives MISO but MOSI does not reach it: it sees all zeros.
};

/// The chip.
class As5047uModel {
public:
  // --- Physical inputs the tests set ---
  uint16_t raw_angle = 0;    ///< Magnet angle before direction and zero, counts.
  uint16_t daec_offset = 0;  ///< What DAEC adds to the angle (ANGLECOM - ANGLEUNC), counts.
  uint16_t velocity = 0;     ///< VEL register content (14-bit two's complement).
  uint16_t magnitude = 4000; ///< MAG.
  uint16_t agc = 128;        ///< AGC.
  /// DIA once the loops have settled; before t_pon the model reads `dia_before_ready`.
  uint16_t dia = static_cast<uint16_t>(chip::kDiaSettled);
  /// DIA before the loops settle (the datasheet gives no value; some loops done is plausible).
  uint16_t dia_before_ready = static_cast<uint16_t>(chip::kDiaCosOffDone | chip::kDiaSinOffDone);
  uint16_t errfl = 0; ///< ERRFL; the tests may set flags directly.

  // --- Fault injection ---
  LineMode line = LineMode::Normal;       ///< MISO/MOSI line state.
  uint32_t power_on_us = 0;               ///< Virtual time until DIA reports ready; 0: up already.
  bool never_ready = false;               ///< The loops never finish.
  int corrupt_reply_frame = -1;           ///< Break the reply CRC of this frame index; -1 never.
  bool ignore_writes = false;             ///< Writes are acknowledged but not stored.
  uint16_t ignore_write_address = 0xFFFF; ///< Only this register ignores writes (0xFFFF: all).
  bool burn_never_finishes = false;       ///< PROG never reads "done".
  bool guard_band_fails = false;          ///< OTPREF reloads a value differing from the burn.
  int fault_on_read_of = -1; ///< Raise ERRFL bits `fault_bits` when this address is read.
  uint16_t fault_bits = 0;   ///< The ERRFL bits raised by `fault_on_read_of`.
  /// ERRFL bits that come back right after each ERRFL read: a condition that persists, such as a
  /// magnet out of range.
  uint16_t persistent_errfl = 0;
  /// The chip sets the error bit for warnings too (the other reading of Figures 22 and 33).
  bool warnings_set_error_bit = false;

  // --- Observations ---
  int frames = 0;                             ///< Frames received.
  int crc_errors_seen = 0;                    ///< MOSI frames with a wrong CRC.
  int dia_reads = 0;                          ///< Read commands of DIA.
  int errfl_reads = 0;                        ///< Read commands of ERRFL.
  bool otp_burned = false;                    ///< A burn finished.
  int burn_starts = 0;                        ///< PROGOTP commands that started a burn.
  uint64_t now_us = 0;                        ///< Virtual clock.
  std::array<uint16_t, chip::kNvCount> otp{}; ///< The burned content.
  std::array<uint16_t, chip::kNvCount> nv{};  ///< DISABLE to ECC: the volatile copy.
  uint16_t prog = 0;                          ///< PROG.
  int resets = 0;                             ///< `PowerOnReset()` calls.

  /// Time passes (the driver's DelayUs).
  void Elapse(uint32_t us) noexcept {
    now_us += us;
    if (burn_left_us_ > 0) {
      burn_left_us_ = us >= burn_left_us_ ? 0 : burn_left_us_ - us;
      if (burn_left_us_ == 0 && !burn_never_finishes) {
        otp = nv;
        otp_burned = true;
        prog = static_cast<uint16_t>(chip::kProgEn);
      }
    }
  }

  /// A power-on reset (or a brown-out below the supply minimum) in the middle of a session, as
  /// the datasheet describes it: the soft-written settings are lost and the volatile copy DISABLE
  /// to ECC reloads from the OTP ("Non-Volatile Registers (OTP)": "Soft written register content
  /// will be lost after a hardware reset"; a blank part reloads 0x0000), PROG and any frame in
  /// flight are gone, and the chip starts again: DIA reports the loops finished only after t_pon
  /// (Figure 9), and ERRFL holds OffCompNotFinished (Figure 33, bit 9) until the offset
  /// compensation has finished. That ERRFL holds the flag right after a reset is this model's
  /// reading of Figure 33, not a datasheet statement (docs/limitations.md).
  void PowerOnReset() noexcept {
    nv = otp;
    prog = 0;
    pending_ = 0;
    write_address_ = -1;
    burn_left_us_ = 0;
    errfl = static_cast<uint16_t>(chip::kOffCompNotFinished);
    power_on_us =
        static_cast<uint32_t>(now_us + chip::kPowerOnUs); // the clock stays small in tests
    never_ready = false;
    resets++;
  }

  /// True once the magnetic loops have settled.
  [[nodiscard]] bool Ready() const noexcept {
    return !never_ready && now_us >= power_on_us;
  }

  /// Angle the chip reports at ANGLEUNC: direction first, then the zero position.
  [[nodiscard]] uint16_t AngleUnc() const noexcept {
    const uint32_t s2 = nv[4];
    const uint32_t raw = raw_angle;
    const uint32_t dir = (s2 & chip::kS2Dir) != 0U ? (16384U - raw) & chip::kData : raw;
    const uint32_t zero = ((static_cast<uint32_t>(nv[1]) & chip::kZposmBits) << 6U) |
                          (static_cast<uint32_t>(nv[2]) & chip::kZposlBits);
    return static_cast<uint16_t>((dir - zero) & chip::kData);
  }

  /// One frame: `tx` in, `rx` out, `len` 2, 3 or 4 bytes.
  void Frame(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    const int index = frames++;
    if (line == LineMode::StuckLow || line == LineMode::FloatHigh) {
      for (std::size_t i = 0; i < len; ++i) {
        rx[i] = line == LineMode::StuckLow ? 0x00U : 0xFFU;
      }
      return;
    }
    if (line == LineMode::Echo) {
      for (std::size_t i = 0; i < len; ++i) {
        rx[i] = tx[i];
      }
      return;
    }
    std::array<uint8_t, 4> heard{};
    for (std::size_t i = 0; i < len && i < heard.size(); ++i) {
      heard[i] = line == LineMode::Deaf ? 0x00U : tx[i];
    }
    const std::size_t at = len == 4 ? 1 : 0;
    const auto word = static_cast<uint16_t>((static_cast<uint32_t>(heard[at]) << 8U) |
                                            static_cast<uint32_t>(heard[at + 1]));
    // Reply with what the previous frame asked for, flags as ERRFL is now.
    uint32_t reply = static_cast<uint32_t>(pending_) & chip::kData;
    if ((static_cast<uint32_t>(errfl) & chip::kErrorFlags) != 0U) {
      reply |= chip::kErrorBit;
    }
    if ((static_cast<uint32_t>(errfl) & chip::kWarningFlags) != 0U) {
      reply |= warnings_set_error_bit ? (chip::kWarningBit | chip::kErrorBit) : chip::kWarningBit;
    }
    rx[0] = static_cast<uint8_t>(reply >> 8U);
    rx[1] = static_cast<uint8_t>(reply & 0xFFU);
    if (len >= 3) {
      rx[2] = chip::Crc8(static_cast<uint16_t>(reply));
      if (index == corrupt_reply_frame) {
        rx[2] = static_cast<uint8_t>(rx[2] ^ 0x01U);
      }
    }
    if (len == 4) {
      rx[3] = heard[0];
    }
    if (len >= 3 && heard[at + 2] != chip::Crc8(word)) {
      crc_errors_seen++;
      errfl = static_cast<uint16_t>(errfl | chip::kCrcError);
      write_address_ = -1;
      pending_ = 0;
      return;
    }
    handle(word);
  }

private:
  void handle(uint16_t word) noexcept {
    if (write_address_ >= 0) {
      const auto address = static_cast<uint16_t>(write_address_);
      write_address_ = -1;
      store(address, static_cast<uint16_t>(static_cast<uint32_t>(word) & chip::kData));
      pending_ = load(address);
      return;
    }
    const auto address = static_cast<uint16_t>(static_cast<uint32_t>(word) & chip::kData);
    if ((static_cast<uint32_t>(word) & chip::kRead) != 0U) {
      if (!known(address)) {
        errfl = static_cast<uint16_t>(errfl | chip::kCommandError);
        pending_ = 0;
        return;
      }
      pending_ = load(address);
      if (address == chip::kDia) {
        dia_reads++;
      }
      if (address == chip::kErrfl) {
        errfl_reads++;
        errfl = persistent_errfl;
        if (!Ready()) {
          // The offset compensation is still running: the flag comes straight back (Figure 33).
          errfl = static_cast<uint16_t>(errfl | chip::kOffCompNotFinished);
        }
      }
      if (std::cmp_equal(address, fault_on_read_of)) {
        errfl = static_cast<uint16_t>(errfl | fault_bits);
      }
      return;
    }
    write_address_ = address;
    pending_ = load(address);
  }

  static bool isNv(uint16_t a) noexcept {
    return a >= chip::kDisable && a <= chip::kEcc;
  }

  static bool known(uint16_t a) noexcept {
    return a == chip::kNop || a == chip::kErrfl || a == chip::kProg || a == chip::kDia ||
           (a >= chip::kAgc && a <= chip::kAngleCom) || a == chip::kEccS || isNv(a);
  }

  [[nodiscard]] uint16_t load(uint16_t a) const noexcept {
    if (isNv(a)) {
      return nv[a - chip::kDisable];
    }
    switch (a) {
    case chip::kErrfl:
      return errfl;
    case chip::kProg:
      return prog;
    case chip::kDia:
      return Ready() ? dia : dia_before_ready;
    case chip::kAgc:
      return agc;
    case chip::kVel:
      return velocity;
    case chip::kMag:
      return magnitude;
    case chip::kAngleUnc:
      return AngleUnc();
    case chip::kAngleCom: {
      const uint32_t s2 = nv[4];
      if ((s2 & chip::kS2DataSelect) != 0U) {
        return AngleUnc(); // Data_select: 0x3FFF carries ANGLEUNC
      }
      const bool daec = (s2 & chip::kS2DaecDis) == 0U;
      return static_cast<uint16_t>(
          (static_cast<uint32_t>(AngleUnc()) + (daec ? static_cast<uint32_t>(daec_offset) : 0U)) &
          chip::kData);
    }
    case chip::kEccS: {
      uint32_t sum = 0;
      for (std::size_t i = 0; i + 1 < nv.size(); ++i) {
        sum += nv[i];
      }
      return static_cast<uint16_t>(sum & chip::kEccKey);
    }
    default:
      return 0;
    }
  }

  void store(uint16_t a, uint16_t v) noexcept {
    if (ignore_writes && (ignore_write_address == 0xFFFF || ignore_write_address == a)) {
      return;
    }
    if (isNv(a)) {
      const auto i = static_cast<std::size_t>(a - chip::kDisable);
      nv[i] = static_cast<uint16_t>(static_cast<uint32_t>(v) & chip::kNvBits[i]);
      return;
    }
    if (a == chip::kProg) {
      writeProg(v);
      return;
    }
    if (a == chip::kErrfl || !known(a)) {
      errfl = static_cast<uint16_t>(errfl | chip::kCommandError);
    }
  }

  void writeProg(uint16_t v) noexcept {
    const uint32_t value = v;
    if ((value & chip::kProgOtp) != 0U && (static_cast<uint32_t>(prog) & chip::kProgEn) != 0U) {
      prog = static_cast<uint16_t>(chip::kProgEn | chip::kProgOtp);
      burn_left_us_ = kBurnTimeUs;
      burn_starts++;
      return;
    }
    if ((value & chip::kOtpRef) != 0U && otp_burned) {
      nv = otp;
      if (guard_band_fails) {
        nv[3] = static_cast<uint16_t>(static_cast<uint32_t>(nv[3]) ^ 0x01U);
      }
      return;
    }
    if ((value & chip::kProgVer) != 0U) {
      return; // guard-band level for the next refresh
    }
    prog = v;
  }

  static constexpr uint32_t kBurnTimeUs = 5000;
  uint16_t pending_ = 0;
  int write_address_ = -1;
  uint32_t burn_left_us_ = 0;
};

} // namespace as5047u_test

#include "mock_as5047u_bus.hpp" // IWYU pragma: export

#endif // AS5047U_MOCK_AS5047U_HPP
