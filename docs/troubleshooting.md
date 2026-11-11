# Troubleshooting

Symptoms, causes and fixes, cheapest check first. The causes are the datasheet's; none of them is **verified on
hardware** with this driver yet.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From ams OSRAM DS000637 v1-01:

    - Power-up time and SPI timing: Timing Characteristics; SPI Timing, p. 9
    - CRC-8 of a frame: CRC Checksum, p. 23
    - Angle, velocity, AGC, magnitude, ERRFL and DIA registers: Volatile Registers, p. 24
    - AGC and the magnet's field strength: Automatic Gain Control (AGC) and CORDIC Magnitude, p. 37

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## EnsureInitialized returns Bus

The port reported a failure: either `EnsureInitialized()` returned `false` or a transfer did.

1. Check that your port's `EnsureInitialized()` returns `true` on its own (log it).
2. Check the bus pins in your port against the board.
3. With a logic analyser, check that the first frames (a read of DIA, command 0x7FF5) appear on the bus.

## EnsureInitialized returns NoDevice, NotReady or DeviceIdMismatch

The driver polls DIA at start-up, once per millisecond up to `Config::startup_timeout_ms` (10 ms by default), and
starts only when two DIA reads in a row report every magnetic loop finished. It then names what it saw last:

| Error | What the bus showed | Usual cause |
| --- | --- | --- |
| `NoDevice` | All zeros or all ones (MISO stuck low or floating high), the looped-back command, or nothing but 0 in DIA | No supply, MISO not connected, MOSI not reaching the chip |
| `NotReady` | A plausible DIA, but `loops_finished` never set | No magnet or a far magnet, a slow supply ramp: raise `startup_timeout_ms` |
| `DeviceIdMismatch` | DIA with bit 10 set, which reads 0 on an AS5047U | Another device on this chip select |
| `Crc` | Replies with a wrong CRC that are not line patterns | Signal integrity, wrong SPI mode |

| Cause | Check | Fix |
| --- | --- | --- |
| No supply, or TEST not grounded | 3.0 V to 3.6 V (3.3 V mode) or 4.5 V to 5.5 V (5 V mode) on VDD; TEST at GND | Fix the supply; ground TEST |
| MISO not connected, or floating | Probe MISO during a frame: it must toggle | Wire MISO |
| Wrong SPI mode | Mode 1: CLK idles low, data sampled on the falling edge | Set mode 1; modes 0, 2 and 3 do not work |
| CSn held low across frames | CSn must go high after each `Transfer()` | One frame per chip-select period ([Porting](porting.md)) |
| 5 V mode with a 3.3 V MCU | V_IH is 0.7 × VDD, 3.15 V at 4.5 V | Level-shift, or run the sensor in 3.3 V mode |

## Calls return Crc, Framing or Command

`Crc` means a reply's CRC was wrong, or the chip saw a wrong CRC in a frame it received (ERRFL.CRC_error).
`Framing` and `Command` come from ERRFL: the chip saw a frame of the wrong length, or an invalid command.

| Cause | Fix |
| --- | --- |
| Clock too fast for the wiring | Drop to 1 MHz; the limit is 10 MHz on a short, clean bus |
| CSn set-up or high time under 350 ns | Add chip-select set-up and hold cycles in your SPI driver |
| A frame of the wrong length | `Transfer()` must clock exactly `len` bytes (2, 3 or 4) in one chip-select period |
| Noise on CLK | Shorter traces, a series resistor at the MCU, solid ground between MCU and sensor |
| `Command` from `ReadRegister()` or `WriteRegister()` only | The address does not exist on the chip |

Single errors point at signal integrity; an error on every call points at the port. With `Bits16` reads carry no
CRC, so a corrupted reply goes unnoticed: use `Bits24` while you bring a board up.

## Reads return DeviceFault

ERRFL reports a fault of the measurement path: P2RAM_error (uncorrectable OTP copy), Watchdog_error, Off_comp_error
(offset compensation not finished) or CORDIC_overflow. The value is not a measurement.

1. Read `ReadErrorFlags()` to see which flag it was.
2. Off_comp_error or CORDIC_overflow right after power-up: `EnsureInitialized()` waits for DIA to report the loops
   finished, so this should not appear at start-up; `GetLatchedErrorFlags()` after start-up shows what the power-up
   left in ERRFL. Otherwise
   check the magnet: a missing or far magnet keeps the loops from settling (`ReadDiagnostics().value.loops_finished`).
   Either flag after start-up, or Watchdog_error, makes the driver compare the settings registers with what it
   wrote: `DeviceFault` means they were unchanged, so the chip was not reset or the reset lost nothing
   ([Calls return DeviceReset](#calls-return-devicereset)).
3. P2RAM_error or Watchdog_error that persists after a power cycle: replace the part.

## Calls return DeviceReset

The chip lost its settings: a power-on reset or brown-out reloaded them from the OTP, which the driver saw as a
difference in DISABLE to SETTINGS3 after ERRFL reported OffCompNotFinished or WDTST, or in `CheckForReset()`. The
driver is uninitialised; nothing was written from its cache.

1. Call `EnsureInitialized()`: it waits for the chip and writes the configuration again.
2. Check the sensor supply for dips (decoupling at VDD and VDD3V3, the regulator, the connector) and the reset
   behaviour of whatever powers it.
3. A `DeviceReset` with no supply event: a `WriteRegister()` or a write that the chip dropped (with `Verify::None`)
   also leaves the registers differing from what the driver expects; use `Verify::ReadBack` to find it.

## Latched flags show flags_lost

A read of ERRFL failed after the chip had cleared the register, so the flags it held were never seen. It follows a
`Crc`, a `Bus` or a `NoDevice` from a call that read ERRFL: treat it as a fault that may have been missed, a reset
of the chip included (its OffCompNotFinished may have been in the lost read). The driver compared the settings
registers in that call when it could; to be sure, call `CheckForReset()`, read `ReadDiagnostics()` and
`ReadErrorFlags()`, fix the signal integrity ([Calls return Crc, Framing or
Command](#calls-return-crc-framing-or-command)), then `ClearLatchedErrorFlags()`.

## AGC at 0 or 255

| AGC | Meaning | Fix |
| --- | --- | --- |
| Near 255, `mag_half` set | Field too weak: magnet too far, too small or not diametrically magnetised | Move it closer; the field must be 35 mT to 70 mT at the die |
| Near 0 | Field too strong | Move the magnet away |
| Changes with angle | Magnet off-centre or tilted | Centre it on the package axis |

`ErrorFlags::agc_warning` is set at either limit. An angle or velocity read under it fails with `MagnetWarning`; the
AGC, magnitude and diagnostics still read. `GetLatchedErrorFlags()` shows every warning the driver has seen, and
`ReadErrorFlags()` reads ERRFL now.

## Reads return MagnetWarning

The reply carried the warning bit and ERRFL holds AGC_warning or MagHalf: the field at the die is outside the range
the chip regulates, so the angle is not a measurement (handbook KB-CPP-127). Fix the magnet as in the table above.
The read cost four frames instead of two (the ERRFL read). While the field stays out of range, every angle read
fails this way; nothing is returned as a value.

## Reads return NoDevice after a good start

A reply that a stuck or looped-back line produces: all zeros or all ones with a wrong CRC, an ERRFL with its
reserved bit set, or an error bit while ERRFL holds no error. Check that MISO is still driven and MOSI still reaches
the chip; a connector that works loose does this. With `Bits16` frames MISO stuck low reads as angle 0 with no flag
([Status and limitations](limitations.md)): use `Bits24`.

## The angle is wrong or noisy

| Symptom | Cause | Fix |
| --- | --- | --- |
| Angle runs backwards | Direction | `SetDirection(Direction::CounterClockwise)`, or `Config::direction` |
| Zero is in the wrong place | Zero not set, or lost at power-off | `SetZeroHere()`, then [OTP programming](otp.md) to keep it |
| Noisy at standstill | High filter K, or DAEC at low speed | Filter codes for a lower K (`filter_k_min_code`, `filter_k_max_code`); `dynamic_compensation = false` below about 100 rpm |
| Lags at high speed | DAEC off, or reading ANGLEUNC | `dynamic_compensation = true`; use `ReadAngleCounts()` |
| The ABI count lags by a step after a reversal | ABI hysteresis | `hysteresis_code` (SETTINGS3.HYS, Figure 48) |

## Velocity is not zero at standstill

The velocity is a signed 14-bit value at 24.141 °/s per count. The datasheet gives 5.8 °/s rms noise at K = 0 and
244.9 °/s at K = 6 (Figure 17), so ±1 count with the magnet still is normal, and more with a high filter K. Detect
"stopped" with a threshold, not with zero.

## Settings are gone after a power cycle

The settings registers are volatile until the OTP is burned: this is expected. Write them in `EnsureInitialized()` at
every start-up, or burn them once you are sure of them ([OTP programming](otp.md)).

## A setting returns WriteMismatch

A register read back different from what was written: a call with `Verify::ReadBack`, `EnsureInitialized()` or
`ProgramOtp()`. Check for `Crc` errors on the same bus first; a read-only bit written through `WriteRegister()` also
reads back differently.

## ProgramOtp fails

[OTP programming](otp.md#when-it-fails) says what each failure means and whether the part can still be used.
