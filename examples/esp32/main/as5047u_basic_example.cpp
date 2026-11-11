/**
 * @file as5047u_basic_example.cpp
 * @brief Initialise the AS5047U from the menuconfig values and log angle, speed and AGC twice a
 * second.
 *
 * Wiring: examples/esp32/README.md. Expected output: one "initialised" line, then lines such as
 * "angle  123.45 deg  speed      0.0 rpm  AGC 110". Every Result is checked and logged by name.
 * Not verified on hardware.
 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "as5047u.hpp"
#include "as5047u_kconfig.hpp"
#include "esp32_as5047u_port.hpp"

namespace {
constexpr const char* kTag = "as5047u_example";
}

extern "C" void app_main(void) {
  static Esp32As5047uPort port; // static: the driver keeps a reference
  static as5047u::AS5047U<Esp32As5047uPort> sensor(port, as5047u::KconfigDefaults()); // no I/O

  ESP_LOGI(kTag, "AS5047U driver %s", as5047u::GetDriverVersion());
  const auto init = sensor.EnsureInitialized();
  if (!init) {
    ESP_LOGE(kTag, "EnsureInitialized failed: %s", as5047u::ToString(init.error));
    return;
  }
  // Twice a second: angle, speed and the magnet's health. A failed read is logged by name and
  // never shown as a value.
  while (true) {
    const auto angle = sensor.ReadAngleDegrees();
    const auto rpm = sensor.ReadVelocityRpm();
    const auto agc = sensor.ReadAgc();
    if (angle && rpm && agc) {
      ESP_LOGI(kTag, "angle %7.2f deg  speed %8.1f rpm  AGC %3u", static_cast<double>(angle.value),
               static_cast<double>(rpm.value), static_cast<unsigned>(agc.value));
    } else {
      const as5047u::Error e = !angle ? angle.error : (!rpm ? rpm.error : agc.error);
      ESP_LOGW(kTag, "read failed: %s", as5047u::ToString(e));
      if (e == as5047u::Error::Bus) {
        (void)sensor.Deinitialize(); // recover after a bus glitch: start again from scratch
        (void)sensor.EnsureInitialized();
      }
    }
    if (const auto flags = sensor.GetLatchedErrorFlags(); flags.agc_warning || flags.mag_half) {
      ESP_LOGW(kTag, "magnet too far or too close (AGC at its limit)");
    }
    sensor.ClearLatchedErrorFlags();
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
