/**
 * @file as5047u.hpp
 * @brief AS5047U driver: the one header to include.
 * @copyright Copyright (c) 2026 The driver-as5047u authors. SPDX-License-Identifier: MIT
 *
 * @details
 * `as5047u::AS5047U<Port>` drives one AS5047U 14-bit magnetic rotary position sensor through a
 * platform port you write (see `as5047u_spi_interface.hpp`). The driver has no platform code,
 * allocates nothing, throws nothing, takes no lock and is not thread-safe: give each object one
 * owner, or guard it with one mutex.
 */
#ifndef AS5047U_HPP
#define AS5047U_HPP

#include <cstddef>
#include <cstdint>

#include "as5047u_detail.hpp"
#include "as5047u_registers.hpp"
#include "as5047u_spi_interface.hpp"
#include "as5047u_types.hpp"
#include "as5047u_version.h"

namespace as5047u {

namespace detail {

// Port hooks: a `Declares*` concept only tests the name, the `Has*` concept the exact signature,
// `noexcept` and the result type; the driver rejects a hook that is declared but does not match
// (KB-CPP20-007).

/// The port declares something called `DelayUs`.
template <typename Port>
concept DeclaresDelayUs = requires(Port& port) { port.DelayUs(1U); };

/// The port waits: `void DelayUs(uint32_t us) noexcept`. A narrower parameter would truncate long
/// waits.
template <typename Port>
concept HasDelayUs = requires { ExactHook<Port, void(uint32_t) noexcept>::Take(&Port::DelayUs); };

/// The port declares something called `Log`.
template <typename Port>
concept DeclaresLog = requires(Port& port) { port.Log(LogLevel::Info, ""); };

/// The port takes log lines: `void Log(LogLevel, const char* message) noexcept`.
template <typename Port>
concept HasLog =
    requires { ExactHook<Port, void(LogLevel, const char*) noexcept>::Take(&Port::Log); };

/// Wait through the port. A port without `DelayUs()` fails to compile at the first call that needs
/// a wait (`EnsureInitialized()` polls DIA, `ProgramOtp()` polls the burn): the waits are
/// requirements, never skipped.
template <typename Port>
void DelayUs(Port& port, uint32_t us) noexcept {
  if constexpr (HasDelayUs<Port>) {
    port.DelayUs(us);
  } else {
    static_assert(kAlwaysFalse<Port>,
                  "as5047u: the port must implement `void DelayUs(uint32_t us) noexcept`");
  }
}

/// Log through the port, or nothing when the port has no `Log()`. The driver never formats text.
template <typename Port>
void Log(Port& port, [[maybe_unused]] LogLevel level,
         [[maybe_unused]] const char* message) noexcept {
  if constexpr (HasLog<Port>) {
    port.Log(level, message);
  } else {
    (void)port;
  }
}

/// Sign-extend a 14-bit two's-complement field.
constexpr int16_t SignExtend14(uint16_t raw) noexcept {
  const auto v = static_cast<int32_t>(static_cast<uint32_t>(raw) & reg::kDataMask);
  return static_cast<int16_t>(((static_cast<uint32_t>(v) & 0x2000U) != 0U) ? (v - 0x4000) : v);
}
static_assert((SignExtend14(0x1FFF_u16) == 8191) && (SignExtend14(0x2000_u16) == -8192) &&
              (SignExtend14(0x3FFF_u16) == -1));

/// Decode ERRFL.
constexpr ErrorFlags DecodeErrorFlags(uint16_t raw) noexcept {
  const auto v = static_cast<uint32_t>(raw);
  ErrorFlags f{};
  f.agc_warning = (v & reg::kErrflAgcWarning) != 0U;
  f.mag_half = (v & reg::kErrflMagHalf) != 0U;
  f.p2ram_warning = (v & reg::kErrflP2ramWarning) != 0U;
  f.p2ram_error = (v & reg::kErrflP2ramError) != 0U;
  f.framing = (v & reg::kErrflFraming) != 0U;
  f.command = (v & reg::kErrflCommand) != 0U;
  f.crc = (v & reg::kErrflCrc) != 0U;
  f.watchdog = (v & reg::kErrflWatchdog) != 0U;
  f.offset_comp = (v & reg::kErrflOffComp) != 0U;
  f.cordic_overflow = (v & reg::kErrflCordic) != 0U;
  f.raw = raw;
  return f;
}

/// Decode DIA.
constexpr Diagnostics DecodeDiagnostics(uint16_t raw) noexcept {
  const auto v = static_cast<uint32_t>(raw);
  Diagnostics d{};
  d.vdd_5v = (v & reg::kDiaVdd5V) != 0U;
  d.loops_finished = (v & reg::kDiaLoopsFinished) != 0U;
  d.cordic_overflow = (v & reg::kDiaCordicOverflow) != 0U;
  d.agc_low = (v & reg::kDiaCompLow) != 0U;
  d.agc_high = (v & reg::kDiaCompHigh) != 0U;
  d.mag_half = (v & reg::kDiaMagHalf) != 0U;
  d.offset_comp_done = (v & reg::kDiaOffsetCompDone) != 0U;
  d.agc_done = (v & reg::kDiaAgcDone) != 0U;
  d.spi_frame_count = static_cast<uint8_t>((v & reg::kDiaSpiCountMask) >> reg::kDiaSpiCountShift);
  d.raw = raw;
  return d;
}

/// The error that an ERRFL value means for the access it reports on; `Ok` for warnings only.
[[nodiscard]] constexpr Error ClassifyErrorFlags(uint16_t errfl) noexcept {
  const auto v = static_cast<uint32_t>(errfl);
  if ((v & reg::kErrflCrc) != 0U) {
    return Error::Crc;
  }
  if ((v & reg::kErrflFraming) != 0U) {
    return Error::Framing;
  }
  if ((v & reg::kErrflCommand) != 0U) {
    return Error::Command;
  }
  if ((v & reg::kErrflDeviceFault) != 0U) {
    return Error::DeviceFault;
  }
  return Error::Ok;
}

/// True for an enumerator of `Verify`; a cast-in value is refused with `InvalidArgument`.
constexpr bool IsVerify(Verify v) noexcept {
  return IsEnumerator(v);
}

// Settings encoder and decoder: free constexpr functions, emitted once whatever the port type, and
// proved below with static_assert (KB-CPP20-009, -012). Each register is built in uint32_t and
// narrowed once (KB-CPP20-021, -022).

/// One bit of a register: `bit` when `on`, else 0. Takes the `uint16_t` register constants as
/// they are and widens once, to the `uint32_t` the register is built in.
constexpr uint32_t BitIf(bool on, uint16_t bit) noexcept {
  return on ? static_cast<uint32_t>(bit) : 0U;
}

/// DISABLE to SETTINGS3 for `c`, keeping the DIA enable bits of ZPOSL and SETTINGS1 from `now`
/// (Figures 43 to 48). ECC is copied from `now`: only the OTP procedure writes it.
constexpr NonVolatileRegisters EncodeSettings(const Config& c,
                                              const NonVolatileRegisters& now) noexcept {
  using reg::At;
  using reg::NvIndex;
  NonVolatileRegisters r = now;
  const auto zero = static_cast<uint32_t>(c.zero_position);
  r[At(NvIndex::Disable)] = static_cast<uint16_t>(
      BitIf(!c.uvw_enable, reg::kDisableUvwOff) | BitIf(!c.abi_enable, reg::kDisableAbiOff) |
      BitIf(!c.adaptive_filter, reg::kDisableFilterOff));
  r[At(NvIndex::Zposm)] = static_cast<uint16_t>((zero >> reg::kZeroHighShift) & reg::kZposmMask);
  r[At(NvIndex::Zposl)] =
      static_cast<uint16_t>((static_cast<uint32_t>(now[At(NvIndex::Zposl)]) & reg::kZposlDiaBits) |
                            (zero & reg::kZposlMask));
  r[At(NvIndex::Settings1)] = static_cast<uint16_t>(
      (static_cast<uint32_t>(now[At(NvIndex::Settings1)]) & reg::kS1DiaBits) |
      ((static_cast<uint32_t>(c.filter_k_min_code) << reg::kS1KminShift) & reg::kS1KminMask) |
      (static_cast<uint32_t>(c.filter_k_max_code) & reg::kS1KmaxMask));
  r[At(NvIndex::Settings2)] = static_cast<uint16_t>(
      BitIf(c.index_width == IndexWidth::OneLsb, reg::kS2IndexWidth1) |
      BitIf(c.high_temperature_noise, reg::kS2NoiseSet) |
      BitIf(c.direction == Direction::CounterClockwise, reg::kS2Dir) |
      BitIf(c.incremental == IncrementalOutput::Uvw, reg::kS2UvwAbi) |
      BitIf(!c.dynamic_compensation, reg::kS2DaecDis) | BitIf(c.abi_decimal, reg::kS2AbiDecimal) |
      BitIf(c.pwm_enable, reg::kS2PwmOn) |
      BitIf(c.uncompensated_at_angle_register, reg::kS2DataSelect));
  r[At(NvIndex::Settings3)] = static_cast<uint16_t>(
      ((static_cast<uint32_t>(c.uvw_pole_pairs) - 1U) & reg::kS3UvwppMask) |
      ((static_cast<uint32_t>(c.hysteresis_code) << reg::kS3HysShift) & reg::kS3HysMask) |
      ((static_cast<uint32_t>(c.abi_resolution_code) << reg::kS3AbiresShift) & reg::kS3AbiresMask));
  return r;
}

/// The inverse of `EncodeSettings()`: the settings a chip holds. The driver-side fields (frame,
/// pad byte, write_settings, startup deadline) come from `base`.
constexpr Config DecodeSettings(const NonVolatileRegisters& r, const Config& base) noexcept {
  using reg::At;
  using reg::NvIndex;
  Config c = base;
  const auto disable = static_cast<uint32_t>(r[At(NvIndex::Disable)]);
  c.uvw_enable = (disable & reg::kDisableUvwOff) == 0U;
  c.abi_enable = (disable & reg::kDisableAbiOff) == 0U;
  c.adaptive_filter = (disable & reg::kDisableFilterOff) == 0U;
  c.zero_position = static_cast<uint16_t>(
      ((static_cast<uint32_t>(r[At(NvIndex::Zposm)]) & reg::kZposmMask) << reg::kZeroHighShift) |
      (static_cast<uint32_t>(r[At(NvIndex::Zposl)]) & reg::kZposlMask));
  const auto s1 = static_cast<uint32_t>(r[At(NvIndex::Settings1)]);
  c.filter_k_min_code = static_cast<uint8_t>((s1 & reg::kS1KminMask) >> reg::kS1KminShift);
  c.filter_k_max_code = static_cast<uint8_t>(s1 & reg::kS1KmaxMask);
  const auto s2 = static_cast<uint32_t>(r[At(NvIndex::Settings2)]);
  c.index_width = ((s2 & reg::kS2IndexWidth1) != 0U) ? IndexWidth::OneLsb : IndexWidth::ThreeLsb;
  c.high_temperature_noise = (s2 & reg::kS2NoiseSet) != 0U;
  c.direction = ((s2 & reg::kS2Dir) != 0U) ? Direction::CounterClockwise : Direction::Clockwise;
  c.incremental = ((s2 & reg::kS2UvwAbi) != 0U) ? IncrementalOutput::Uvw : IncrementalOutput::Abi;
  c.dynamic_compensation = (s2 & reg::kS2DaecDis) == 0U;
  c.abi_decimal = (s2 & reg::kS2AbiDecimal) != 0U;
  c.uncompensated_at_angle_register = (s2 & reg::kS2DataSelect) != 0U;
  c.pwm_enable = (s2 & reg::kS2PwmOn) != 0U;
  const auto s3 = static_cast<uint32_t>(r[At(NvIndex::Settings3)]);
  const uint32_t pole_pairs = (s3 & reg::kS3UvwppMask) + 1U;
  // UVWPP code 7 also means 7 pole pairs (Figure 48).
  c.uvw_pole_pairs =
      static_cast<uint8_t>((pole_pairs > kMaxUvwPolePairs) ? kMaxUvwPolePairs : pole_pairs);
  c.hysteresis_code = static_cast<uint8_t>((s3 & reg::kS3HysMask) >> reg::kS3HysShift);
  c.abi_resolution_code = static_cast<uint8_t>((s3 & reg::kS3AbiresMask) >> reg::kS3AbiresShift);
  return c;
}

/// A configuration that sets every chip field away from its reset value, for the round trip below.
constexpr Config RoundTripSample() noexcept {
  Config c{};
  c.direction = Direction::CounterClockwise;
  c.zero_position = 0x1234U;
  c.dynamic_compensation = false;
  c.adaptive_filter = false;
  c.filter_k_min_code = 5U;
  c.filter_k_max_code = 2U;
  c.incremental = IncrementalOutput::Uvw;
  c.abi_enable = false;
  c.uvw_enable = false;
  c.pwm_enable = true;
  c.abi_resolution_code = 4U;
  c.abi_decimal = true;
  c.index_width = IndexWidth::OneLsb;
  c.uvw_pole_pairs = 7U;
  c.hysteresis_code = 3U;
  c.high_temperature_noise = true;
  c.uncompensated_at_angle_register = true;
  return c;
}

// The reset configuration encodes to the reset values of an unprogrammed part (all 0), and every
// field decodes back to what was encoded, also with the DIA enable bits set.
static_assert(EncodeSettings(Config{}, NonVolatileRegisters{}) == NonVolatileRegisters{});
static_assert(DecodeSettings(EncodeSettings(Config{}, {}), Config{}) == Config{});
static_assert(DecodeSettings(EncodeSettings(RoundTripSample(), {}), Config{}) == RoundTripSample());
static_assert(DecodeSettings(EncodeSettings(RoundTripSample(),
                                            NonVolatileRegisters{0, 0, 0xC0U, 0xC0U, 0, 0, 0}),
                             Config{}) == RoundTripSample());

} // namespace detail

/**
 * @defgroup as5047u_driver AS5047U driver
 * @brief The driver class.
 * @{
 */

// clang-format off: the hook table below must keep one row per line.
/**
 * @brief Driver for one AS5047U on a platform port.
 *
 * Lifecycle: construct with the configuration (no I/O), then `EnsureInitialized()`, the one way
 * to start (bus up, presence check, error flags cleared, settings written and read back), use,
 * optionally `Deinitialize()`. Every call that touches the chip returns `Error::NotInitialized`
 * until `EnsureInitialized()` has succeeded; no call starts the chip as a side effect, so a read
 * is always the frames it needs and nothing more.
 *
 * Every read checks the two flag bits of the reply. A read whose reply has neither set costs
 * exactly its two frames. When the error bit is set the driver reads ERRFL (which clears it on the
 * chip, so the driver ORs it into `GetLatchedErrorFlags()`) and fails the call with `Crc`,
 * `Framing`, `Command` or `DeviceFault`. When an angle or velocity reply carries the warning bit,
 * the driver reads ERRFL the same way and refuses the value with `MagnetWarning` if the field is
 * out of range (AGC at a limit, field at half); a P2RAM single-bit warning (corrected by the ECC)
 * does not refuse it. Other reads (AGC, magnitude, DIA, settings) return their value under a
 * warning and leave ERRFL on the chip for `ReadErrorFlags()`.
 *
 * Reset detection: a power-on reset or brown-out of the chip reloads its settings from the OTP and
 * loses every soft write ("Non-Volatile Registers (OTP)"). When any ERRFL read after start-up holds
 * OffCompNotFinished or WDTST (the offset compensation restarts at power-up, Figure 33), the driver
 * reads DISABLE to ECC (14 frames) and compares them with what it last wrote or read there. A
 * read of ERRFL that failed (and may have taken that flag) costs the same compare. On a
 * difference, or when that read cannot be completed after a flag, the call fails with `DeviceReset` and the
 * driver is uninitialised: call `EnsureInitialized()` again, which writes the configuration anew.
 * The datasheet does not promise that flag after a reset, so call `CheckForReset()` periodically
 * where a reset must not go unnoticed (docs/limitations.md).
 *
 * Port hooks beyond the required bus hooks, found at compile time:
 *
 * | Hook | Used for | Without it |
 * | --- | --- | --- |
 * | `void DelayUs(uint32_t us) noexcept` | 1 ms between polls of DIA at start-up and of the OTP burn | Compile error: the waits are required |
 * | `void Log(LogLevel, const char* message) noexcept` | One line per lifecycle event or failure | Nothing is logged |
 *
 * A hook declared with the wrong signature (for example `DelayUs(uint16_t)`) is a compile error.
 *
 * @tparam Port Your class derived from `SpiInterface<Port>`.
 */
// clang-format on
template <SpiPort Port>
class AS5047U {
  static_assert(!detail::DeclaresDelayUs<Port> || detail::HasDelayUs<Port>,
                "as5047u: the DelayUs hook must be `void DelayUs(uint32_t us) noexcept`, so that "
                "no wait is truncated");
  static_assert(!detail::DeclaresLog<Port> || detail::HasLog<Port>,
                "as5047u: the optional Log hook must be `void Log(LogLevel, const char* message) "
                "noexcept`");

public:
  /**
   * @brief Bind the driver to a port and store the configuration. No I/O, cannot fail.
   * @param port The port; it must outlive the driver.
   * @param config What `EnsureInitialized()` will apply; checked there, before any I/O.
   */
  explicit AS5047U(Port& port, const Config& config = {}) noexcept : bus_(port), config_(config) {}

  /** @brief One driver object per chip. */
  AS5047U(const AS5047U&) = delete;
  /** @brief One driver object per chip. */
  AS5047U& operator=(const AS5047U&) = delete;

  /// @name Lifecycle
  /// @{

  /**
   * @brief Start the chip, once: the only way to initialise the driver.
   *
   * The first call brings the bus up and waits for the chip: it reads DIA, once per millisecond
   * for at most `Config::startup_timeout_ms`, until every magnetic loop reports finished, and
   * accepts that only when the next DIA read says the same (a ready status a line could fake
   * counts only when a second read confirms it, KB-CPP-116, KB-CPP-124). Then it reads ERRFL,
   * which clears what the power-up left there (the value goes into `GetLatchedErrorFlags()`, which
   * the call empties first), and applies the stored configuration: the settings registers read
   * (to keep their DIA bits), then every setting written and read back (start-up always
   * verifies), or, with `write_settings` false (an OTP-burned part), the chip's settings read into
   * `GetConfig()`. A chip that is already up costs 6 frames before the settings; one that is not
   * yet ready adds 2 frames and 1 ms per poll. After it succeeds, later calls return `Ok` at once
   * with no I/O. After it fails, the driver stays uninitialised and the next call starts again
   * from the beginning.
   *
   * @return `Ok`, `InvalidArgument` (the stored configuration fails `Validate()`; no I/O), `Bus`,
   *         `NoDevice` (only line patterns until the deadline), `NotReady` (the chip answers but
   *         its loops did not finish by the deadline), `DeviceIdMismatch` (a DIA with a bit set
   *         that reads 0 on an AS5047U), `Crc`, `Framing`, `Command`, `DeviceFault` or
   *         `WriteMismatch`. After a `DeviceReset` from any call, this call starts the chip again
   *         and writes the stored configuration (`GetConfig()` still holds it).
   */
  Result<void> EnsureInitialized() noexcept;

  /** @return true after a successful `EnsureInitialized()` and before `Deinitialize()`. No I/O. */
  [[nodiscard]] bool IsInitialized() const noexcept {
    return initialized_;
  }

  /**
   * @brief Mark the driver uninitialised; the next `EnsureInitialized()` starts the chip again
   * (the recovery after a fault). No I/O: the AS5047U has no low-power mode to enter.
   * @return `Ok`.
   */
  Result<void> Deinitialize() noexcept;

  /// @}
  /// @name Configuration
  /// @{

  /**
   * @brief Change the configuration.
   *
   * Running: the settings registers are read (to keep their DIA bits), then every setting is
   * written, and read back with `Verify::ReadBack`. Not started yet: `config` is only stored, for
   * the first `EnsureInitialized()`, with no I/O; start-up reads its writes back anyway.
   *
   * When it fails after a write may have reached the chip, the write may have taken effect, so
   * the driver neither retries it nor keeps the old values: it reads DISABLE to ECC back (14
   * frames) and `GetConfig()` then holds what the chip holds, field by field (the driver-side
   * fields keep their old values). When that read fails too, the driver is uninitialised and
   * `GetConfig()` holds the old configuration again, which `EnsureInitialized()` writes.
   *
   * @param config Checked with `Validate()` before any I/O.
   * @param verify `Verify::ReadBack` reads each write back (two more frames per register).
   * @return `Ok`, `InvalidArgument` (also for a `verify` that is not an enumerator), `Bus`, `Crc`,
   *         `Framing`, `Command`, `DeviceFault`, `WriteMismatch` (with `Verify::ReadBack`) or
   *         `DeviceReset` (as for `ReadAngleCounts()`).
   */
  Result<void> ApplyConfig(const Config& config, Verify verify = Verify::None) noexcept;

  /**
   * @return The configuration last applied, stored or read from the chip: after a failed
   * `ApplyConfig()` or setter, what the read-back found. No I/O.
   */
  [[nodiscard]] const Config& GetConfig() const& noexcept {
    return config_;
  }
  /** @brief Not on a temporary driver: the reference would dangle (KB-CPP20-044). */
  void GetConfig() const&& = delete;

  /**
   * @brief Set the zero position (ZPOSM, ZPOSL).
   * @param counts 0 to 16383: the uncorrected angle that will read as 0.
   * @param verify As for `ApplyConfig()`.
   * @return As `ApplyConfig()`: before `EnsureInitialized()` the value is only stored.
   */
  Result<void> SetZeroPosition(uint16_t counts, Verify verify = Verify::None) noexcept;

  /**
   * @brief Make the current position read as 0.
   *
   * Writes a zero of 0, reads the angle at 0x3FFF (`ReadAngleCounts()`, datasheet OTP procedure
   * step 4) and writes it as the new zero. When the read
   * fails, the previous zero is written back before the error is returned.
   *
   * @param verify Passed to each `SetZeroPosition()`.
   * @return The new zero position in counts; `NotInitialized` before `EnsureInitialized()`, or the
   *         errors of `ReadAngleCounts()` and `SetZeroPosition()`.
   */
  Result<uint16_t> SetZeroHere(Verify verify = Verify::None) noexcept;

  /**
   * @brief Set the direction in which the angle increases.
   * @param direction The direction.
   * @param verify As for `ApplyConfig()`.
   * @return As `ApplyConfig()`: before `EnsureInitialized()` the value is only stored.
   */
  Result<void> SetDirection(Direction direction, Verify verify = Verify::None) noexcept;

  /// @}
  /// @name Measurement
  /// @{

  /**
   * @brief The angle at address 0x3FFF (with DAEC unless it is off): the hot path.
   *
   * Two frames (the read command, then a NOP whose reply carries the angle) and nothing more when
   * the reply has neither flag bit set. When it has one, two more frames read ERRFL to find out
   * why (latched in `GetLatchedErrorFlags()`).
   *
   * @return 0 to 16383 counts, or `NotInitialized`, `Bus`, `Crc`, `NoDevice`, `Framing`,
   *         `Command`, `DeviceFault`, `MagnetWarning` (the field is out of range: not a
   *         measurement) or `DeviceReset` (ERRFL holds OffCompNotFinished or WDTST and the settings
   *         registers changed; 14 more frames read them, see the class description).
   */
  Result<uint16_t> ReadAngleCounts() noexcept;
  /** @return The angle without dynamic compensation (ANGLEUNC), 0 to 16383 counts; errors and
   * frames as `ReadAngleCounts()`. */
  Result<uint16_t> ReadUncompensatedAngleCounts() noexcept;
  /** @return The angle in degrees, [0, 360); errors as `ReadAngleCounts()`. */
  Result<float> ReadAngleDegrees() noexcept;
  /** @return The angle in radians, [0, 2π); errors as `ReadAngleCounts()`. */
  Result<float> ReadAngleRadians() noexcept;
  /** @return The angular velocity in counts (signed 14 bit, 24.141 °/s per count); errors and
   * frames as `ReadAngleCounts()`, `MagnetWarning` included. */
  Result<int16_t> ReadVelocityCounts() noexcept;
  /** @return The angular velocity in degrees per second. */
  Result<float> ReadVelocityDegPerSec() noexcept;
  /** @return The angular velocity in revolutions per minute. */
  Result<float> ReadVelocityRpm() noexcept;
  /** @return The AGC value, 0 to 255: low for a strong field, high for a weak one. */
  Result<uint8_t> ReadAgc() noexcept;
  /** @return The CORDIC magnitude, 0 to 16383. */
  Result<uint16_t> ReadMagnitude() noexcept;

  /// @}
  /// @name Diagnostics
  /// @{

  /** @return DIA, decoded. */
  Result<Diagnostics> ReadDiagnostics() noexcept;

  /**
   * @brief Check that the chip still holds the settings the driver wrote: the cheap test for a
   * reset of the chip that ERRFL may not report.
   *
   * Reads DISABLE to ECC (14 frames, 7 register reads; about 34 µs of bus time at 10 MHz with
   * 24-bit frames) and compares them with what the driver last wrote there or, with
   * `write_settings` false, read at start-up. A power-on reset or brown-out reloads them from the
   * OTP ("Non-Volatile Registers (OTP)"), so a configuration that differs from the OTP shows the
   * reset; one equal to the OTP is not changed by a reset, so nothing is lost. Call it at the rate
   * at which a reset must be noticed (for example once per control-loop supervision tick); the
   * driver adds no periodic traffic of its own.
   *
   * @return `Ok`; `DeviceReset` (the registers differ: the chip was reset, or a write was lost;
   *         the driver is now uninitialised, call `EnsureInitialized()`); `NotInitialized`; or a
   *         read error (`Bus`, `Crc`, `NoDevice`, `Framing`, `Command`, `DeviceFault`), after
   *         which the driver stays initialised, unless ERRFL reported a reset hint during the
   *         read, which is `DeviceReset`.
   */
  Result<void> CheckForReset() noexcept;

  /**
   * @brief Read ERRFL, which clears it on the chip. The value is also kept in
   * `GetLastErrorFlags()` and ORed into `GetLatchedErrorFlags()`.
   * @return The flags read now, or `NotInitialized`, `Bus`, `Crc`, `NoDevice` (the reply has
   *         ERRFL's reserved bit set, or is a line pattern) or `DeviceReset` (ERRFL holds
   *         OffCompNotFinished or WDTST and the settings registers changed; the flags are still
   *         latched). A failed read sets `flags_lost` in `GetLatchedErrorFlags()`.
   */
  Result<ErrorFlags> ReadErrorFlags() noexcept;

  /** @return The ERRFL value the driver read last, by any call. No I/O. */
  [[nodiscard]] ErrorFlags GetLastErrorFlags() const noexcept {
    return detail::DecodeErrorFlags(last_errfl_);
  }

  /**
   * @brief Every ERRFL flag the driver has read since the last `ClearLatchedErrorFlags()`. No I/O.
   *
   * ERRFL clears when it is read, and the driver reads it inside other calls: to resolve a reply's
   * error or warning bit, and once at start-up. Each of those reads, `ReadErrorFlags()` and a raw
   * `ReadRegister(reg::kErrfl)` OR the value into this latch, so no internal read hides a flag
   * from the caller (handbook KB-CPP-119). `EnsureInitialized()` empties it first; after start-up
   * it holds what the power-up left in ERRFL (for example `offset_comp` from before the loops
   * settled): clear it there if only later flags matter.
   *
   * `flags_lost` is set when a read of ERRFL failed once its command may have reached the chip
   * (a reply CRC error, a failed transfer, `NoDevice` for the reserved bit): the chip cleared the
   * flags that read took, so a fault may have been missed. The driver then compares the settings
   * registers itself (14 frames, `DeviceReset` on a difference). When that compare cannot be read
   * either, the call keeps its own error and the compare stays pending: each later call runs it and
   * fails with its error until it is read, so no value is returned meanwhile. Check the bus,
   * read `ReadDiagnostics()`, and call `CheckForReset()` when it is set.
   *
   * @return The latched flags; `raw` is their OR.
   */
  [[nodiscard]] ErrorFlags GetLatchedErrorFlags() const noexcept {
    ErrorFlags f = detail::DecodeErrorFlags(latched_errfl_);
    f.flags_lost = flags_lost_;
    return f;
  }

  /** @brief Empty the latch of `GetLatchedErrorFlags()`, `flags_lost` included. No I/O. */
  void ClearLatchedErrorFlags() noexcept {
    latched_errfl_ = 0U;
    flags_lost_ = false;
  }

  /**
   * @return The raw reply word of the last frame (warning bit 15, error bit 14, 14 data bits),
   * by any call, whatever its CRC said; 0 before the first. No I/O (handbook KB-CPP-129).
   */
  [[nodiscard]] uint16_t GetLastReply() const noexcept {
    return bus_.LastReply();
  }

  /**
   * @brief Read one register once: the command frame and the NOP frame that carries the answer,
   * no retry (handbook KB-CPP-129).
   *
   * When the reply's error bit is set, ERRFL is read (two more frames) and the call fails with the
   * error it names; a warning bit alone does not fail the call. Side effects are the chip's own:
   * reading ERRFL clears it (the driver latches it, see `GetLatchedErrorFlags()`), reading DIA
   * restarts its SPI frame counter.
   *
   * @param address 14-bit address.
   * @return The 14-bit content, or `NotInitialized`, `InvalidArgument` (above 0x3FFF; nothing
   *         sent), `Bus`, `Crc`, `NoDevice`, `Framing`, `Command`, `DeviceFault` or `DeviceReset`
   *         (as for `ReadAngleCounts()`).
   */
  Result<uint16_t> ReadRegister(uint16_t address) noexcept;

  /**
   * @brief Write a register: the command and data frames, nothing more unless `verify` asks for a
   * read-back. PROG is refused: use `ProgramOtp()`. A write to DISABLE to SETTINGS3 does not change
   * `GetConfig()`, so the next `ApplyConfig()` or setter writes the configuration over it. When
   * such a write fails, DISABLE to ECC are read back (14 frames) for the reset check's copy.
   * @param address 14-bit address, not `reg::kProg`.
   * @param value 14-bit value.
   * @param verify `Verify::ReadBack` reads the register back (two more frames) and compares.
   * @return `Ok`, `NotInitialized`, `InvalidArgument`, `Bus`, `Crc`, `Framing`, `Command`,
   *         `DeviceFault`, `WriteMismatch` (with `Verify::ReadBack`) or `DeviceReset` (as for
   *         `ReadAngleCounts()`).
   */
  Result<void> WriteRegister(uint16_t address, uint16_t value,
                             Verify verify = Verify::None) noexcept;

  /** @return DISABLE to ECC as the chip holds them now. */
  Result<NonVolatileRegisters> ReadNonVolatile() noexcept;

  /// @}
  /// @name OTP
  /// @{

  /**
   * @brief Burn the current settings into the OTP. **This cannot be undone.**
   *
   * Follows the datasheet's step-by-step OTP procedure from step 6: the zero position is the one
   * configured (steps 4 and 5, which set it to the current angle, are `SetZeroHere()`). Steps 1
   * to 3 (supply, settings) and 20 to 22 (power cycle and check) are the caller's. Needs the
   * `DelayUs()` hook. Every frame of the procedure is CRC-protected, whatever `Config::frame` says,
   * and the writes of steps 7, 9, 12 and 15 are always read back: a burn cannot be undone.
   *
   * @param confirm Must be `kOtpConfirm`, so that a stray call cannot burn the part.
   * @param report Filled with the step that failed, whether the burn started, and what was burned.
   * @return `Ok`, `NotInitialized`, `InvalidArgument` (wrong `confirm`), `Bus`, `Crc`, `Framing`,
   *         `Command`, `DeviceFault`, `WriteMismatch` (steps 7, 9, 11, 12 and 15), `Timeout` (step
   * 14) or `OtpVerify` (step 19: the part failed the guard-band test).
   */
  Result<void> ProgramOtp(OtpBurnConfirm confirm, OtpReport& report) noexcept;

  /// @}

private:
  Result<void> initialize() noexcept;
  Result<void> applyConfig(const Config& config, bool verify) noexcept;
  Result<void> waitReady() noexcept;
  Error classifyDia() noexcept;
  Result<void> writeSettings(const Config& config, bool verify) noexcept;
  Result<uint16_t> readRaw(uint16_t address) noexcept;
  Result<uint16_t> readChecked(uint16_t address) noexcept;
  Result<uint16_t> readMeasurement(uint16_t address) noexcept;
  Result<uint16_t> measure(uint16_t address) noexcept;
  Result<uint16_t> readErrfl() noexcept;
  Result<void> writeRaw(uint16_t address, uint16_t value, bool verify) noexcept;
  Result<void> resolveFlags(bool measurement) noexcept;
  Result<void> checkReset() noexcept;
  void flagsLost() noexcept;
  void rereadAfterFailedWrite(const Config& base, bool settings) noexcept;
  Result<NonVolatileRegisters> readNonVolatile() noexcept;
  Result<void> programOtp(OtpReport& report) noexcept;
  Result<void> otpPrepare(OtpReport& report) noexcept;
  Result<void> otpBurn(OtpReport& report) noexcept;
  Result<void> otpVerify(OtpReport& report) noexcept;
  Result<void> fail(Error error, const char* message) noexcept;

  /// Once a public call's own access is done: when one of its ERRFL reads held a reset hint,
  /// compare the settings registers (`checkReset()`), and return `DeviceReset` on a difference.
  template <typename T>
  Result<T> afterAccess(const Result<T>& r) noexcept {
    if (!reset_hint_ && !lost_hint_) {
      return r;
    }
    if (!initialized_) {
      reset_hint_ = false; // already uninitialised: EnsureInitialized() writes everything
      lost_hint_ = false;
      return r;
    }
    // A reset replaces the call's own result. A compare that could not be read leaves a failed
    // call's own error, but fails a call that succeeded with the compare's error: no value is
    // returned while a reset may have gone unseen (the compare stays pending).
    const auto c = checkReset();
    if (c.error == Error::DeviceReset) {
      return Result<T>::Failure(Error::DeviceReset);
    }
    if (!c && r.ok()) {
      return Result<T>::Failure(c.error);
    }
    return r;
  }

  [[nodiscard]] FrameFormat writeFormat() const noexcept {
    return (config_.frame == FrameFormat::Bits16) ? FrameFormat::Bits24 : config_.frame;
  }

  detail::FrameBus<Port> bus_;
  Config config_{};
  uint16_t last_errfl_{0};
  uint16_t latched_errfl_{0};
  /// DISABLE to ECC as the driver last wrote or read them: what the chip holds unless it was reset.
  NonVolatileRegisters expected_nv_{};
  bool flags_lost_{false};
  /// An ERRFL read after start-up held OffCompNotFinished or WDTST: `afterAccess()` checks.
  bool reset_hint_{false};
  /// An ERRFL read after start-up failed, so a reset hint may have been lost: `afterAccess()`
  /// compares the settings, and keeps this pending while that compare cannot be read.
  bool lost_hint_{false};
  bool initialized_{false};
};

/** @} */

} // namespace as5047u

// codeql::misra_deviation_next_line(MD-CPP-009)
#include "../src/as5047u.ipp" // IWYU pragma: export

#endif // AS5047U_HPP
