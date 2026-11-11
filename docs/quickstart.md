# Quickstart

Run the driver on your PC first, with no hardware; then read the shaft angle from your MCU: wire the chip, place the
magnet, write a small port class, call two driver functions.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From ams OSRAM DS000637 v1-01:

    - Pins and package: Pin Assignment, p. 3
    - Reference circuit: Circuit Diagram, p. 44

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Try it on your PC

[`examples/host/`](../examples/host/README.md) runs the real driver against a simulated AS5047U, through a host
port. From the repository root, with CMake and a C++20 compiler:

```bash
cmake -S examples/host -B build-example
cmake --build build-example
./build-example/as5047u_host_example
```

```text
AS5047U driver 0.1.0 on the host
as5047u: initialised
angle    0.00 deg  AGC 128
angle   29.99 deg  AGC 128
angle   59.99 deg  AGC 128
angle   90.00 deg  AGC 128
```

The calls in [`main.cpp`](../examples/host/main.cpp) are the ones below; only the port differs on an MCU. CI builds
and runs this example on every pull request.

## 1. Wire it

3.3 V mode: VDD and VDD3V3 tied together, so the logic levels match a 3.3 V MCU.

| MCU | AS5047U | Notes |
| --- | --- | --- |
| SPI SCLK | CLK (pin 2) | Mode 1 (CPOL = 0, CPHA = 1), up to 10 MHz; 1 MHz for bring-up on jumper wires |
| SPI MOSI | MOSI (pin 4) | |
| SPI MISO | MISO (pin 3) | |
| Chip select | CSn (pin 1) | Low for one frame, high between frames: one transfer per frame |
| 3.3 V | VDD (pin 11) and VDD3V3 (pin 12) | 3.0 V to 3.6 V; 100 nF to GND close to the pins |
| GND | GND (pin 13) and TEST (pin 5) | TEST must always be grounded |

Leave A, B, I, U, V and W open for this example. Centre a diametrically magnetised magnet (the datasheet's
reference is 8 mm × 3 mm) over the package, close enough for 35 mT to 70 mT at the die. The [Hardware](hardware.md)
page has the full pin list, the 5 V mode, the timing and the magnet limits.

## 2. Add the driver

Add the repository as a submodule and link the `as5047u::as5047u` CMake target. [Installation](installation.md)
covers CMake on any MCU, the installed package and ESP-IDF.

```bash
git submodule add https://github.com/kokeb-labs-drivers/driver-as5047u.git external/driver-as5047u
```

## 3. Write the port

The port is the only platform code: it brings the bus up and moves bytes. Replace the `platform_*` calls with your
SDK's.

```cpp title="my_port.hpp"
--8<-- "tests/docs/my_port.hpp:port"
```

[Porting](porting.md) explains each method, the optional hooks and the transfer rules.

## 4. Read the angle

```cpp title="main.cpp"
--8<-- "tests/docs/quickstart.cpp:quickstart"
```

What each step does:

1. `EnsureInitialized()` calls your port's `EnsureInitialized()`, clears ERRFL, checks that an AS5047U answers on MISO,
   and writes `Config{}`, the reset values of an unprogrammed part: 24-bit frames with CRC, clockwise, zero at 0,
   DAEC on.
2. `ReadAngleDegrees()` reads ANGLECOM in two frames and checks the reply's CRC and error bit.
3. The value is in degrees, [0, 360), at 14-bit resolution: 360° / 16384 = 0.022° per count.

This example is built by the host CI, so it compiles against the current headers. It is **not verified on
hardware** yet.

## If it does not work

| Symptom | First thing to check |
| --- | --- |
| `EnsureInitialized()` returns `Bus` | Your port's `EnsureInitialized()`: the bus did not come up |
| `EnsureInitialized()` returns `NoDevice`, `NotReady` or `DeviceIdMismatch` | MISO, MOSI, CSn, SPI mode 1, the supply and the magnet ([Troubleshooting](troubleshooting.md#ensureinitialized-returns-nodevice-notready-or-deviceidmismatch)) |
| `Crc` or `Framing` from any call | Clock too fast for the wiring, or CSn not going high between frames |
| `ReadAngleDegrees()` returns `DeviceFault` | No magnet, or a chip fault: read `ReadErrorFlags()` and `ReadDiagnostics()` |
| The angle does not follow the magnet | Magnet off-centre or too far: check `ReadAgc()` ([Troubleshooting](troubleshooting.md#agc-at-0-or-255)) |

## Next

- [API guide](api-guide.md): velocity, zero position, magnet health, errors.
- [Configuration](configuration.md): every `Config` field, the ABI, UVW and PWM outputs, Kconfig.
- [OTP programming](otp.md): make the zero position and the settings permanent.
- [Examples](examples.md): the host example and the MCU examples.
- [Platforms](platforms.md): which MCUs have a port and an example.
