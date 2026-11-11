/**
 * @file as5047u_types.hpp
 * @brief Public types of the AS5047U driver: errors, `Result<T>`, configuration, decoded registers.
 * @copyright Copyright (c) 2026 The driver-as5047u authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Everything here is plain data with no platform dependency, so handlers, tests and tools can
 * include it without the driver template. The configuration defaults are the register reset values
 * of a part whose OTP was never burned (datasheet DS000637, "Register Description"), so `Config{}`
 * describes a freshly powered chip; only the SPI frame format is a driver-side choice.
 */
#ifndef AS5047U_TYPES_HPP
#define AS5047U_TYPES_HPP

#include <array>
#include <cstdint>

#include "as5047u_detail.hpp"
#include "as5047u_registers.hpp"
#include "as5047u_version.h"

/** @brief The AS5047U driver: everything it declares lives here; internals in `detail`. */
namespace as5047u {

/**
 * @defgroup as5047u_types Driver types
 * @brief Errors, results, configuration and decoded registers.
 * @{
 */

/**
 * @brief Why a call failed. Every call of the driver returns one of these in its `Result`.
 *
 * Values 0 to 9 are the shared base set; chip-specific values start at 16. The type is
 * `[[nodiscard]]`: a call whose `Error` is dropped does not compile quietly (KB-CPP20-037).
 */
enum class [[nodiscard]] Error : uint8_t {
  // Base set: the same names and values in every driver of the org (standards/driver.md).
  Ok = 0, ///< The call succeeded.
  NotInitialized =
      1, ///< `EnsureInitialized()` has not succeeded yet, or `Deinitialize()` ran since.
  InvalidArgument = 2,  ///< An argument or a `Config` field is out of range; nothing was sent.
  Bus = 3,              ///< A port hook returned `false`.
  Timeout = 4,          ///< The OTP burn did not finish in `kOtpBurnTimeoutMs`.
  DeviceIdMismatch = 5, ///< Something answers at start-up, but DIA has bit 10 set, which reads 0
                        ///< on an AS5047U: not this chip.
  Crc = 6,              ///< A reply's CRC was wrong, or the chip saw a wrong CRC (ERRFL.CRC_error).
  Busy = 7,             ///< Not returned by this driver (the chip has no busy state).
  DeviceFault = 8,      ///< ERRFL reports a fault of the measurement path (P2RAM, watchdog, CORDIC,
                        ///< offset compensation): the value is not a measurement.
  Unsupported = 9,      ///< Not returned by this driver.
  // Chip-specific values start at 16; each one is returned by some call (tests/test_errors.cpp).
  Framing = 16,       ///< The chip saw an SPI framing error (ERRFL.Framing_error).
  Command = 17,       ///< The chip saw an invalid command (ERRFL.Command_error).
  WriteMismatch = 18, ///< A register read back different from what was written.
  OtpVerify = 19,     ///< The OTP burn finished, but the guard-band read-back differs: do not use
                      ///< the part (datasheet OTP procedure, step 19).
  NoDevice = 20,      ///< A reply that a stuck, floating or looped-back line produces: all zeros
                      ///< or all ones with a wrong CRC, an ERRFL with its reserved bit set, an
                      ///< error bit with no error in ERRFL, or a DIA that stayed 0 (KB-CPP-116).
  NotReady = 21,      ///< The chip answers, but DIA did not report every loop finished within
                      ///< `Config::startup_timeout_ms` (KB-CPP-124).
  MagnetWarning = 22, ///< The reply carried the warning bit and ERRFL holds an AGC or
                      ///< magnetic-half warning: the field is out of range and the angle is not a
                      ///< measurement (KB-CPP-127).
  DeviceReset = 23,   ///< The chip was reset, or a reset could not be ruled out: after
                      ///< OffCompNotFinished or WDTST in ERRFL, or in `CheckForReset()`, the
                      ///< settings registers did not hold what the driver wrote. The driver is
                      ///< uninitialised: call `EnsureInitialized()` again.
};

/**
 * @brief The name of an error, for logs.
 * @param error The error.
 * @return A static string such as `"Crc"`; `"Unknown"` for a value that is not an enumerator.
 */
constexpr const char* ToString(Error error) noexcept {
  switch (error) {
  case Error::Ok:
    return "Ok";
  case Error::NotInitialized:
    return "NotInitialized";
  case Error::InvalidArgument:
    return "InvalidArgument";
  case Error::Bus:
    return "Bus";
  case Error::Timeout:
    return "Timeout";
  case Error::DeviceIdMismatch:
    return "DeviceIdMismatch";
  case Error::Crc:
    return "Crc";
  case Error::Busy:
    return "Busy";
  case Error::DeviceFault:
    return "DeviceFault";
  case Error::Unsupported:
    return "Unsupported";
  case Error::Framing:
    return "Framing";
  case Error::Command:
    return "Command";
  case Error::WriteMismatch:
    return "WriteMismatch";
  case Error::OtpVerify:
    return "OtpVerify";
  case Error::NoDevice:
    return "NoDevice";
  case Error::NotReady:
    return "NotReady";
  case Error::MagnetWarning:
    return "MagnetWarning";
  case Error::DeviceReset:
    return "DeviceReset";
  default:
    break;
  }
  return "Unknown";
}

/**
 * @brief The value of a call, or why there is none.
 *
 * Check `ok()` (or the `bool` conversion) before reading `value`; on failure `value` is `T{}`.
 *
 * @tparam T The value type. `Result<void>` carries only the error.
 */
template <typename T>
struct [[nodiscard]] Result {
  T value{};              ///< The value; `T{}` when the call failed.
  Error error{Error::Ok}; ///< `Error::Ok` on success.

  /** @return true when the call succeeded. */
  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == Error::Ok;
  }
  /** @return true when the call succeeded. */
  constexpr explicit operator bool() const noexcept {
    return ok();
  }

  /**
   * @brief A successful result.
   * @param v The value.
   * @return `{v, Error::Ok}`.
   */
  static constexpr Result Success(T v) noexcept {
    return {v, Error::Ok};
  }
  /**
   * @brief A failed result.
   * @param e Why it failed; never `Error::Ok`.
   * @return `{T{}, e}`.
   */
  static constexpr Result Failure(Error e) noexcept {
    return {T{}, e};
  }
};

/** @brief The result of a call that returns no value. */
template <>
struct [[nodiscard]] Result<void> {
  Error error{Error::Ok}; ///< `Error::Ok` on success.

  /** @return true when the call succeeded. */
  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == Error::Ok;
  }
  /** @return true when the call succeeded. */
  constexpr explicit operator bool() const noexcept {
    return ok();
  }

  /** @return A successful result. */
  static constexpr Result Success() noexcept {
    return {Error::Ok};
  }
  /**
   * @brief A failed result.
   * @param e Why it failed; never `Error::Ok`.
   * @return `{e}`.
   */
  static constexpr Result Failure(Error e) noexcept {
    return {e};
  }
};

/** @brief Severity passed to the optional `Log()` hook. */
enum class LogLevel : uint8_t {
  Error = 0, ///< A call failed.
  Warning,   ///< Something unexpected that the driver recovered from.
  Info,      ///< Lifecycle events: initialised, OTP burned.
  Debug,     ///< Detail for bring-up.
};

/**
 * @brief SPI frame length. The chip recognises the length of each frame, so the driver may mix
 * them.
 *
 * Writes always use a CRC-protected frame: with `Bits16` the driver sends writes as 24-bit frames
 * (the datasheet allows 16-bit frames for reads only).
 */
enum class FrameFormat : uint8_t {
  Bits16 = 0, ///< 16-bit frames, no CRC: the fastest reads, but a corrupted reply goes unnoticed.
  Bits24 = 1, ///< 24-bit frames with CRC-8 in both directions. Default.
  Bits32 = 2, ///< 32-bit frames: a pad byte, then a 24-bit frame (daisy chains).
};

namespace detail {
/// Last enumerator of `FrameFormat`, for `IsEnumerator()`.
template <>
struct EnumLast<FrameFormat> {
  static constexpr FrameFormat kValue = FrameFormat::Bits32;
};
} // namespace detail

/** @brief Direction in which the angle increases (SETTINGS2.DIR). */
enum class Direction : uint8_t {
  Clockwise = 0,        ///< The datasheet's default direction, seen from above the magnet.
  CounterClockwise = 1, ///< Inverted (DIR = 1).
};

/** @brief Which incremental interface the A/B/I-U/V/W pins carry (SETTINGS2.UVW_ABI). */
enum class IncrementalOutput : uint8_t {
  Abi = 0, ///< A, B and index pulses; PWM, when on, goes on pin W. Reset value.
  Uvw = 1, ///< U, V and W commutation signals; PWM, when on, goes on pin I.
};

/** @brief Length of the ABI index pulse (SETTINGS2.IWIDTH). */
enum class IndexWidth : uint8_t {
  ThreeLsb = 0, ///< 3 LSB. Reset value.
  OneLsb = 1,   ///< 1 LSB.
};

namespace detail {
/// Last enumerator of `Direction`.
template <>
struct EnumLast<Direction> {
  static constexpr Direction kValue = Direction::CounterClockwise;
};
/// Last enumerator of `IncrementalOutput`.
template <>
struct EnumLast<IncrementalOutput> {
  static constexpr IncrementalOutput kValue = IncrementalOutput::Uvw;
};
/// Last enumerator of `IndexWidth`.
template <>
struct EnumLast<IndexWidth> {
  static constexpr IndexWidth kValue = IndexWidth::OneLsb;
};
} // namespace detail

/**
 * @brief Whether a run-time write reads the register back, chosen per call.
 *
 * A write is the two frames the datasheet needs (command and data, CRC-protected, the reply's
 * error bit checked). `ReadBack` adds one frame pair per register and fails with `WriteMismatch`
 * when the chip holds something else: it catches a write the chip lost, or a read-only bit.
 * `EnsureInitialized()` and `ProgramOtp()` read their writes back whatever is passed elsewhere.
 */
enum class Verify : uint8_t {
  None = 0,     ///< Command and data frames only. The default: nothing extra on a timed path.
  ReadBack = 1, ///< Read each written register back and compare.
};

namespace detail {
/// Last enumerator of `Verify`.
template <>
struct EnumLast<Verify> {
  static constexpr Verify kValue = Verify::ReadBack;
};
} // namespace detail

/// Longest wait from power-on to the first valid angle, t_pon (datasheet "Timing
/// Characteristics", Figure 9): the default start-up deadline.
inline constexpr uint32_t kPowerOnTimeMs = 10;
/// Largest `Config::startup_timeout_ms` that `Validate()` accepts.
inline constexpr uint32_t kStartupTimeoutMaxMs = 1000;

/**
 * @brief Everything `EnsureInitialized()` and `ApplyConfig()` write to the chip.
 *
 * The register defaults are the reset values of an unprogrammed part, so `Config{}` changes nothing
 * on such a chip. Codes are the raw register field values; the datasheet section of each field
 * gives the table.
 */
struct Config {
  /// Driver side: the frame length of every read (writes are always CRC-protected).
  /// **`Bits16` reads carry no CRC**: a corrupted reply goes unnoticed, and MISO stuck low (or a
  /// chip that hears MOSI as all zeros) reads as angle 0 with no flag bit. Keep the default
  /// `Bits24` wherever a stuck line must never read as a position (docs/limitations.md).
  FrameFormat frame{FrameFormat::Bits24};
  /// Driver side: the pad byte of 32-bit frames (the daisy-chain address).
  uint8_t pad_byte{0};
  /// Driver side: write the settings below in `EnsureInitialized()`. False leaves the chip as it
  /// is, for a part whose OTP is burned: `EnsureInitialized()` reads the settings back into
  /// `GetConfig()`, so a later setter changes only the field it names.
  bool write_settings{true};
  /// Driver side: how long `EnsureInitialized()` polls DIA, once per millisecond, for every loop
  /// to report finished before it fails with `NotReady` (or `NoDevice` when DIA never read
  /// anything but 0). 0 to `kStartupTimeoutMaxMs`; 0 checks once and never waits. The default
  /// t_pon covers a start made the moment the supply comes up (handbook KB-CPP-124).
  uint32_t startup_timeout_ms{kPowerOnTimeMs};

  Direction direction{Direction::Clockwise}; ///< SETTINGS2.DIR.
  uint16_t zero_position{0};                 ///< ZPOSM/ZPOSL, counts 0 to 16383.
  bool dynamic_compensation{true};           ///< DAEC on (SETTINGS2.DAECDIS = 0).
  bool adaptive_filter{true};                ///< Velocity filter on (DISABLE bit 6 = 0).
  uint8_t filter_k_min_code{0};              ///< SETTINGS1.K_min code, 0 to 7.
  uint8_t filter_k_max_code{0};              ///< SETTINGS1.K_max code, 0 to 7.
  IncrementalOutput incremental{IncrementalOutput::Abi}; ///< SETTINGS2.UVW_ABI.
  bool abi_enable{true};                        ///< A/B/I pins driven (DISABLE.ABI_off = 0).
  bool uvw_enable{true};                        ///< U/V/W pins driven (DISABLE.UVW_off = 0).
  bool pwm_enable{false};                       ///< SETTINGS2.PWMon.
  uint8_t abi_resolution_code{0};               ///< SETTINGS3.ABIRES, 0 to 7.
  bool abi_decimal{false};                      ///< SETTINGS2.ABI_DEC.
  IndexWidth index_width{IndexWidth::ThreeLsb}; ///< SETTINGS2.IWIDTH.
  uint8_t uvw_pole_pairs{1};                    ///< SETTINGS3.UVWPP + 1, 1 to 7.
  uint8_t hysteresis_code{0};                   ///< SETTINGS3.HYS, 0 to 3.
  bool high_temperature_noise{false};           ///< SETTINGS2.NOISESET (150 °C at 3.3 V).
  /// SETTINGS2.Data_select: address 0x3FFF (and the PWM output) carry the angle without DAEC.
  bool uncompensated_at_angle_register{false};

  /**
   * @brief Field by field; every member is an integer, a bool or an enumeration, so equality is
   * exact (KB-CPP20-033). A hidden friend, so both operands are treated alike.
   * @param lhs One configuration.
   * @param rhs The other.
   * @return true when every field is equal.
   */
  [[nodiscard]] friend constexpr bool operator==(const Config& lhs,
                                                 const Config& rhs) noexcept = default;
};

/// Angle counts per revolution (14 bits).
inline constexpr uint16_t kCountsPerRevolution = 16384;
/// Highest angle count.
inline constexpr uint16_t kMaxAngleCount = kCountsPerRevolution - 1U;
/// Velocity resolution in degrees per second per LSB (datasheet, VEL register).
inline constexpr float kVelocityDegPerSecPerLsb = 24.141F;
/// Pi, as a float (C++20's `std::numbers` is not on every embedded toolchain).
// NOLINTNEXTLINE(modernize-use-std-numbers): <numbers> is outside the target subset (ADR-0017)
inline constexpr float kPi = 3.14159265358979323846F;
/// Longest wait for an OTP burn to finish, in milliseconds (polled once per millisecond). The
/// datasheet gives no burn time; this bound is the driver's choice, not verified on hardware.
inline constexpr uint32_t kOtpBurnTimeoutMs = 100;
/**
 * @brief The typed confirmation `ProgramOtp()` requires: burning the OTP cannot be undone, so the
 * call names its intent and a stray integer cannot stand in for it (KB-CPP-045, KB-CPP20-030).
 */
struct OtpBurnConfirm {
  uint32_t code{0}; ///< Must be `kOtpConfirm.code`.
};
/// The confirmation to pass to `ProgramOtp()`.
inline constexpr OtpBurnConfirm kOtpConfirm{0x0B7B'0B7BU};
/// The highest field code for 3-bit fields (`filter_k_*_code`, `abi_resolution_code`).
inline constexpr uint8_t kMax3BitCode = 7;
/// The highest hysteresis code (2-bit field).
inline constexpr uint8_t kMaxHysteresisCode = 3;
/// Highest UVW pole-pair count.
inline constexpr uint8_t kMaxUvwPolePairs = 7;

/** @brief ERRFL, decoded. Reading ERRFL clears it on the chip. */
struct ErrorFlags {
  bool agc_warning{false};     ///< AGC at its minimum or maximum: magnet too far or too close.
  bool mag_half{false};        ///< Field at half the regulated value.
  bool p2ram_warning{false};   ///< ECC corrected one bit in the P2RAM.
  bool p2ram_error{false};     ///< ECC found an uncorrectable P2RAM error.
  bool framing{false};         ///< SPI framing error.
  bool command{false};         ///< Invalid SPI command.
  bool crc{false};             ///< Wrong CRC on a MOSI frame.
  bool watchdog{false};        ///< Internal oscillator or watchdog fault.
  bool offset_comp{false};     ///< Offset compensation not finished.
  bool cordic_overflow{false}; ///< CORDIC overflow.
  /**
   * @brief Not an ERRFL bit: in `GetLatchedErrorFlags()` only, true when a read of ERRFL failed
   * after its command was sent (a reply CRC error, a failed transfer, a reserved bit set). ERRFL
   * clears when it is read, so the flags that read took from the chip may be lost.
   */
  bool flags_lost{false};
  uint16_t raw{0}; ///< The register value.
};

/** @brief DIA, decoded: the state of the chip's internal loops. */
struct Diagnostics {
  bool vdd_5v{false};           ///< VDD_mode: the chip runs from 5 V (else 3.3 V).
  bool loops_finished{false};   ///< Every magnetic loop has settled.
  bool cordic_overflow{false};  ///< CORDIC overflow.
  bool agc_low{false};          ///< AGC at its low limit (field too strong).
  bool agc_high{false};         ///< AGC at its high limit (field too weak).
  bool mag_half{false};         ///< Field at half the regulated value.
  bool offset_comp_done{false}; ///< Offset compensation finished.
  bool agc_done{false};         ///< AGC loop finished.
  uint8_t spi_frame_count{0};   ///< SPI_cnt: frames since the last read of DIA, modulo 4.
  uint16_t raw{0};              ///< The register value.
};

/// The non-volatile registers DISABLE to ECC, in address order; index with `reg::At(reg::NvIndex)`.
using NonVolatileRegisters = std::array<uint16_t, reg::kNonVolatileCount>;

/** @brief Outcome of `AS5047U::ProgramOtp()`. */
struct OtpReport {
  /// Datasheet step that failed (6 to 19), or 0 when every step succeeded.
  uint8_t failed_step{0};
  /// True from the moment the burn command (step 13) is sent, even when that transfer reports a
  /// failure (the chip may have received it): the fuses may be blown. A part that failed after
  /// this must not be used until verified.
  bool burn_started{false};
  /// DISABLE to ECC as verified before the burn: what was burned.
  NonVolatileRegisters written{};
};

/**
 * @brief Check a configuration without touching the chip.
 *
 * Fails when an enum holds a value that is not an enumerator or a code or count is out of its
 * field's range.
 *
 * @param config The configuration.
 * @return `Error::Ok` or `Error::InvalidArgument`.
 */
constexpr Error Validate(const Config& config) noexcept {
  if (!detail::IsEnumerator(config.frame) || !detail::IsEnumerator(config.direction) ||
      !detail::IsEnumerator(config.incremental) || !detail::IsEnumerator(config.index_width)) {
    return Error::InvalidArgument;
  }
  if ((config.zero_position > kMaxAngleCount) || (config.filter_k_min_code > kMax3BitCode) ||
      (config.filter_k_max_code > kMax3BitCode) || (config.abi_resolution_code > kMax3BitCode) ||
      (config.hysteresis_code > kMaxHysteresisCode) || (config.uvw_pole_pairs == 0U) ||
      (config.uvw_pole_pairs > kMaxUvwPolePairs) ||
      (config.startup_timeout_ms > kStartupTimeoutMaxMs)) {
    return Error::InvalidArgument;
  }
  return Error::Ok;
}

static_assert(Validate(Config{}) == Error::Ok, "the defaults are valid");

/**
 * @brief Convert angle counts to degrees.
 * @param counts 0 to 16383.
 * @return Degrees in [0, 360).
 */
constexpr float CountsToDegrees(uint16_t counts) noexcept {
  return static_cast<float>(static_cast<uint32_t>(counts) & kMaxAngleCount) * (360.0F / 16384.0F);
}

/**
 * @brief Convert angle counts to radians.
 * @param counts 0 to 16383.
 * @return Radians in [0, 2π).
 */
constexpr float CountsToRadians(uint16_t counts) noexcept {
  return static_cast<float>(static_cast<uint32_t>(counts) & kMaxAngleCount) *
         (2.0F * kPi / 16384.0F);
}

/** @return The driver version, e.g. `"0.1.0"`, from the generated `as5047u_version.h`. No I/O. */
constexpr const char* GetDriverVersion() noexcept {
  return AS5047U_VERSION_STRING;
}
/** @return The major version. No I/O. */
constexpr uint32_t GetDriverVersionMajor() noexcept {
  return AS5047U_VERSION_MAJOR;
}
/** @return The minor version. No I/O. */
constexpr uint32_t GetDriverVersionMinor() noexcept {
  return AS5047U_VERSION_MINOR;
}
/** @return The patch version. No I/O. */
constexpr uint32_t GetDriverVersionPatch() noexcept {
  return AS5047U_VERSION_PATCH;
}

/** @} */

} // namespace as5047u

#endif // AS5047U_TYPES_HPP
