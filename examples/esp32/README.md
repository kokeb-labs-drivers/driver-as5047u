# AS5047U ESP32 examples

ESP-IDF applications that run the AS5047U driver on an ESP32-C6, built by CI on every pull request.

## Wiring (ESP32-C6 DevKitC-1)

3.3 V mode: VDD and VDD3V3 tied, so the logic levels match the ESP32-C6.

| ESP32-C6 | AS5047U | Notes |
| --- | --- | --- |
| GPIO6 | CLK (pin 2) | SPI2, mode 1, 1 MHz in the example (up to 10 MHz) |
| GPIO7 | MOSI (pin 4) | |
| GPIO2 | MISO (pin 3) | |
| GPIO10 | CSn (pin 1) | Driven by spi_master: low for one frame, high between frames |
| 3V3 | VDD (pin 11) and VDD3V3 (pin 12) | 100 nF to GND at the pins |
| GND | GND (pin 13) and TEST (pin 5) | TEST must be grounded |

Centre a diametrically magnetised magnet over the chip ([Hardware](../../docs/hardware.md#magnet)). The pins are
in `Esp32As5047uPins`.

## Build and flash

```bash
git submodule update --init scripts      # the ESP-IDF build scripts
. $IDF_PATH/export.sh                     # ESP-IDF v5.5
./scripts/build_app.sh list
./scripts/build_app.sh as5047u_basic_example Debug
./scripts/flash_app.sh flash_monitor as5047u_basic_example Debug
```

`idf.py menuconfig` shows "AS5047U rotary position sensor" under Component config: the SPI frame format, the
direction and DAEC, which `as5047u::KconfigDefaults()` returns.

## Expected output

Not verified on hardware yet. The app logs one line twice a second; the values below are an example:

```text
I (312) as5047u_example: AS5047U driver 0.1.0
as5047u: initialised
I (820) as5047u_example: angle  123.45 deg  speed      0.0 rpm  AGC 110
```

An `Initialize failed: DeviceIdMismatch` line means no AS5047U answered: check CSn, SPI mode 1, the MISO wire and
TEST at GND first. A "magnet too far or too close" warning means the AGC is at a limit.

## Files

| File | What it is |
| --- | --- |
| [`main/as5047u_basic_example.cpp`](main/as5047u_basic_example.cpp) | The example app |
| [`main/esp32_as5047u_port.hpp`](main/esp32_as5047u_port.hpp) | The ESP-IDF port: bus (spi_master, mode 1), delay, log |
| [`CMakeLists.txt`](CMakeLists.txt) | The ESP-IDF project; `EXTRA_COMPONENT_DIRS` points at the repository root, which is the component |
| [`app_config.yml`](app_config.yml) | The app matrix for the scripts, CI and the docs |
