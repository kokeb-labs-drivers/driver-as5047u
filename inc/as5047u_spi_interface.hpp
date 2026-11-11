// clang-format off: the hook table below must keep one row per line.
/**
 * @file as5047u_spi_interface.hpp
 * @brief The SPI port of the AS5047U driver: the CRTP base, the `SpiPort` concept and the frame
 * layer.
 * @copyright Copyright (c) 2026 The driver-as5047u authors. SPDX-License-Identifier: MIT
 *
 * @details
 * The driver never calls an SDK. It calls hooks on a class you derive from
 * `as5047u::SpiInterface<Self>`:
 *
 * | Hook | Contract |
 * | --- | --- |
 * | `bool EnsureInitialized() noexcept` | Bring the bus up on the first call; `true` at once afterwards |
 * | `bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept` | One full-duplex frame, CS low for all `len` bytes |
 *
 * The port moves raw bytes: it never retries and never interprets a frame. What the chip needs from
 * the bus is below as `constexpr` values (`kSpiMode` to `kContinuousWord`, datasheet DS000637, "SPI
 * Interface (Slave)" and "SPI Timing", Figures 18 and 19); a port checks its settings against them
 * with `static_assert` (handbook KB-CPP-123). CS must go high between frames, because the chip
 * executes a frame on the rising CS edge and finds its length from the clocks while CS was low.
 * The hooks `DelayUs` (required) and `Log` (optional) are described in `as5047u.hpp`.
 */
// clang-format on
#ifndef AS5047U_SPI_INTERFACE_HPP
#define AS5047U_SPI_INTERFACE_HPP

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>

#include "as5047u_detail.hpp"
#include "as5047u_registers.hpp"
#include "as5047u_types.hpp"

namespace as5047u {

/**
 * @name Bus requirements
 * What the AS5047U needs from the SPI bus (datasheet "SPI Interface (Slave)", "SPI Timing",
 * Figures 18 and 19, and "SPI Transaction", Figures 20 to 28). A port and a board configuration
 * check their settings against these with `static_assert` (handbook KB-CPP-123).
 * @{
 */
/// SPI mode 1: CPOL 0, CPHA 1. The chip samples MOSI on the falling edge of CLK.
inline constexpr uint8_t kSpiMode = 1;
/// Each frame is clocked most significant bit first.
inline constexpr bool kMsbFirst = true;
/// Highest CLK frequency: t_clk min 100 ns, so 10 MHz.
inline constexpr uint32_t kMaxClockHz = 10'000'000;
/// Shortest time CS stays high between two frames, t_CSn (2 t_CLK_SYS + 10 ns with the internal
/// clock at its 9 MHz typical; give margin on a slow internal clock).
inline constexpr uint32_t kMinCsHighNs = 350;
/// CS falling edge to the first CLK rising edge, t_L.
inline constexpr uint32_t kCsSetupNs = 350;
/// Last CLK falling edge to CS rising edge, t_H: t_clk / 2, so 50 ns at `kMaxClockHz`.
inline constexpr uint32_t kCsHoldNs = 50;
/// `false`: the chip counts clocks while CS is low and executes the frame at the rising CS edge,
/// so idle CLK time between the bytes of one frame is allowed. CS must not rise inside a frame.
inline constexpr bool kContinuousWord = false;
/// Frame lengths in bits the chip accepts: 16 (reads only, no CRC), 24 and 32 (Figures 20, 23, 26).
inline constexpr std::array<uint8_t, 3> kFrameBits = {16, 24, 32};
/** @} */

/**
 * @brief CRTP base of the platform SPI port.
 *
 * Derive `class MyPort : public as5047u::SpiInterface<MyPort>` and implement the hooks in this
 * file's description. The base declares no hook itself (a forwarding hook would turn a missing
 * override into infinite recursion) and no data; the driver checks your class with the `SpiPort`
 * concept, so a missing or misdeclared hook is a compile error that names it.
 *
 * @tparam Derived Your port class.
 */
template <typename Derived>
class SpiInterface {
public:
  /** @brief Ports are not copied: the driver holds a reference. */
  SpiInterface(const SpiInterface&) = delete;
  /** @brief Ports are not copied: the driver holds a reference. */
  SpiInterface& operator=(const SpiInterface&) = delete;

protected:
  /** @brief Only the derived port is constructed. */
  SpiInterface() noexcept = default;
  /** @brief Destroyed through the derived port only. */
  ~SpiInterface() noexcept = default;
};

/**
 * @brief Satisfied by a class that derives from `SpiInterface<Port>` and implements the required
 * hooks with exactly the types above, `noexcept` and returning `bool`.
 *
 * Each hook's address is matched with `detail::ExactHook`, which takes a member function of exactly
 * the signature and converts no value: a hook declared with a narrower length (`uint16_t len`) or
 * without `noexcept` does not satisfy the concept, instead of truncating every transfer silently
 * (KB-CPP20-006).
 *
 * @tparam Port The port class.
 */
template <typename Port>
concept SpiPort = std::derived_from<Port, SpiInterface<Port>> && requires {
  detail::ExactHook<Port, bool() noexcept>::Take(&Port::EnsureInitialized);
  detail::ExactHook<Port, bool(const uint8_t*, uint8_t*, std::size_t) noexcept>::Take(
      &Port::Transfer);
};

namespace detail {

/// One CRC-8 step over a byte, bit by bit: the reference the table is built from and checked
/// against (datasheet "CRC Checksum", Figure 31).
constexpr uint32_t Crc8Step(uint32_t crc, uint32_t byte) noexcept {
  uint32_t c = (crc ^ byte) & 0xFFU;
  for (uint32_t bit = 0U; bit < 8U; ++bit) {
    c = ((c & 0x80U) != 0U) ? (((c << 1U) ^ reg::kCrcPolynomial) & 0xFFU) : ((c << 1U) & 0xFFU);
  }
  return c;
}

/// CRC-8 of a word, bit by bit; `Crc8()` must agree with it for every word.
constexpr uint8_t Crc8Bitwise(uint16_t word) noexcept {
  const uint32_t high =
      Crc8Step(static_cast<uint32_t>(reg::kCrcInit), static_cast<uint32_t>(word) >> 8U);
  const uint32_t low = Crc8Step(high, static_cast<uint32_t>(word) & 0xFFU);
  return static_cast<uint8_t>(low ^ reg::kCrcFinalXor);
}

/// The 256 one-byte CRC-8 steps, built at compile time (256 bytes of read-only data).
constexpr std::array<uint8_t, 256> MakeCrc8Table() noexcept {
  std::array<uint8_t, 256> table{};
  for (uint32_t i = 0U; i < 256U; ++i) {
    table[i] = static_cast<uint8_t>(Crc8Step(0_u32, i));
  }
  return table;
}

/// CRC-8 lookup table. Two lookups per frame word instead of 16 shift-and-XOR steps: about three
/// times faster per word on the host (docs/architecture.md, "CRC"), for 256 bytes of flash.
inline constexpr std::array<uint8_t, 256> kCrc8Table = MakeCrc8Table();

} // namespace detail

/**
 * @brief CRC-8 of a 16-bit frame word: polynomial 0x1D, initial value 0xC4, final XOR 0xFF, not
 * reflected (datasheet "CRC Checksum", Figure 31). Table-driven; equal to the bitwise definition
 * for every word (tests/test_as5047u.cpp).
 * @param word The word the CRC protects (command or data, MOSI or MISO).
 * @return The CRC byte.
 */
constexpr uint8_t Crc8(uint16_t word) noexcept {
  const auto high = static_cast<uint32_t>(detail::kCrc8Table[static_cast<std::size_t>(
      (reg::kCrcInit ^ (static_cast<uint32_t>(word) >> 8U)) & 0xFFU)]);
  const auto low = static_cast<uint32_t>(
      detail::kCrc8Table[static_cast<std::size_t>((high ^ static_cast<uint32_t>(word)) & 0xFFU)]);
  return static_cast<uint8_t>(low ^ reg::kCrcFinalXor);
}

namespace detail {

static_assert((Crc8(0x0000_u16) == 0xF1U) && (Crc8(0xFFFF_u16) == 0x74U),
              "CRC-8 of the line patterns");
static_assert((Crc8(0x3FFF_u16) == Crc8Bitwise(0x3FFF_u16)) &&
                  (Crc8(0x4001_u16) == Crc8Bitwise(0x4001_u16)),
              "the table agrees with the bitwise CRC");

/// Longest frame: 32 bits.
inline constexpr std::size_t kMaxFrameBytes = 4U;

/// The frame length in bytes of a format.
constexpr std::size_t FrameBytes(FrameFormat format) noexcept {
  switch (format) {
  case FrameFormat::Bits16:
    return 2U;
  case FrameFormat::Bits24:
    return 3U;
  case FrameFormat::Bits32:
    return 4U;
  default:
    break;
  }
  return 3U;
}
static_assert(((FrameBytes(FrameFormat::Bits16) * 8U) == kFrameBits[0]) &&
              ((FrameBytes(FrameFormat::Bits24) * 8U) == kFrameBits[1]) &&
              ((FrameBytes(FrameFormat::Bits32) * 8U) == kFrameBits[2]));

/// True when a 24- or 32-bit reply is all zeros or all ones (the pad byte of a 32-bit frame aside):
/// what MISO stuck low, or held high by a pull-up with no chip driving it, reads. Straight-line, so
/// it costs the hot path nothing (it runs only after a CRC mismatch).
constexpr bool IsLinePattern(uint16_t reply, uint8_t crc) noexcept {
  return ((reply == 0x0000U) && (crc == 0x00U)) || ((reply == 0xFFFFU) && (crc == 0xFFU));
}
static_assert(IsLinePattern(0x0000_u16, 0x00_u8) && IsLinePattern(0xFFFF_u16, 0xFF_u8) &&
              !IsLinePattern(0x0000_u16, 0xFF_u8) && !IsLinePattern(0x4000_u16, 0x00_u8));

/// One SPI frame at a time, in any format (datasheet "SPI Transaction", Figures 20 to 28):
/// - 16-bit: MOSI word; MISO warning, error, 14 data bits.
/// - 24-bit: the same word followed by its CRC-8, in both directions.
/// - 32-bit: MOSI pad byte, then the 24-bit frame; MISO the 24-bit frame, then the pad byte.
template <SpiPort Port>
class FrameBus {
public:
  explicit FrameBus(Port& port) noexcept : port_(port) {}

  Port& GetPort() & noexcept {
    return port_;
  }

  /// Send `word`; return the reply word (warning, error, 14 data bits). `Bus` when the transfer
  /// fails; `Crc` when a CRC-protected reply has a wrong CRC, `NoDevice` when that reply is all
  /// zeros or all ones (a stuck MISO line, KB-CPP-116).
  ///
  /// A write is two frames, the command and then the data. After a failed transfer the driver
  /// cannot know whether the chip received the frame, so the chip may still be waiting for the data
  /// of a write; the next command would then be stored as that data (a read command written into
  /// PROG sets PROGOTP and burns the OTP). So the first frame after a failure is a read of NOP:
  /// taken as a command it reads nothing; taken as data it writes 0, which clears PROG.
  Result<uint16_t> Exchange(FrameFormat format, uint8_t pad, uint16_t word) noexcept {
    if (resync_) {
      (void)transfer(format, pad, kResyncWord); // its reply is the previous command's; unused
      if (resync_) {
        return Result<uint16_t>::Failure(Error::Bus); // still failing: send nothing else
      }
    }
    return transfer(format, pad, word);
  }

  /// True when the last transfer failed and the next frame will be preceded by a NOP read.
  [[nodiscard]] bool NeedsResync() const noexcept {
    return resync_;
  }

  /// The reply word of the last frame that was transferred, flag bits included, whatever its CRC
  /// said; 0 before the first. No I/O (KB-CPP-129).
  [[nodiscard]] uint16_t LastReply() const noexcept {
    return last_reply_;
  }

private:
  /// Read of NOP (R/W = 1, address 0x0000); as a data word its 14 data bits are 0.
  static constexpr uint16_t kResyncWord = 0x4000U;

  Result<uint16_t> transfer(FrameFormat format, uint8_t pad, uint16_t word) noexcept {
    std::array<uint8_t, kMaxFrameBytes> tx{};
    std::array<uint8_t, kMaxFrameBytes> rx{};
    const std::size_t len = FrameBytes(format);
    std::size_t at = 0U;
    if (format == FrameFormat::Bits32) {
      tx[0] = pad;
      at = 1U;
    }
    tx[at] = static_cast<uint8_t>(static_cast<uint32_t>(word) >> 8U);
    tx[at + 1U] = static_cast<uint8_t>(static_cast<uint32_t>(word) & 0xFFU);
    if (format != FrameFormat::Bits16) {
      tx[at + 2U] = Crc8(word);
    }
    resync_ = !port_.Transfer(tx.data(), rx.data(), len);
    if (resync_) {
      return Result<uint16_t>::Failure(Error::Bus);
    }
    const auto reply =
        static_cast<uint16_t>((static_cast<uint32_t>(rx[0]) << 8U) | static_cast<uint32_t>(rx[1]));
    last_reply_ = reply;
    if ((format != FrameFormat::Bits16) && (rx[2] != Crc8(reply))) {
      return Result<uint16_t>::Failure(IsLinePattern(reply, rx[2]) ? Error::NoDevice : Error::Crc);
    }
    return Result<uint16_t>::Success(reply);
  }

  Port& port_;
  uint16_t last_reply_ = 0;
  bool resync_ = false;
};

} // namespace detail

} // namespace as5047u

#endif // AS5047U_SPI_INTERFACE_HPP
