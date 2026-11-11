/**
 * @file mock_as5047u_bus.hpp
 * @brief SPI mock ports over the chip model.
 *
 * `MockPortBase<Self>` implements the required hooks of the SPI port over a `MockBusState`. Derive
 * from it to build a port with any set of optional hooks:
 *
 * | Port | Hooks |
 * | --- | --- |
 * | `BarePort` | the bus hooks only (no `DelayUs()`: the driver does not compile with it) |
 * | `MinimalPort` | also `DelayUs()`, which every port needs |
 * | `FullPort` | also `Log()` |
 */
#ifndef AS5047U_MOCK_AS5047U_BUS_HPP
#define AS5047U_MOCK_AS5047U_BUS_HPP

#include <cstddef>
#include <cstdint>

#include "as5047u_spi_interface.hpp"
#include "as5047u_types.hpp"
#include "mock_as5047u.hpp"

namespace as5047u_test {

/// Shared state of the mock ports: the chip, fault injection and counters.
struct MockBusState {
  As5047uModel chip;
  bool bus_up_ok = true;    ///< EnsureInitialized() result.
  int fail_transfer = -1;   ///< Fail the transfer with this index (0-based); -1 never.
  int transfers = 0;        ///< Transfers so far.
  int bus_up_calls = 0;     ///< EnsureInitialized() calls so far.
  std::size_t last_len = 0; ///< Length of the last transfer.
  std::size_t shortest_len =
      0; ///< Shortest transfer since the last reset of this field (0 = none).

  bool DoTransfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    const int index = transfers++;
    last_len = len;
    shortest_len = (shortest_len == 0 || len < shortest_len) ? len : shortest_len;
    if (index == fail_transfer || len < 2 || len > 4 || tx == nullptr || rx == nullptr) {
      return false;
    }
    chip.Frame(tx, rx, len);
    return true;
  }
};

/// The required SPI hooks over a MockBusState. Derive `class P : public MockPortBase<P>`.
template <typename Self>
class MockPortBase : public as5047u::SpiInterface<Self> {
public:
  MockBusState state;

  bool EnsureInitialized() noexcept {
    state.bus_up_calls++;
    return state.bus_up_ok;
  }
  bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    return state.DoTransfer(tx, rx, len);
  }
};

/// Only the required hooks.
class BarePort : public MockPortBase<BarePort> {};

/// Required hooks plus the delay; time passes in the model as the driver waits.
class MinimalPort : public MockPortBase<MinimalPort> {
public:
  uint64_t delayed_us = 0; ///< Sum of DelayUs() arguments.

  void DelayUs(uint32_t us) noexcept {
    delayed_us += us;
    state.chip.Elapse(us);
  }
};

/// Every optional hook, with counters.
class FullPort : public MockPortBase<FullPort> {
public:
  uint64_t delayed_us = 0; ///< Sum of DelayUs() arguments.
  int log_lines = 0;       ///< Log() calls.
  int error_lines = 0;     ///< Log() calls at LogLevel::Error.

  void DelayUs(uint32_t us) noexcept {
    delayed_us += us;
    state.chip.Elapse(us);
  }
  void Log(as5047u::LogLevel level, const char* message) noexcept {
    (void)message;
    log_lines++;
    if (level == as5047u::LogLevel::Error) {
      error_lines++;
    }
  }
};

} // namespace as5047u_test

#endif // AS5047U_MOCK_AS5047U_BUS_HPP
