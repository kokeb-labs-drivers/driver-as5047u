/**
 * @file as5047u.ipp
 * @brief AS5047U driver implementation, included by `as5047u.hpp`.
 * @copyright Copyright (c) 2026 The driver-as5047u authors. SPDX-License-Identifier: MIT
 *
 * Section and figure numbers refer to the AS5047U datasheet (ams OSRAM DS000637, v1-01).
 */
#ifndef AS5047U_IPP
#define AS5047U_IPP

namespace as5047u {

// -------------------------------------------------------------------------------------------------
// Lifecycle
// -------------------------------------------------------------------------------------------------

template <SpiPort Port>
Result<void> AS5047U<Port>::EnsureInitialized() noexcept {
  if (initialized_) {
    return Result<void>::Success();
  }
  if (auto r = initialize(); !r) {
    return r;
  }
  initialized_ = true;
  detail::Log(bus_.GetPort(), LogLevel::Info, "as5047u: initialised");
  return Result<void>::Success();
}

template <SpiPort Port>
Result<void> AS5047U<Port>::initialize() noexcept {
  const Config config = config_;
  if (Validate(config) != Error::Ok) {
    return fail(Error::InvalidArgument, "as5047u: invalid Config");
  }
  if (!bus_.GetPort().EnsureInitialized()) {
    return fail(Error::Bus, "as5047u: the port could not bring the bus up");
  }
  latched_errfl_ = 0U;
  flags_lost_ = false;
  reset_hint_ = false;
  lost_hint_ = false;
  if (auto r = waitReady(); !r) {
    return r;
  }
  // ERRFL holds what the power-up left (offset compensation before the loops settled, a framing
  // error from the first edges): read it, which clears it, so the first real access is judged on
  // its own. The value is latched, not dropped (KB-CPP-119).
  if (auto flush = readErrfl(); !flush) {
    return fail(flush.error, "as5047u: reading ERRFL at start-up failed");
  }
  if (config.write_settings) {
    // Start-up is not timing-critical: every settings write is read back here.
    if (auto r = applyConfig(config, true); !r) {
      return fail(r.error, "as5047u: writing the settings failed");
    }
    return Result<void>::Success();
  }
  const auto now = readNonVolatile();
  if (!now) {
    return fail(now.error, "as5047u: reading the settings failed");
  }
  config_ = detail::DecodeSettings(now.value, config);
  expected_nv_ = now.value;
  return Result<void>::Success();
}

template <SpiPort Port>
Result<void> AS5047U<Port>::Deinitialize() noexcept {
  initialized_ = false;
  return Result<void>::Success();
}

// Start-up readiness (KB-CPP-116, -117, -124). The chip has no ID register; DIA is the active
// answer: after t_pon every magnetic loop reports finished (bit 1, Figure 35; 0xX3C2 after
// power-up, Figure 32) and bit 10 reads 0. MISO stuck high reads all ones (0x3FFF, or a wrong CRC
// in a 24-bit frame), stuck low or a looped-back MOSI reads 0. DIA is polled once per
// millisecond up to the Config deadline; a ready DIA counts only when the next read confirms it.
// The deadline counts polls, so it holds whatever DelayUs() costs.
namespace detail {

/// What one DIA value at start-up says: `Ok` when the chip is up.
constexpr Error ClassifyStartupDia(uint16_t dia) noexcept {
  const uint32_t v = static_cast<uint32_t>(dia) & reg::kDataMask;
  if ((v == 0U) || (v == reg::kDataMask)) {
    return Error::NoDevice; // a stuck or looped-back line
  }
  if ((v & reg::kDiaReserved) != 0U) {
    return Error::DeviceIdMismatch; // something answers, but not an AS5047U
  }
  return ((v & reg::kDiaLoopsFinished) != 0U) ? Error::Ok : Error::NotReady;
}
static_assert(ClassifyStartupDia(reg::kDiaReadyValue) == Error::Ok);
static_assert((ClassifyStartupDia(0_u16) == Error::NoDevice) &&
              (ClassifyStartupDia(0xFFFF_u16) == Error::NoDevice));
static_assert(ClassifyStartupDia(static_cast<uint16_t>(0x0400U | reg::kDiaReadyValue)) ==
              Error::DeviceIdMismatch);
static_assert(ClassifyStartupDia(static_cast<uint16_t>(reg::kDiaReadyValue &
                                                       ~uint32_t{reg::kDiaLoopsFinished})) ==
              Error::NotReady);

} // namespace detail

template <SpiPort Port>
Result<void> AS5047U<Port>::waitReady() noexcept {
  constexpr uint32_t kPollUs = 1000U;
  const uint32_t timeout_ms = config_.startup_timeout_ms;
  Error reason = Error::NoDevice;
  // One poll per millisecond of the deadline, plus the first: a fixed bound, whatever the chip
  // answers (KB-CPP20-058). A poll that reads ready reads DIA again at once to confirm it.
  for (uint32_t poll = 0U; poll <= timeout_ms; ++poll) {
    if (poll > 0U) {
      detail::DelayUs(bus_.GetPort(), kPollUs);
    }
    reason = classifyDia();
    if (reason == Error::Bus) {
      return fail(Error::Bus, "as5047u: transfer failed");
    }
    if (reason == Error::Ok) {
      reason = classifyDia(); // the confirmation
      if (reason == Error::Ok) {
        return Result<void>::Success();
      }
      if (reason == Error::Bus) {
        return fail(Error::Bus, "as5047u: transfer failed");
      }
    }
  }
  return fail(reason, "as5047u: no ready AS5047U answers");
}

template <SpiPort Port>
Error AS5047U<Port>::classifyDia() noexcept {
  const auto dia = readRaw(reg::kDia);
  return dia ? detail::ClassifyStartupDia(dia.value) : dia.error; // Bus, Crc or NoDevice
}

// -------------------------------------------------------------------------------------------------
// Configuration
// -------------------------------------------------------------------------------------------------

template <SpiPort Port>
Result<void> AS5047U<Port>::ApplyConfig(const Config& config, Verify verify) noexcept {
  if ((Validate(config) != Error::Ok) || !detail::IsVerify(verify)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (!initialized_) {
    config_ = config; // applied by the first EnsureInitialized()
    return Result<void>::Success();
  }
  const Config previous = config_;
  // The reset check comes before the read-back, which would otherwise take a reset chip's
  // registers as the expected ones; a hint raised by the read-back itself is checked after it,
  // so that nothing carries over to the next call.
  Result<void> r = afterAccess(applyConfig(config, verify == Verify::ReadBack));
  if (!r) {
    rereadAfterFailedWrite(previous, true);
    r = afterAccess(r);
  }
  return r;
}

// A write that failed (a reply CRC error, a lost frame, an error flag, a read-back mismatch) may
// still have taken effect: the chip stores the data frame when it arrives (Figure 30), whatever
// happens to the reply. The driver never retries the write and never assumes the old or the new
// value: it reads DISABLE to ECC back (CRC- and flag-checked) and takes the chip's settings from
// them. `base` gives the driver-side fields. When that read fails too, nothing is known about the
// settings, so the driver is marked uninitialised and `EnsureInitialized()` writes them again.
template <SpiPort Port>
void AS5047U<Port>::rereadAfterFailedWrite(const Config& base, bool settings) noexcept {
  if (!initialized_) {
    return; // a reset was found meanwhile: EnsureInitialized() writes everything again
  }
  const auto now = readNonVolatile();
  if (!now) {
    if (settings) {
      initialized_ = false;
      detail::Log(bus_.GetPort(), LogLevel::Error,
                  "as5047u: a settings write failed and the read-back too: settings unknown");
    }
    return;
  }
  expected_nv_ = now.value;
  if (settings) {
    config_ = detail::DecodeSettings(now.value, base);
  }
}

template <SpiPort Port>
Result<void> AS5047U<Port>::applyConfig(const Config& config, bool verify) noexcept {
  // This call's frames use the new format already; on a failure the copy goes back to the old
  // one, so GetConfig() never mixes the two (KB-CPP-120).
  const Config previous = config_;
  config_.frame = config.frame;
  config_.pad_byte = config.pad_byte;
  if (auto r = writeSettings(config, verify); !r) {
    config_ = previous;
    return r;
  }
  config_ = config;
  return Result<void>::Success();
}

template <SpiPort Port>
Result<void> AS5047U<Port>::writeSettings(const Config& config, bool verify) noexcept {
  const auto now = readNonVolatile();
  if (!now) {
    return Result<void>::Failure(now.error);
  }
  expected_nv_ = now.value; // what the chip holds; each write below updates its entry
  const NonVolatileRegisters next = detail::EncodeSettings(config, now.value);
  // DISABLE to SETTINGS3; ECC is written by the OTP procedure only. The read above is not a
  // check: ZPOSL and SETTINGS1 hold DIA bits that the writes must keep. Every read in it passed
  // its CRC and flag checks, so no corrupt read reaches a write (KB-CPP-122).
  const std::size_t count = reg::At(reg::NvIndex::Ecc);
  for (std::size_t i = 0U; i < count; ++i) {
    const auto address = static_cast<uint16_t>(reg::kFirstNonVolatile + i);
    if (auto r = writeRaw(address, next[i], verify); !r) {
      return r;
    }
  }
  return Result<void>::Success();
}

template <SpiPort Port>
Result<void> AS5047U<Port>::SetZeroPosition(uint16_t counts, Verify verify) noexcept {
  Config next = config_;
  next.zero_position = counts;
  return ApplyConfig(next, verify);
}

// Datasheet OTP procedure, steps 4 and 5: with the zero at 0, the angle read is the new zero.
template <SpiPort Port>
Result<uint16_t> AS5047U<Port>::SetZeroHere(Verify verify) noexcept {
  if (!initialized_) {
    return Result<uint16_t>::Failure(Error::NotInitialized); // nothing to read the position from
  }
  using detail::operator""_u16;
  const uint16_t previous = config_.zero_position;
  if (auto r = SetZeroPosition(0_u16, verify); !r) {
    return Result<uint16_t>::Failure(r.error);
  }
  const auto angle = ReadAngleCounts();
  if (!angle) {
    // Put the old zero back, so a failed call does not leave the part at zero 0. If this write
    // fails too, GetConfig() still says 0, which is what the chip then holds.
    (void)SetZeroPosition(previous, verify);
    return angle;
  }
  if (auto r = SetZeroPosition(angle.value, verify); !r) {
    return Result<uint16_t>::Failure(r.error);
  }
  return angle;
}

template <SpiPort Port>
Result<void> AS5047U<Port>::SetDirection(Direction direction, Verify verify) noexcept {
  Config next = config_;
  next.direction = direction;
  return ApplyConfig(next, verify);
}

// -------------------------------------------------------------------------------------------------
// Measurement
// -------------------------------------------------------------------------------------------------

template <SpiPort Port>
Result<uint16_t> AS5047U<Port>::ReadAngleCounts() noexcept {
  return readMeasurement(reg::kAngleCom);
}

template <SpiPort Port>
Result<uint16_t> AS5047U<Port>::ReadUncompensatedAngleCounts() noexcept {
  return readMeasurement(reg::kAngleUnc);
}

template <SpiPort Port>
Result<float> AS5047U<Port>::ReadAngleDegrees() noexcept {
  const auto r = ReadAngleCounts();
  return r ? Result<float>::Success(CountsToDegrees(r.value)) : Result<float>::Failure(r.error);
}

template <SpiPort Port>
Result<float> AS5047U<Port>::ReadAngleRadians() noexcept {
  const auto r = ReadAngleCounts();
  return r ? Result<float>::Success(CountsToRadians(r.value)) : Result<float>::Failure(r.error);
}

template <SpiPort Port>
Result<int16_t> AS5047U<Port>::ReadVelocityCounts() noexcept {
  const auto r = readMeasurement(reg::kVel);
  return r ? Result<int16_t>::Success(detail::SignExtend14(r.value))
           : Result<int16_t>::Failure(r.error);
}

template <SpiPort Port>
Result<float> AS5047U<Port>::ReadVelocityDegPerSec() noexcept {
  const auto r = ReadVelocityCounts();
  return r ? Result<float>::Success(static_cast<float>(r.value) * kVelocityDegPerSecPerLsb)
           : Result<float>::Failure(r.error);
}

template <SpiPort Port>
Result<float> AS5047U<Port>::ReadVelocityRpm() noexcept {
  const auto r = ReadVelocityDegPerSec();
  return r ? Result<float>::Success(r.value / 6.0F) : r;
}

template <SpiPort Port>
Result<uint8_t> AS5047U<Port>::ReadAgc() noexcept {
  const auto r = ReadRegister(reg::kAgc);
  return r ? Result<uint8_t>::Success(static_cast<uint8_t>(static_cast<uint32_t>(r.value) & 0xFFU))
           : Result<uint8_t>::Failure(r.error);
}

template <SpiPort Port>
Result<uint16_t> AS5047U<Port>::ReadMagnitude() noexcept {
  return ReadRegister(reg::kMag);
}

// -------------------------------------------------------------------------------------------------
// Diagnostics
// -------------------------------------------------------------------------------------------------

template <SpiPort Port>
Result<Diagnostics> AS5047U<Port>::ReadDiagnostics() noexcept {
  const auto r = ReadRegister(reg::kDia);
  return r ? Result<Diagnostics>::Success(detail::DecodeDiagnostics(r.value))
           : Result<Diagnostics>::Failure(r.error);
}

template <SpiPort Port>
Result<ErrorFlags> AS5047U<Port>::ReadErrorFlags() noexcept {
  if (!initialized_) {
    return Result<ErrorFlags>::Failure(Error::NotInitialized);
  }
  const auto r = afterAccess(readErrfl());
  return r ? Result<ErrorFlags>::Success(detail::DecodeErrorFlags(r.value))
           : Result<ErrorFlags>::Failure(r.error);
}

template <SpiPort Port>
Result<uint16_t> AS5047U<Port>::ReadRegister(uint16_t address) noexcept {
  if (!initialized_) {
    return Result<uint16_t>::Failure(Error::NotInitialized);
  }
  if (address > reg::kDataMask) {
    return Result<uint16_t>::Failure(Error::InvalidArgument);
  }
  // ERRFL clears when read: through the latch, never around it (KB-CPP-119).
  return afterAccess((address == reg::kErrfl) ? readErrfl() : readChecked(address));
}

template <SpiPort Port>
Result<void> AS5047U<Port>::WriteRegister(uint16_t address, uint16_t value,
                                          Verify verify) noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if ((address > reg::kDataMask) || (value > reg::kDataMask) || (address == reg::kProg) ||
      !detail::IsVerify(verify)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  Result<void> r = afterAccess(writeRaw(address, value, verify == Verify::ReadBack));
  const auto nv_index = static_cast<uint32_t>(address) - uint32_t{reg::kFirstNonVolatile};
  if (!r && (nv_index < reg::kNonVolatileCount)) {
    rereadAfterFailedWrite(config_, false); // GetConfig() never follows raw writes
    r = afterAccess(r);                     // as in ApplyConfig()
  }
  return r;
}

template <SpiPort Port>
Result<NonVolatileRegisters> AS5047U<Port>::ReadNonVolatile() noexcept {
  if (!initialized_) {
    return Result<NonVolatileRegisters>::Failure(Error::NotInitialized);
  }
  return afterAccess(readNonVolatile());
}

template <SpiPort Port>
Result<NonVolatileRegisters> AS5047U<Port>::readNonVolatile() noexcept {
  NonVolatileRegisters out{};
  const std::size_t count = out.size();
  for (std::size_t i = 0U; i < count; ++i) {
    const auto r = readChecked(static_cast<uint16_t>(reg::kFirstNonVolatile + i));
    if (!r) {
      return Result<NonVolatileRegisters>::Failure(r.error);
    }
    out[i] = r.value;
  }
  return Result<NonVolatileRegisters>::Success(out);
}

// -------------------------------------------------------------------------------------------------
// OTP: "Step-by-Step Procedure to Permanently Program the Non-Volatile Memory" and Figure 60. The
// steps read and compare DISABLE to ECC (0x0015 to 0x001B): DISABLE is non-volatile too
// (Figure 42), so it is burned and checked with the others. The writes of steps 7, 9, 12 and 15
// are always read back, whatever a caller passes elsewhere as `Verify`: a burn cannot be undone
// and is not timing-critical. PROG commands that start an action (steps 13, 16, 17) are sent once
// and never read back.
// -------------------------------------------------------------------------------------------------

template <SpiPort Port>
Result<void> AS5047U<Port>::ProgramOtp(OtpBurnConfirm confirm, OtpReport& report) noexcept {
  report = OtpReport{};
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if (confirm.code != kOtpConfirm.code) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  // Every frame of the procedure carries a CRC, reads included: a corrupted compare in step 11 or
  // 19 would otherwise pass unnoticed.
  const FrameFormat saved = config_.frame;
  config_.frame = writeFormat();
  const Result<void> r = afterAccess(programOtp(report));
  config_.frame = saved;
  if (!r) {
    return r;
  }
  detail::Log(bus_.GetPort(), LogLevel::Info, "as5047u: OTP burned and verified");
  return r;
}

template <SpiPort Port>
Result<void> AS5047U<Port>::programOtp(OtpReport& report) noexcept {
  if (auto r = otpPrepare(report); !r) {
    return fail(r.error, "as5047u: OTP preparation failed; nothing was burned");
  }
  if (auto r = otpBurn(report); !r) {
    return fail(r.error, "as5047u: OTP burn failed");
  }
  if (auto r = otpVerify(report); !r) {
    return fail(r.error, "as5047u: OTP guard-band verification failed");
  }
  return Result<void>::Success();
}

// Steps 6 to 11: compute the ECC and check that the registers hold what will be burned.
template <SpiPort Port>
Result<void> AS5047U<Port>::otpPrepare(OtpReport& report) noexcept {
  constexpr std::size_t kEccIndex = reg::At(reg::NvIndex::Ecc);
  report.failed_step = 6;
  auto expected = readNonVolatile();
  if (!expected) {
    return Result<void>::Failure(expected.error);
  }
  report.failed_step = 7;
  if (auto r = writeRaw(reg::kEcc, reg::kEccEnable, true); !r) {
    return r;
  }
  report.failed_step = 8;
  const auto key = readChecked(reg::kEccChecksum);
  if (!key) {
    return Result<void>::Failure(key.error);
  }
  report.failed_step = 9;
  const auto ecc =
      static_cast<uint16_t>(static_cast<uint32_t>(reg::kEccEnable) |
                            (static_cast<uint32_t>(key.value) & reg::kEccChecksumMask));
  if (auto r = writeRaw(reg::kEcc, ecc, true); !r) {
    return r;
  }
  expected.value[kEccIndex] = ecc;
  report.failed_step = 10;
  const auto step1 = readNonVolatile();
  if (!step1) {
    return Result<void>::Failure(step1.error);
  }
  report.failed_step = 11;
  if (step1.value != expected.value) {
    return Result<void>::Failure(Error::WriteMismatch);
  }
  report.written = step1.value;
  report.failed_step = 0;
  return Result<void>::Success();
}

// Steps 12 to 14: enable programming, start the burn (sent once, never retried) and wait for it.
template <SpiPort Port>
Result<void> AS5047U<Port>::otpBurn(OtpReport& report) noexcept {
  report.failed_step = 12;
  if (auto r = writeRaw(reg::kProg, reg::kProgEnable, true); !r) {
    return r;
  }
  report.failed_step = 13;
  report.burn_started = true;
  if (auto r = writeRaw(reg::kProg, reg::kProgBurn, false); !r) {
    return r;
  }
  report.failed_step = 14;
  using detail::operator""_u32;
  for (uint32_t ms = 0; ms < kOtpBurnTimeoutMs; ++ms) {
    detail::DelayUs(bus_.GetPort(), 1000_u32);
    const auto prog = readChecked(reg::kProg);
    if (!prog) {
      return Result<void>::Failure(prog.error);
    }
    if (prog.value == reg::kProgDone) {
      report.failed_step = 0;
      return Result<void>::Success();
    }
  }
  return Result<void>::Failure(Error::Timeout);
}

// Steps 15 to 19: clear the volatile copy, reload it from the OTP at the guard-band level and
// compare it with what was burned.
template <SpiPort Port>
Result<void> AS5047U<Port>::otpVerify(OtpReport& report) noexcept {
  report.failed_step = 15;
  using detail::operator""_u16;
  for (std::size_t i = 0U; i < reg::kNonVolatileCount; ++i) {
    if (auto r = writeRaw(static_cast<uint16_t>(reg::kFirstNonVolatile + i), 0_u16, true); !r) {
      return r;
    }
  }
  report.failed_step = 16;
  if (auto r = writeRaw(reg::kProg, reg::kProgVerify, false); !r) {
    return r;
  }
  report.failed_step = 17;
  if (auto r = writeRaw(reg::kProg, reg::kProgRefresh, false); !r) {
    return r;
  }
  report.failed_step = 18;
  const auto step2 = readNonVolatile();
  if (!step2) {
    return Result<void>::Failure(step2.error);
  }
  expected_nv_ = step2.value; // the chip now holds what the OTP reloaded
  report.failed_step = 19;
  if (step2.value != report.written) {
    return Result<void>::Failure(Error::OtpVerify);
  }
  report.failed_step = 0;
  return Result<void>::Success();
}

// -------------------------------------------------------------------------------------------------
// Frames
// -------------------------------------------------------------------------------------------------

// A read is two frames: the command, then a NOP whose reply carries the data (Figure 29). The reply
// to the command frame belongs to the previous command, so only a failed transfer counts there.
// Both frames go out inside this one call, so no other access can come between them (KB-ARCH-043).
template <SpiPort Port>
Result<uint16_t> AS5047U<Port>::readRaw(uint16_t address) noexcept {
  const auto command = static_cast<uint16_t>(static_cast<uint32_t>(reg::kReadBit) |
                                             (static_cast<uint32_t>(address) & reg::kDataMask));
  const auto first = bus_.Exchange(config_.frame, config_.pad_byte, command);
  if (!first && (first.error == Error::Bus)) {
    return first;
  }
  constexpr auto kNopRead = static_cast<uint16_t>(static_cast<uint32_t>(reg::kReadBit) |
                                                  static_cast<uint32_t>(reg::kNop));
  return bus_.Exchange(config_.frame, config_.pad_byte, kNopRead);
}

// A register read: the error bit of the reply is resolved through ERRFL; a warning alone leaves
// the value valid (AGC, magnitude, DIA and the settings describe the chip, not the angle).
template <SpiPort Port>
Result<uint16_t> AS5047U<Port>::readChecked(uint16_t address) noexcept {
  const auto r = readRaw(address);
  if (!r) {
    return r;
  }
  if ((static_cast<uint32_t>(r.value) & reg::kReplyErrorBit) != 0U) {
    if (auto c = resolveFlags(false); !c) {
      return Result<uint16_t>::Failure(c.error);
    }
  }
  return Result<uint16_t>::Success(
      static_cast<uint16_t>(static_cast<uint32_t>(r.value) & reg::kDataMask));
}

// The hot path (angle and velocity): two frames and one test of the reply's flag bits. Only a
// flagged reply costs the two frames of an ERRFL read, which decides whether the value is a
// measurement (KB-CPP-127).
template <SpiPort Port>
Result<uint16_t> AS5047U<Port>::readMeasurement(uint16_t address) noexcept {
  if (!initialized_) {
    return Result<uint16_t>::Failure(Error::NotInitialized);
  }
  return afterAccess(measure(address));
}

template <SpiPort Port>
Result<uint16_t> AS5047U<Port>::measure(uint16_t address) noexcept {
  const auto r = readRaw(address);
  if (!r) {
    return r;
  }
  const auto reply = static_cast<uint32_t>(r.value);
  if ((reply & reg::kReplyFlagBits) != 0U) {
    if (auto c = resolveFlags(true); !c) {
      return Result<uint16_t>::Failure(c.error);
    }
  }
  return Result<uint16_t>::Success(static_cast<uint16_t>(reply & reg::kDataMask));
}

// ERRFL: read, checked for its reserved bit, kept as the last value and ORed into the latch. The
// reply flags of an ERRFL read describe ERRFL itself, so they are not resolved again. ERRFL clears
// when it is read (Figure 33), so a read that fails once its command may have gone out has taken
// flags from the chip that the driver never saw: the latch records that (`flags_lost`).
template <SpiPort Port>
Result<uint16_t> AS5047U<Port>::readErrfl() noexcept {
  const auto r = readRaw(reg::kErrfl);
  if (!r) {
    flagsLost();
    return r;
  }
  const uint32_t value = static_cast<uint32_t>(r.value) & reg::kDataMask;
  if ((value & reg::kErrflReserved) != 0U) {
    flagsLost();
    return Result<uint16_t>::Failure(Error::NoDevice); // all ones, or not ERRFL at all
  }
  // Latched before anything else is decided: the chip has cleared these flags (KB-CPP-119).
  last_errfl_ = static_cast<uint16_t>(value);
  latched_errfl_ = static_cast<uint16_t>(static_cast<uint32_t>(latched_errfl_) | value);
  // A flag bit on this reply with ERRFL empty is not an answer from the chip: a looped-back MOSI
  // reads the NOP command 0x4000 back, a deaf chip flags the frames it could not check.
  if (((static_cast<uint32_t>(r.value) & reg::kReplyFlagBits) != 0U) && (value == 0U)) {
    return Result<uint16_t>::Failure(Error::NoDevice);
  }
  // Recorded here, checked by the public call once its own access is done (`afterAccess()`), so
  // that no read of ERRFL calls back into the register reads it is part of.
  if (initialized_ && ((value & reg::kErrflResetHint) != 0U)) {
    reset_hint_ = true;
  }
  return Result<uint16_t>::Success(last_errfl_);
}

// The flags a failed ERRFL read took may have included the one sign of a reset the chip gives
// (OffCompNotFinished, Figure 33): a brown-out that resets the chip is also what most likely
// corrupts the frame. So a lost ERRFL makes the driver compare the settings too. It is weaker
// evidence than a flag seen: when that compare cannot be read either, the call keeps its own
// error and the compare stays pending, instead of declaring a reset; each later call runs it and
// fails with its error until it is read (`afterAccess()`), so no value passes an unseen reset.
template <SpiPort Port>
void AS5047U<Port>::flagsLost() noexcept {
  flags_lost_ = true;
  if (initialized_) {
    lost_hint_ = true;
  }
}

// Reset detection. A power-on reset or brown-out reloads DISABLE to ECC from the OTP and loses
// every soft write ("Non-Volatile Registers (OTP)"), and the offset compensation starts again
// (OffCompNotFinished, Figure 33). The settings registers are read (14 frames) and compared with
// what the driver last wrote or read there; a difference, or (after a reset hint in ERRFL) a read
// that cannot be completed, marks the driver uninitialised, so the old settings are never assumed.
template <SpiPort Port>
Result<void> AS5047U<Port>::checkReset() noexcept {
  const bool flag_seen = reset_hint_; // OffCompNotFinished or WDTST read from ERRFL
  const bool flags_lost = lost_hint_; // an ERRFL read failed: those flags are unknown
  reset_hint_ = false;
  lost_hint_ = false;
  const auto now = readNonVolatile();
  const bool flag_seen_now = reset_hint_; // a flagged reply of the read itself
  reset_hint_ = false;
  if (now) {
    lost_hint_ = false;
    if (now.value == expected_nv_) {
      return Result<void>::Success();
    }
    initialized_ = false;
    return fail(Error::DeviceReset, "as5047u: the settings registers changed: chip reset");
  }
  if (flag_seen || flag_seen_now) {
    lost_hint_ = false;
    initialized_ = false; // a reset was signalled and cannot be ruled out
    return fail(Error::DeviceReset, "as5047u: reset flagged, settings unreadable");
  }
  // Not resolved: after a lost ERRFL, the next call compares again; otherwise a failed read alone
  // says nothing about a reset.
  lost_hint_ = lost_hint_ || flags_lost;
  return Result<void>::Failure(now.error);
}

template <SpiPort Port>
Result<void> AS5047U<Port>::CheckForReset() noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  return checkReset();
}

// A flagged reply: ERRFL says why. An error flag fails the access with its error. A flag bit with
// ERRFL empty is not a reply this chip gives (a looped-back MOSI reads the NOP command 0x4000
// back): NoDevice, never a value. The datasheet does not say which ERRFL flags drive the error bit
// and which the warning bit (Figures 22 and 33), so the driver judges by ERRFL, not by which bit
// was set: for a measurement, a field warning refuses the value either way.
template <SpiPort Port>
Result<void> AS5047U<Port>::resolveFlags(bool measurement) noexcept {
  const auto errfl = readErrfl();
  if (!errfl) {
    return Result<void>::Failure(errfl.error);
  }
  const Error e = detail::ClassifyErrorFlags(errfl.value);
  if (e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if (errfl.value == 0U) {
    return Result<void>::Failure(Error::NoDevice);
  }
  if (measurement && ((static_cast<uint32_t>(errfl.value) & reg::kErrflFieldWarning) != 0U)) {
    return Result<void>::Failure(Error::MagnetWarning);
  }
  return Result<void>::Success();
}

// A write is two frames, the command and the data, always CRC-protected (Figure 30). The reply to
// the data frame reports on the command frame. `verify` reads the register back.
template <SpiPort Port>
Result<void> AS5047U<Port>::writeRaw(uint16_t address, uint16_t value, bool verify) noexcept {
  const FrameFormat format = writeFormat();
  const auto command =
      bus_.Exchange(format, config_.pad_byte,
                    static_cast<uint16_t>(static_cast<uint32_t>(address) & reg::kDataMask));
  if (!command && (command.error == Error::Bus)) {
    return Result<void>::Failure(Error::Bus);
  }
  const auto masked = static_cast<uint16_t>(static_cast<uint32_t>(value) & reg::kDataMask);
  const auto data = bus_.Exchange(format, config_.pad_byte, masked);
  if (!data) {
    return Result<void>::Failure(data.error);
  }
  // The data frame arrived: the chip holds `masked` now, which the reset check compares with.
  const auto nv_index = static_cast<uint32_t>(address) - uint32_t{reg::kFirstNonVolatile};
  if (nv_index < reg::kNonVolatileCount) {
    // Only the implemented bits are stored (Figures 43 to 49): the others read back as 0.
    expected_nv_[nv_index] =
        static_cast<uint16_t>(static_cast<uint32_t>(masked) & reg::NvImplementedBits(nv_index));
  }
  if ((static_cast<uint32_t>(data.value) & reg::kReplyErrorBit) != 0U) {
    if (auto c = resolveFlags(false); !c) {
      return c;
    }
  }
  if (!verify) {
    return Result<void>::Success();
  }
  const auto back = readChecked(address);
  if (!back) {
    return Result<void>::Failure(back.error);
  }
  return (back.value == masked) ? Result<void>::Success()
                                : Result<void>::Failure(Error::WriteMismatch);
}

template <SpiPort Port>
Result<void> AS5047U<Port>::fail(Error error, const char* message) noexcept {
  detail::Log(bus_.GetPort(), LogLevel::Error, message);
  return Result<void>::Failure(error);
}

} // namespace as5047u

#endif // AS5047U_IPP
