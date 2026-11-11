# Configuration

Every field of `as5047u::Config`, the register it goes to, how a configuration is checked, OTP-burned parts, and
the optional Kconfig layer.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From ams OSRAM DS000637 v1-01:

    - Dynamic angle error compensation (DAEC): Dynamic Angle Error Compensation, p. 12
    - Adaptive velocity filter: Adaptive Filter System; Rotation Speed Measurement, p. 12
    - Settings, zero position and direction registers: Non-Volatile Registers (OTP), p. 27
    - ABI incremental output: ABI Incremental Interface, p. 31
    - UVW commutation output: UVW Commutation Interface, p. 33
    - PWM output and hysteresis: PWM; Incremental Output Hysteresis, p. 35

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Fields

`Config{}` holds the register reset values of a part whose OTP was never burned (every non-volatile register reads
0), so it changes nothing on such a chip. Only the frame format is a driver-side choice. `EnsureInitialized()` and
`ApplyConfig()` read DISABLE to ECC, to keep the diagnostic bits of ZPOSL and SETTINGS1, and write DISABLE to
SETTINGS3 (0x0015 to 0x001A). `EnsureInitialized()` reads each write back; `ApplyConfig()` does only with
`Verify::ReadBack` ([API guide](api-guide.md#verify-a-write)). The writes go to the volatile copy and are lost at
power-off unless you burn them ([OTP programming](otp.md)).

| Field | Default | Range | Register |
| --- | --- | --- | --- |
| `frame` | `FrameFormat::Bits24` | `Bits16`, `Bits24`, `Bits32` | Driver only: the frame length of reads. **`Bits16` reads carry no CRC**: MISO stuck low reads as angle 0 with no flag ([Status and limitations](limitations.md#what-the-driver-cannot-detect)) |
| `pad_byte` | 0 | 0 to 255 | Driver only: the pad byte of 32-bit frames |
| `write_settings` | `true` | | Driver only: `false` leaves the settings as the chip has them |
| `startup_timeout_ms` | 10 (`kPowerOnTimeMs`, t_pon) | 0 to 1000 (`kStartupTimeoutMaxMs`) | Driver only: how long `EnsureInitialized()` polls DIA for the chip to be ready; 0 checks once |
| `direction` | `Direction::Clockwise` | `Clockwise`, `CounterClockwise` | SETTINGS2.DIR |
| `zero_position` | 0 | 0 to 16383 counts | ZPOSM (bits 13..6), ZPOSL (bits 5..0) |
| `dynamic_compensation` | `true` | | SETTINGS2.DAECDIS = 0 |
| `adaptive_filter` | `true` | | DISABLE.FILTER_disable = 0 |
| `filter_k_min_code` | 0 | 0 to 7 | SETTINGS1.K_min |
| `filter_k_max_code` | 0 | 0 to 7 | SETTINGS1.K_max |
| `incremental` | `IncrementalOutput::Abi` | `Abi`, `Uvw` | SETTINGS2.UVW_ABI |
| `abi_enable` | `true` | | DISABLE.ABI_off = 0 |
| `uvw_enable` | `true` | | DISABLE.UVW_off = 0 |
| `pwm_enable` | `false` | | SETTINGS2.PWMon |
| `abi_resolution_code` | 0 | 0 to 7 | SETTINGS3.ABIRES |
| `abi_decimal` | `false` | | SETTINGS2.ABI_DEC |
| `index_width` | `IndexWidth::ThreeLsb` | `ThreeLsb`, `OneLsb` | SETTINGS2.IWIDTH |
| `uvw_pole_pairs` | 1 | 1 to 7 | SETTINGS3.UVWPP + 1 |
| `hysteresis_code` | 0 | 0 to 3 | SETTINGS3.HYS |
| `high_temperature_noise` | `false` | | SETTINGS2.NOISESET: 150 °C operation at 3.3 V |
| `uncompensated_at_angle_register` | `false` | | SETTINGS2.Data_select: address 0x3FFF and the PWM output carry the angle without DAEC |

Directions are seen from above the magnet. The driver keeps the diagnostic enable bits of ZPOSL and SETTINGS1
(Dia1_en to Dia4_en) as the chip has them. With `uncompensated_at_angle_register` false (the reset value) address
0x3FFF reads ANGLECOM, the compensated angle; `ReadUncompensatedAngleCounts()` reads ANGLEUNC (0x3FFE) in either
case. ECC is written only by `ProgramOtp()`.

Codes are the raw register field values (Figures 46 to 48), not physical values:

- **Filter.** The velocity filter picks a coefficient K between K_min and K_max from the acceleration; low K means
  less noise and more latency. The code-to-K mapping is not linear (Figure 13): K_min code 0 is K = 2, K_max code
  0 is K = 6. The application note *AS5x47U Adaptive Filter* helps choose them.
- **DAEC.** Dynamic angle error compensation removes the latency of the angle at speed. The datasheet suggests
  turning it off below about 100 rpm, where it only adds noise.
- **ABI resolution.** With `abi_decimal` false:

| `abi_resolution_code` | Steps per revolution | Pulses per revolution |
| --- | --- | --- |
| 0 (default) | 4096 | 1024 |
| 1 | 2048 | 512 |
| 2 | 1024 | 256 |
| 3 | 8192 | 2048 |
| 4 | 16384 | 4096 |

With `abi_decimal` true the codes give decimal counts, from 1000 down to 25 pulses per revolution; the datasheet's
SETTINGS3 table (Figure 48) lists them, and the codes not shown here.

- **Outputs.** `incremental` picks ABI or UVW on the shared pins; `abi_enable` and `uvw_enable` tristate the pins
  when false; PWM goes on pin W with ABI and on pin I with UVW ([Hardware](hardware.md#outputs-other-than-spi)).

Start from `Config{}` and change fields:

```cpp
--8<-- "tests/docs/configuration.cpp:custom"
```

## Validation

`as5047u::Validate(config)` checks a configuration without touching the chip, and `EnsureInitialized()` and
`ApplyConfig()` call it before any I/O. It returns `Error::InvalidArgument` when an enum holds a value that is not
an enumerator, `zero_position` is above 16383, a 3-bit code is above 7, `hysteresis_code` is above 3, or
`uvw_pole_pairs` is 0 or above 7.

## Change one setting

`SetZeroPosition()`, `SetZeroHere()` and `SetDirection()` change one field of `GetConfig()` and write it with
`ApplyConfig()`, so every other field is written again with the value it already has. `SetZeroHere()`
sets the zero to 0, reads the angle and writes it as the new zero: steps 4 and 5 of the datasheet's OTP procedure.

## OTP-burned parts

A part whose OTP is burned starts from its own settings. Initialise it with `write_settings = false`: the driver
checks that the chip answers, writes nothing, and reads DISABLE to SETTINGS3 back into `GetConfig()`.

```cpp
--8<-- "tests/docs/configuration.cpp:burned"
```

`GetConfig()` then holds the settings the chip holds; only the driver-side fields (`frame`, `pad_byte`,
`write_settings`, `startup_timeout_ms`) come from the `Config` you passed. A later `SetZeroPosition()` or
`SetDirection()` therefore changes only its own field, in the volatile copy, until the next power cycle. `ApplyConfig()`
writes the whole `Config` it is given, as always.

UVWPP code 7 reads back as 7 pole pairs, like code 6; written back, it becomes code 6. Both mean 7 pole pairs, but
`ReadNonVolatile()` then differs from the OTP content in that field until the next power cycle.

## Kconfig

Under ESP-IDF, `idf.py menuconfig` shows **AS5047U rotary position sensor** when the driver is a component of the
project ([Installation](installation.md)); the menu is [`_config/Kconfig`](../_config/Kconfig). The options reach
the code only through [`inc/as5047u_kconfig.hpp`](../inc/as5047u_kconfig.hpp), never
through a driver signature:

```cpp
--8<-- "tests/docs/configuration.cpp:kconfig"
```

| Option | Default | `Config` field |
| --- | --- | --- |
| `CONFIG_AS5047U_FRAME_16`, `_24`, `_32` (a choice) | 24-bit with CRC-8 | `frame` |
| `CONFIG_AS5047U_DIRECTION_CCW` | n | `direction` |
| `CONFIG_AS5047U_DAEC_DISABLE` | n | `dynamic_compensation` (false when set) |

Without Kconfig (plain CMake, other platforms) `KconfigDefaults()` returns `Config{}`. Both cases are host
tests.

Next: [OTP programming](otp.md).
