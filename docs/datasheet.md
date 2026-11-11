# Datasheet map

The driver follows the AS5047U datasheet. This page says which revision, where to get it, and where each part of
the driver is described in it, so you can check any behaviour against the source. Section names and page numbers
are the datasheet's own (the printed page numbers).

<!-- generated: datasheet-map -->
- **Datasheet: AS5047U High Resolution Rotary Position Sensor**, ams OSRAM DS000637, revision v1-01 (2020-12-16). Get it
  from the [vendor
  page](https://ams-osram.com/products/sensor-solutions/position-sensors/ams-as5047u-high-resolution-rotary-position-sensor).
  SHA-256 of the file used: `715ae24904c1cb1a7b87c282249d302559fba9894a5d88267f927b4f0b800ba6`.

| Topic | Datasheet section | Page | In the driver | Explained on |
| --- | --- | --- | --- | --- |
| Pins and package | Pin Assignment | p. 3 | - | [Hardware](hardware.md), [Quickstart](quickstart.md) |
| Supply voltage, 3.3 V and 5 V operation | Electrical Characteristics; Power Management | p. 6 | `Config::high_temperature_noise` | [Hardware](hardware.md) |
| Power-up time and SPI timing | Timing Characteristics; SPI Timing | p. 9 | - | [Hardware](hardware.md), [Porting](porting.md), [Troubleshooting](troubleshooting.md) |
| Dynamic angle error compensation (DAEC) | Dynamic Angle Error Compensation | p. 12 | `Config::dynamic_compensation`, `ReadUncompensatedAngleCounts()` | [Configuration](configuration.md) |
| Adaptive velocity filter | Adaptive Filter System; Rotation Speed Measurement | p. 12 | `Config::adaptive_filter`, `filter_k_min_code`, `filter_k_max_code`, `ReadVelocityDegPerSec()` | [Configuration](configuration.md), [API guide](api-guide.md) |
| SPI mode, frames and the read pipeline | SPI Interface (Slave); SPI Transaction | p. 16 | `FrameFormat`, `SpiInterface`, every read and write | [Hardware](hardware.md), [Porting](porting.md), [Architecture](architecture.md) |
| Pad byte of 32-bit frames | PAD Word | p. 22 | `Config::pad_byte` | [Hardware](hardware.md) |
| CRC-8 of a frame | CRC Checksum | p. 23 | `Crc8()`, `Error::Crc` | [Hardware](hardware.md), [Troubleshooting](troubleshooting.md) |
| Angle, velocity, AGC, magnitude, ERRFL and DIA registers | Volatile Registers | p. 24 | `ReadAngleCounts()`, `ReadVelocityCounts()`, `ReadAgc()`, `ReadMagnitude()`, `ReadErrorFlags()`, `ReadDiagnostics()`, `ErrorFlags::flags_lost` | [API guide](api-guide.md), [Troubleshooting](troubleshooting.md) |
| Settings, zero position and direction registers | Non-Volatile Registers (OTP) | p. 27 | `Config`, `ApplyConfig()`, `SetZeroPosition()`, `SetDirection()`, `CheckForReset()`, `Error::DeviceReset` | [Configuration](configuration.md), [API guide](api-guide.md), [Status and limitations](limitations.md) |
| ABI incremental output | ABI Incremental Interface | p. 31 | `Config::abi_enable`, `abi_resolution_code`, `abi_decimal`, `index_width` | [Configuration](configuration.md) |
| UVW commutation output | UVW Commutation Interface | p. 33 | `Config::uvw_enable`, `uvw_pole_pairs`, `incremental` | [Configuration](configuration.md) |
| PWM output and hysteresis | PWM; Incremental Output Hysteresis | p. 35 | `Config::pwm_enable`, `hysteresis_code` | [Configuration](configuration.md) |
| AGC and the magnet's field strength | Automatic Gain Control (AGC) and CORDIC Magnitude | p. 37 | `ReadAgc()`, `ReadMagnitude()`, `ErrorFlags::agc_warning` | [API guide](api-guide.md), [Troubleshooting](troubleshooting.md) |
| OTP burn and verification | Burn and Verification of the OTP Memory; Step-by-Step Procedure (Figure 60) | p. 39 | `ProgramOtp()`, `OtpReport`, `SetZeroHere()` | [OTP programming](otp.md) |
| Reference circuit | Circuit Diagram | p. 44 | - | [Hardware](hardware.md), [Quickstart](quickstart.md) |
| Package and magnet placement | Package Drawings & Markings; Mechanical Data | p. 45 | - | [Hardware](hardware.md) |
<!-- /generated -->

## When the datasheet and the driver differ

The driver is written against the revision above. If you have a newer revision, compare its "Revision Information"
section with the topics above; a change to a register, a frame or the OTP procedure is a bug report for this
repository. Where the driver deliberately goes beyond the datasheet (for example the presence check in
`EnsureInitialized()`, which the datasheet does not define), the [Contract](contract.md) says so.

The repository never contains the PDF itself: vendors own their documents. Download it from the vendor, and compare
its SHA-256 with the one above to confirm you have the same file.
