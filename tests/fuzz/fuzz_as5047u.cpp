/**
 * @file fuzz_as5047u.cpp
 * @brief Fuzz target: the driver under a chip whose replies the fuzzer controls.
 *
 * Everything the driver receives comes from MISO, so MISO is the untrusted input. The fuzzer's
 * bytes choose a configuration, then a sequence of driver calls; on every transfer they also choose
 * what the bus does: pass the chip model's reply, fail the transfer, flip bits of the reply or
 * replace it. Most transfers pass, so the fuzzer reaches every state of the driver, OTP programming
 * included, and from each one sees what corrupted replies do.
 *
 * After every call the target checks the driver's promises (docs/contract.md), and aborts, naming
 * the line, when one breaks:
 *
 * - a failed call carries an `Error` the enum defines, and a value of `T{}`;
 * - a successful read is in the register's range: angle and magnitude below 16384, degrees in
 *   [0, 360), radians in [0, 2 pi), velocity in [-8192, 8191] counts;
 * - before `EnsureInitialized()` succeeds, no call makes a transfer: reads return `NotInitialized`,
 *   setters only store the value (as `ApplyConfig()` does);
 * - a refused `SetZeroHere()` leaves the stored zero as it was;
 * - `InvalidArgument` means nothing was sent;
 * - no corrupted reply ever starts an OTP burn: the chip sees PROGOTP only inside
 *   `ProgramOtp(kOtpConfirm, ...)`;
 * - with MISO stuck low, floating high, looped back to MOSI, or a chip that cannot hear MOSI, no
 *   angle or velocity read in a CRC-protected format returns a value (KB-CPP-116), and no start-up
 *   succeeds.
 *
 * The bus can also replay the previous frame's reply (a stale reply, KB-ARCH-042).
 *
 * Built two ways (tests/fuzz/CMakeLists.txt): with libFuzzer (Clang, `-DAS5047U_BUILD_FUZZERS=ON`),
 * and as a plain executable that replays the corpus in tests/fuzz/corpus/ as a CTest on any
 * compiler.
 */
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "as5047u.hpp"
#include "mock_as5047u.hpp"

namespace {

using as5047u::Error;
using as5047u_test::As5047uModel;
namespace reg = as5047u::reg;

/// The fuzzer's bytes, read front to back; zeros once they run out.
class Input {
public:
  Input(const uint8_t* data, std::size_t size) noexcept : data_(data), size_(size) {}

  [[nodiscard]] bool Empty() const noexcept {
    return pos_ >= size_;
  }
  uint8_t Byte() noexcept {
    return pos_ < size_ ? data_[pos_++] : uint8_t{0};
  }
  uint16_t Word() noexcept {
    const auto high = Byte();
    return static_cast<uint16_t>((static_cast<uint32_t>(high) << 8U) | Byte());
  }

private:
  const uint8_t* data_;
  std::size_t size_;
  std::size_t pos_ = 0;
};

/// A broken promise: name the line and abort, so libFuzzer saves the input as a crash.
void Check(bool promise, int line = __builtin_LINE()) noexcept {
  if (!promise) {
    (void)std::fprintf(stderr, "fuzz_as5047u: promise broken at line %d\n", line);
    std::abort();
  }
}

/// The SPI port: the chip model behind a bus the fuzzer controls.
class FuzzPort : public as5047u::SpiInterface<FuzzPort> {
public:
  explicit FuzzPort(Input& in) noexcept : in_(in) {}

  [[nodiscard]] bool EnsureInitialized() const noexcept {
    return bus_up_ok_;
  }

  bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    Check(tx != nullptr && rx != nullptr && len >= 2 && len <= 4);
    transfers_++;
    const uint8_t action = in_.Byte();
    if (action == 0xFFU) {
      return false; // the bus failed; the frame never reached the chip
    }
    chip_.Frame(tx, rx, len);
    if (action == 0xFEU) {
      mutated_++;
      for (std::size_t i = 0; i < len; ++i) {
        rx[i] = static_cast<uint8_t>(rx[i] ^ in_.Byte()); // noise on MISO
      }
    } else if (action == 0xFDU) {
      mutated_++;
      for (std::size_t i = 0; i < len; ++i) {
        rx[i] = in_.Byte(); // another device answered
      }
    } else if (action == 0xFCU) {
      mutated_++;
      for (std::size_t i = 0; i < len && i < last_.size(); ++i) {
        rx[i] = last_[i]; // the previous reply again: stale, CRC intact
      }
    }
    for (std::size_t i = 0; i < len && i < last_.size(); ++i) {
      last_[i] = rx[i];
    }
    return true; // any other value: the chip's own reply
  }

  void DelayUs(uint32_t us) noexcept {
    chip_.Elapse(us);
  }

  As5047uModel& Chip() noexcept {
    return chip_;
  }
  [[nodiscard]] int Transfers() const noexcept {
    return transfers_;
  }
  [[nodiscard]] int Mutated() const noexcept {
    return mutated_;
  }
  void SetBusUp(bool ok) noexcept {
    bus_up_ok_ = ok;
  }

private:
  Input& in_;
  As5047uModel chip_;
  int transfers_ = 0;
  int mutated_ = 0;
  std::array<uint8_t, 4> last_{};
  bool bus_up_ok_ = true;
};

using Driver = as5047u::AS5047U<FuzzPort>;

bool KnownError(Error e) noexcept {
  switch (e) {
  case Error::Ok:
  case Error::NotInitialized:
  case Error::InvalidArgument:
  case Error::Bus:
  case Error::Timeout:
  case Error::DeviceIdMismatch:
  case Error::Crc:
  case Error::Busy:
  case Error::DeviceFault:
  case Error::Unsupported:
  case Error::Framing:
  case Error::Command:
  case Error::WriteMismatch:
  case Error::OtpVerify:
  case Error::NoDevice:
  case Error::NotReady:
  case Error::MagnetWarning:
  case Error::DeviceReset:
    return true;
  default:
    break;
  }
  return false;
}

/// A configuration from the fuzzer's bytes; often invalid on purpose.
as5047u::Config FuzzConfig(Input& in) noexcept {
  as5047u::Config c{};
  c.frame = static_cast<as5047u::FrameFormat>(in.Byte() % 4U); // 3 is not an enumerator
  c.pad_byte = in.Byte();
  const uint8_t flags = in.Byte();
  c.write_settings = (flags & 0x01U) != 0U;
  c.direction = static_cast<as5047u::Direction>((uint32_t{flags} >> 1U) & 0x01U);
  c.dynamic_compensation = (flags & 0x04U) != 0U;
  c.adaptive_filter = (flags & 0x08U) != 0U;
  c.abi_enable = (flags & 0x10U) != 0U;
  c.uvw_enable = (flags & 0x20U) != 0U;
  c.pwm_enable = (flags & 0x40U) != 0U;
  c.incremental = static_cast<as5047u::IncrementalOutput>(flags >> 7U);
  c.zero_position = static_cast<uint16_t>(in.Word() & 0x7FFFU); // up to twice the valid range
  c.filter_k_min_code = static_cast<uint8_t>(in.Byte() % 9U);
  c.filter_k_max_code = static_cast<uint8_t>(in.Byte() % 9U);
  c.abi_resolution_code = static_cast<uint8_t>(in.Byte() % 9U);
  c.uvw_pole_pairs = static_cast<uint8_t>(in.Byte() % 9U);
  c.hysteresis_code = static_cast<uint8_t>(in.Byte() % 5U);
  const uint8_t more = in.Byte();
  c.abi_decimal = (more & 0x01U) != 0U;
  c.index_width = static_cast<as5047u::IndexWidth>((more >> 1U) % 3U); // 2 is not an enumerator
  c.high_temperature_noise = (more & 0x08U) != 0U;
  c.uncompensated_at_angle_register = (more & 0x10U) != 0U;
  c.startup_timeout_ms =
      (more & 0x20U) != 0U ? 2000U : static_cast<uint32_t>(more >> 6U); // 2000: too long
  return c;
}

/// A per-call write verification; 2 is not an enumerator, so it must be refused with no I/O.
as5047u::Verify FuzzVerify(Input& in) noexcept {
  return static_cast<as5047u::Verify>(in.Byte() % 3U);
}

/// The chip's own state: what a real magnet, motor and fault would put in its registers.
void FuzzChip(As5047uModel& chip, Input& in) noexcept {
  chip.raw_angle = static_cast<uint16_t>(in.Word() & 0x3FFFU);
  chip.velocity = static_cast<uint16_t>(in.Word() & 0x3FFFU);
  chip.magnitude = static_cast<uint16_t>(in.Word() & 0x3FFFU);
  chip.agc = in.Byte();
  chip.errfl = static_cast<uint16_t>(in.Word() & 0x07FFU);
  const uint8_t line_byte = in.Byte();
  // One time in sixteen the lines break, in one of the four ways of KB-CPP-116.
  const auto line = (line_byte & 0x0FU) == 0U
                        ? static_cast<as5047u_test::LineMode>(1U + ((line_byte >> 4U) % 4U))
                        : as5047u_test::LineMode::Normal;
  chip.persistent_errfl = (line_byte & 0x30U) == 0x30U ? reg::kErrflAgcWarning : uint16_t{0};
  chip.power_on_us = (line_byte & 0xC0U) == 0xC0U ? 3000U : 0U;
  if (chip.line != as5047u_test::LineMode::Normal && line == as5047u_test::LineMode::Normal) {
    // The chip comes back: a power-up. Registers and the SPI state reset; the OTP stays.
    As5047uModel fresh;
    fresh.otp = chip.otp;
    fresh.otp_burned = chip.otp_burned;
    fresh.burn_starts = chip.burn_starts;
    if (chip.otp_burned) {
      fresh.nv = chip.otp;
    }
    chip = fresh;
  }
  chip.line = line;
}

/// Checks shared by every call, given the transfers it made.
template <typename R>
void CheckCommon(const Driver& d, const R& r, bool was_up, int transfers) noexcept {
  Check(KnownError(r.error));
  if (!was_up && !d.IsInitialized()) {
    // Only EnsureInitialized() may talk to the chip before start-up; callers check it separately.
    Check(r.error == Error::NotInitialized && transfers == 0);
  }
  if (r.error == Error::InvalidArgument) {
    Check(transfers == 0);
  }
}

/// Setters that go through ApplyConfig(): before start-up they only store the value, with no I/O.
void CheckStored(const as5047u::Result<void>& r, bool was_up, int transfers) noexcept {
  Check(KnownError(r.error));
  if (!was_up) {
    Check((r.error == Error::Ok || r.error == Error::InvalidArgument) && transfers == 0);
  }
  if (r.error == Error::InvalidArgument) {
    Check(transfers == 0);
  }
}

template <typename T>
void CheckValue(const as5047u::Result<T>& r) noexcept {
  if (!r) {
    Check(r.value == T{});
  }
}

void RunOne(Driver& d, FuzzPort& port, Input& in) noexcept {
  const uint8_t op = in.Byte() % 22U;
  const bool was_up = d.IsInitialized();
  const int before = port.Transfers();
  const int burns_before = port.Chip().burn_starts;
  const auto sent = [&] { return port.Transfers() - before; };
  bool may_burn = false;

  switch (op) {
  case 0: {
    const int mutated = port.Mutated();
    const auto r = d.EnsureInitialized();
    Check(KnownError(r.error) && r.ok() == d.IsInitialized());
    if (!was_up && port.Chip().line != as5047u_test::LineMode::Normal &&
        port.Mutated() == mutated) {
      Check(!r); // no start-up on a broken line
    }
    if (was_up) {
      Check(r.ok() && sent() == 0); // idempotent, no I/O once started
    }
    break;
  }
  case 1: {
    const auto r = d.Deinitialize();
    Check(KnownError(r.error) && !d.IsInitialized());
    break;
  }
  case 2: {
    const auto config = FuzzConfig(in);
    const auto r = d.ApplyConfig(config, FuzzVerify(in));
    Check(KnownError(r.error));
    if (!was_up) {
      Check(sent() == 0); // before start-up the config is only stored
    }
    if (r.error == Error::InvalidArgument) {
      Check(sent() == 0);
    }
    break;
  }
  case 3: {
    const auto counts = in.Word();
    const auto r = d.SetZeroPosition(counts, FuzzVerify(in));
    CheckStored(r, was_up, sent());
    break;
  }
  case 4: {
    const auto zero_before = d.GetConfig().zero_position;
    const auto r = d.SetZeroHere(FuzzVerify(in));
    CheckCommon(d, r, was_up, sent());
    CheckValue(r);
    Check(!r || r.value <= 0x3FFFU);
    if (!was_up) {
      Check(d.GetConfig().zero_position == zero_before); // a refused call changes nothing
    }
    break;
  }
  case 5: {
    const auto direction = static_cast<as5047u::Direction>(in.Byte() % 3U);
    const auto r = d.SetDirection(direction, FuzzVerify(in));
    CheckStored(r, was_up, sent());
    break;
  }
  case 6:
  case 7: {
    const int mutated = port.Mutated();
    const auto r = op == 6 ? d.ReadAngleCounts() : d.ReadUncompensatedAngleCounts();
    CheckCommon(d, r, was_up, sent());
    if (port.Chip().line != as5047u_test::LineMode::Normal && port.Mutated() == mutated &&
        d.GetConfig().frame != as5047u::FrameFormat::Bits16) {
      Check(!r); // a broken line is never an angle
    }
    CheckValue(r);
    Check(!r || r.value <= 0x3FFFU);
    break;
  }
  case 8: {
    const auto r = d.ReadAngleDegrees();
    CheckCommon(d, r, was_up, sent());
    Check(!r || (r.value >= 0.0F && r.value < 360.0F));
    break;
  }
  case 9: {
    const auto r = d.ReadAngleRadians();
    CheckCommon(d, r, was_up, sent());
    Check(!r || (r.value >= 0.0F && r.value < 6.2832F));
    break;
  }
  case 10: {
    const auto r = d.ReadVelocityCounts();
    CheckCommon(d, r, was_up, sent());
    CheckValue(r);
    Check(!r || (r.value >= -8192 && r.value <= 8191));
    break;
  }
  case 11:
  case 12: {
    const auto r = op == 11 ? d.ReadVelocityDegPerSec() : d.ReadVelocityRpm();
    CheckCommon(d, r, was_up, sent());
    Check(!r || std::isfinite(r.value));
    break;
  }
  case 13: {
    const auto r = d.ReadAgc();
    CheckCommon(d, r, was_up, sent());
    CheckValue(r);
    break;
  }
  case 14: {
    const auto r = d.ReadMagnitude();
    CheckCommon(d, r, was_up, sent());
    CheckValue(r);
    Check(!r || r.value <= 0x3FFFU);
    break;
  }
  case 15: {
    const auto r = d.ReadDiagnostics();
    CheckCommon(d, r, was_up, sent());
    break;
  }
  case 16: {
    const auto r = d.ReadErrorFlags();
    CheckCommon(d, r, was_up, sent());
    break;
  }
  case 17: {
    const auto r = d.ReadRegister(in.Word());
    CheckCommon(d, r, was_up, sent());
    CheckValue(r);
    Check(!r || r.value <= 0x3FFFU);
    break;
  }
  case 18: {
    const auto address = in.Word();
    const auto value = in.Word();
    const auto r = d.WriteRegister(address, value, FuzzVerify(in));
    CheckCommon(d, r, was_up, sent());
    if (address == as5047u::reg::kProg) {
      Check(r.error == Error::NotInitialized || r.error == Error::InvalidArgument);
    }
    break;
  }
  case 19: {
    const auto r = d.ReadNonVolatile();
    CheckCommon(d, r, was_up, sent());
    break;
  }
  case 20: {
    const bool confirmed = (in.Byte() & 0x01U) != 0U;
    as5047u::OtpReport report{};
    const auto r =
        d.ProgramOtp(confirmed ? as5047u::kOtpConfirm : as5047u::OtpBurnConfirm{in.Word()}, report);
    CheckCommon(d, r, was_up, sent());
    may_burn = confirmed && was_up;
    if (port.Chip().burn_starts != burns_before) {
      Check(report.burn_started); // the report never hides a burn
    }
    break;
  }
  default:
    FuzzChip(port.Chip(), in);
    port.SetBusUp((in.Byte() & 0x07U) != 0U);
    break;
  }

  if (!may_burn) {
    Check(port.Chip().burn_starts == burns_before);
  }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
  Input in(data, size);
  FuzzPort port(in);
  Driver driver(port, FuzzConfig(in));
  for (int calls = 0; calls < 64 && !in.Empty(); ++calls) {
    RunOne(driver, port, in);
  }
  return 0;
}
