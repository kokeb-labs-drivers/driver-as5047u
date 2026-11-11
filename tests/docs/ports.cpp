/**
 * @file ports.cpp
 * @brief The port with every optional hook, as shown on the Porting page. Compiled, never run.
 */
#include <cstddef>
#include <cstdint>

#include "as5047u.hpp"

bool platform_spi_init() noexcept;
bool platform_spi_transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept;
void platform_delay_us(uint32_t us) noexcept;
void platform_log(const char* line) noexcept;

// --8<-- [start:full-port]
class FullPort : public as5047u::SpiInterface<FullPort> {
public:
  bool EnsureInitialized() noexcept {
    return platform_spi_init();
  }
  bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    return platform_spi_transfer(tx, rx, len);
  }

  // Required: start-up polls DIA, and ProgramOtp() the burn, once per millisecond.
  void DelayUs(uint32_t us) noexcept {
    platform_delay_us(us);
  }

  // Optional: one line per lifecycle event or failure. The driver never formats; the port does.
  void Log(as5047u::LogLevel level, const char* message) noexcept {
    if (level > as5047u::LogLevel::Warning) {
      return;
    }
    platform_log(message); // plain text: never use it as a format string
  }
};
// --8<-- [end:full-port]
