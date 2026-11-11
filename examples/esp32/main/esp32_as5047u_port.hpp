/**
 * @file esp32_as5047u_port.hpp
 * @brief ESP-IDF port for the AS5047U driver: spi_master (mode 1) for the bus, esp_rom
 * delays and ESP_LOG for the log hook.
 *
 * Start from this file for any ESP-IDF target. The pins are in Esp32As5047uPins; the defaults match
 * the wiring table in examples/esp32/README.md (ESP32-C6 DevKitC-1).
 */
#ifndef AS5047U_ESP32_AS5047U_PORT_HPP
#define AS5047U_ESP32_AS5047U_PORT_HPP

#include <cstddef>
#include <cstdint>

#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include "as5047u.hpp"

/// The port's default SPI clock: 1 MHz for bring-up on jumper wires.
inline constexpr int kEsp32As5047uClockHz = 1'000'000;
/// The port's SPI mode.
inline constexpr int kEsp32As5047uSpiMode = 1;
// The bus requirements of the driver (handbook KB-CPP-123), checked at compile time.
static_assert(kEsp32As5047uClockHz <= static_cast<int>(as5047u::kMaxClockHz),
              "the default clock is above the AS5047U's 10 MHz");
static_assert(kEsp32As5047uSpiMode == as5047u::kSpiMode, "the AS5047U uses SPI mode 1");
static_assert(as5047u::kMsbFirst, "this port sends MSB first (no SPI_DEVICE_BIT_LSBFIRST)");
// t_L, t_H and t_CSn (kCsSetupNs, kCsHoldNs, kMinCsHighNs) depend on the spi_master CS timing and
// are not checked here (not verified on hardware): measure CS against CLK with a scope at bring-up.

/// Pins and bus settings of the ESP-IDF port.
struct Esp32As5047uPins {
  spi_host_device_t host = SPI2_HOST;
  int sclk = 6;
  int mosi = 7;
  int miso = 2;
  int cs = 10;
  int clock_hz = kEsp32As5047uClockHz; ///< Up to `as5047u::kMaxClockHz`; checked at start.
};

/// The AS5047U SPI port on ESP-IDF, with every optional hook. CS is driven by the SPI
/// peripheral, so it idles high before the first frame and goes high between frames as the chip
/// requires (handbook KB-HW-018). The chip has no other control pin.
class Esp32As5047uPort : public as5047u::SpiInterface<Esp32As5047uPort> {
public:
  explicit Esp32As5047uPort(const Esp32As5047uPins& pins = {}) noexcept : pins_(pins) {}

  bool EnsureInitialized() noexcept {
    if (device_ != nullptr) {
      return true;
    }
    if (pins_.clock_hz <= 0 || pins_.clock_hz > static_cast<int>(as5047u::kMaxClockHz)) {
      ESP_LOGE(kTag, "clock outside the AS5047U's bus requirements");
      return false;
    }
    spi_bus_config_t bus{};
    bus.mosi_io_num = pins_.mosi;
    bus.miso_io_num = pins_.miso;
    bus.sclk_io_num = pins_.sclk;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 16;
    const esp_err_t bus_err = spi_bus_initialize(pins_.host, &bus, SPI_DMA_DISABLED);
    if (bus_err != ESP_OK && bus_err != ESP_ERR_INVALID_STATE) { // INVALID_STATE: bus already up
      ESP_LOGE(kTag, "spi_bus_initialize: %s", esp_err_to_name(bus_err));
      return false;
    }
    spi_device_interface_config_t dev{};
    dev.clock_speed_hz = pins_.clock_hz;
    dev.mode = kEsp32As5047uSpiMode; // the AS5047U samples on the falling edge: CPOL 0, CPHA 1
    dev.spics_io_num = pins_.cs;
    dev.queue_size = 1;
    if (spi_bus_add_device(pins_.host, &dev, &device_) != ESP_OK) {
      device_ = nullptr;
      return false;
    }
    return true;
  }

  bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept {
    if (device_ == nullptr || len == 0 || rx == nullptr) {
      return false;
    }
    spi_transaction_t t{};
    t.length = len * 8U;
    t.tx_buffer = tx;
    t.rx_buffer = rx; // every frame is full duplex: the reply answers the previous frame
    return spi_device_polling_transmit(device_, &t) == ESP_OK;
  }

  void DelayUs(uint32_t us) noexcept {
    esp_rom_delay_us(us);
  }

  void Log(as5047u::LogLevel level, const char* message) noexcept {
    const esp_log_level_t esp_level = level == as5047u::LogLevel::Error     ? ESP_LOG_ERROR
                                      : level == as5047u::LogLevel::Warning ? ESP_LOG_WARN
                                      : level == as5047u::LogLevel::Info    ? ESP_LOG_INFO
                                                                            : ESP_LOG_DEBUG;
    esp_log_write(esp_level, kTag, "%s\n", message); // plain text, never a format
  }

private:
  static constexpr const char* kTag = "as5047u";
  Esp32As5047uPins pins_;
  spi_device_handle_t device_ = nullptr;
};

#endif // AS5047U_ESP32_AS5047U_PORT_HPP
