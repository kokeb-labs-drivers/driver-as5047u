# AS5047U Driver

C++20 driver for the AS5047U 14-bit magnetic rotary position sensor.

[![CI](https://github.com/kokeb-labs-drivers/driver-as5047u/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/kokeb-labs-drivers/driver-as5047u/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-informational)](docs/installation.md#requirements)

**Documentation: <https://kokeb-labs-drivers.github.io/driver-as5047u/>**, with the API reference built by Doxygen under
[`api/`](https://kokeb-labs-drivers.github.io/driver-as5047u/latest/api/). The same pages are readable here in
[`docs/`](docs/index.md).

The AS5047U measures the absolute angle of a magnet spinning above it, 16384 steps per turn, and its speed. This
driver reads them over SPI with a CRC on every frame, turns the chip's error flags into errors you can act on, sets
the zero, the direction and the encoder outputs, and burns the settings permanently when you ask it to. It runs on
any MCU: you give it a small class that moves bytes on your SPI bus, and it does the rest.

<!-- figure: driver-layers -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/diagrams/driver-layers.dark.svg">
  <img src="docs/diagrams/driver-layers.svg"
    alt="Layer diagram: your application calls the AS5047U core, which sends register reads and writes
    through the SPI port contract to the port class you write, which drives the chip; the core and the
    contract are the same on every MCU.">
</picture>
<!-- /figure -->

## At a glance

| | |
| --- | --- |
| **Chip** | ams OSRAM AS5047U, TSSOP-14, 3.3 V or 5 V |
| **Measures** | Angle over 360° in 14 bits (0.022° per count); velocity at 24.141 °/s per count |
| **Bus** | SPI mode 1, up to 10 MHz; 16-bit frames, or 24- and 32-bit frames with CRC-8 (the default) |
| **Also drives** | ABI quadrature, UVW commutation and PWM outputs; one-time-programmable (OTP) settings |
| **Driver** | Header-only C++20; no heap, exceptions, RTTI or virtual calls; every call returns `Result<T>` |
| **Runs on** | Any MCU with SPI and a C++20 compiler; examples for ESP32 (ESP-IDF) and the host |
| **Status** | 0.1.0, not yet verified on hardware: host-tested against a frame-level model of the chip ([limitations](docs/limitations.md)) |

## Datasheet

<!-- generated: datasheet-card -->
| Document | Revision | Get it |
| --- | --- | --- |
| Datasheet: AS5047U High Resolution Rotary Position Sensor (ams OSRAM DS000637) | v1-01, 2020-12-16 | [vendor page](https://ams-osram.com/products/sensor-solutions/position-sensors/ams-as5047u-high-resolution-rotary-position-sensor) |

Written against these revisions; the SHA-256 of each file is in [`docs/datasheets.yml`](docs/datasheets.yml). Where each
part of the driver is in the datasheet: [Datasheet map](docs/datasheet.md).
<!-- /generated -->

Every docs page lists the datasheet sections it relies on, and code comments cite the section they implement.

## Try it on your PC

No hardware needed: [`examples/host`](examples/host/README.md) runs the real driver against a simulated chip. You need
git, CMake 3.16+ and a C++20 compiler (GCC 10+, Clang 13+ or MSVC 2022).

1. Get the source:

   ```bash
   git clone https://github.com/kokeb-labs-drivers/driver-as5047u.git && cd driver-as5047u
   ```

2. Build and run the example:

   ```bash
   cmake -S examples/host -B build-example
   cmake --build build-example
   ./build-example/as5047u_host_example
   ```

3. You should see the simulated shaft turn in 30° steps:

   ```text
   AS5047U driver 0.1.0 on the host
   as5047u: initialised
   angle    0.00 deg  AGC 128
   angle   29.99 deg  AGC 128
   ```

To run the whole test suite instead: `cmake -S . -B build -DAS5047U_BUILD_TESTS=ON && cmake --build build && ctest
--test-dir build`.

## Use it on your MCU

**1. Wire it** (3.3 V; [Hardware](docs/hardware.md) for 5 V, the reference circuit and the magnet):

| MCU | AS5047U |
| --- | --- |
| SPI SCLK, MOSI, MISO | CLK (pin 2), MOSI (pin 4), MISO (pin 3) |
| Chip select | CSn (pin 1): low for one frame, high between frames |
| 3.3 V | VDD (pin 11) and VDD3V3 (pin 12), 100 nF to GND |
| GND | GND (pin 13) and TEST (pin 5) |

**2. Write the port**: a class with `EnsureInitialized()` (bring the bus up) and `Transfer()` (one full-duplex
frame), about 20 lines on most SDKs. [Porting](docs/porting.md) has the contract and a complete example.

**3. Read the angle:**

<!-- generated: quickstart-code -->
```cpp
#include "as5047u.hpp"

int read_shaft_angle(MyPort& port, float& degrees) {
  as5047u::AS5047U<MyPort> sensor(port); // no I/O yet

  if (auto r = sensor.EnsureInitialized(); !r) { // 1. bus up, wait for ready, Config{} written
    return static_cast<int>(r.error);            //    Bus or NoDevice: check wiring, SPI mode 1
  }
  const auto a = sensor.ReadAngleDegrees(); // 2. compensated angle, CRC-checked frame
  if (!a) {
    return static_cast<int>(a.error); //    MagnetWarning: magnet missing; DeviceFault: chip
  }
  degrees = a.value; // 3. [0, 360), 14-bit resolution (0.022 degrees)
  return 0;
}
```
<!-- /generated -->

`EnsureInitialized()` is the only call that starts the chip. Every other call returns `Error::NotInitialized`
until it has succeeded, and no read ever starts the chip on its own, so a read is always just its own frames.

## What it does

- Waits for the chip to report ready (DIA, confirmed by a second read, with a deadline from `Config`) before it
  writes anything, then writes the settings and reads each one back. A stuck, floating or looped-back line is
  `NoDevice`, never a value.
- Reads the angle (counts, degrees, radians, with or without dynamic compensation), the velocity (counts, °/s,
  rpm), the AGC and the magnitude, the diagnostic and error registers.
- Resolves the error bit of every reply through the chip's ERRFL register into `Crc`, `Framing`, `Command` or
  `DeviceFault`; an angle under a magnet warning is refused with `MagnetWarning`. Every ERRFL value it reads is kept
  in a latch (`GetLatchedErrorFlags()`), and the angle read stays two frames when the reply carries no flag.
- Sets the zero (`SetZeroHere()`), the direction and the ABI, UVW and PWM outputs from one `Config`, whose
  defaults are the chip's reset values.
- Burns the settings into the OTP with the datasheet's step-by-step procedure, guarded by a confirmation value and
  reporting the step that failed.
- States its bus requirements as `constexpr` values (`kSpiMode`, `kMaxClockHz`, ...) for a port to `static_assert`.

## Add it to your project

Pick the row that matches your build; each is a few steps, and [Installation](docs/installation.md) has the details.

| Your build | Steps below |
| --- | --- |
| Plain CMake: STM32 (CubeMX), RP2040 (Pico SDK), nRF Connect, a host app | [A](#a-cmake-any-mcu) |
| ESP-IDF | [B](#b-esp-idf) |
| An SDK or OS image that installs libraries | [C](#c-installed-package) |

### A. CMake, any MCU

1. Add the driver as a submodule, pinned to the version you chose:

   ```bash
   git submodule add https://github.com/kokeb-labs-drivers/driver-as5047u.git external/driver-as5047u
   ```

2. In your `CMakeLists.txt`, after `project()`:

   ```cmake
   add_subdirectory(external/driver-as5047u)
   target_link_libraries(my_app PRIVATE as5047u::as5047u)   # brings the headers and C++20
   ```

3. In your code, `#include "as5047u.hpp"`, write the port ([Porting](docs/porting.md)) and build. A first build
   that compiles the quickstart above is the check that the driver is wired in.

### B. ESP-IDF

1. Add the driver under your project's `components/`; ESP-IDF finds it there and names it after the folder:

   ```bash
   git submodule add https://github.com/kokeb-labs-drivers/driver-as5047u.git components/as5047u
   ```

2. In `main/CMakeLists.txt`, list it: `idf_component_register(SRCS "main.cpp" REQUIRES as5047u)`.
3. `idf.py menuconfig` now has an **AS5047U rotary position sensor** menu (frame format, direction, DAEC), and
   `idf.py build` builds the driver with your app.
   [`examples/esp32`](examples/esp32) is a complete project with an SPI port.

### C. Installed package

1. Install it once into a prefix:

   ```bash
   cmake -S external/driver-as5047u -B build-as5047u && cmake --install build-as5047u --prefix /opt/sdk
   ```

2. In your project, with `CMAKE_PREFIX_PATH=/opt/sdk`:

   ```cmake
   find_package(as5047u 0.1 REQUIRED)
   target_link_libraries(my_app PRIVATE as5047u::as5047u)
   ```

The target is `as5047u::as5047u` every way, so switching later changes nothing else.

## Documentation

| Page | Read it when you want to |
| --- | --- |
| [Quickstart](docs/quickstart.md) | Go from a bare chip to an angle on your MCU |
| [Hardware](docs/hardware.md) | Wire it, choose 3.3 V or 5 V, place the magnet, understand the SPI frames |
| [Porting](docs/porting.md) | Write the port class for your MCU's SDK |
| [API guide](docs/api-guide.md) | Use each call: start, measure, set the zero, watch the magnet, handle errors |
| [Configuration](docs/configuration.md) | Choose every setting, or keep a burned part's settings |
| [OTP programming](docs/otp.md) | Make the zero and the settings permanent |
| [Troubleshooting](docs/troubleshooting.md) | Find the cause of an error or a wrong reading |
| [Contract](docs/contract.md) | Know exactly what the driver promises, needs and returns |
| [Status and limitations](docs/limitations.md) | Decide whether it is ready for your use: what is and is not verified |
| [Datasheet map](docs/datasheet.md) | Find the datasheet section behind each part of the driver |
| [API reference](docs/api-reference.md) | Look up a class or member (Doxygen; built by CI and locally) |

## Tests and quality

```bash
cmake -S . -B build -DAS5047U_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build
```

The host tests run the driver against a model of the chip's SPI protocol, fail every bus transfer of every operation in
turn, and produce each error the driver can return. A fuzzer corrupts, drops and replaces the chip's replies and checks
the driver's promises after every call, among them that no lost frame can ever burn the OTP. CI also builds for
Cortex-M0+ to M33F and ESP32, and gates coverage, complexity, sanitizers, fuzzing, clang-tidy, cppcheck and the [Kokeb
Labs handbook](https://github.com/kokeb-labs/handbook) rules. See [Testing](docs/testing.md), which lists every gate and
its limit.

## License

MIT. See [LICENSE](LICENSE).
