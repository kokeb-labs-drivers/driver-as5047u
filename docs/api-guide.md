# API guide

The driver's API by task, with code that compiles: initialise, measure, set the zero, watch the magnet, burn the
OTP and handle errors.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From ams OSRAM DS000637 v1-01:

    - Adaptive velocity filter: Adaptive Filter System; Rotation Speed Measurement, p. 12
    - Angle, velocity, AGC, magnitude, ERRFL and DIA registers: Volatile Registers, p. 24
    - Settings, zero position and direction registers: Non-Volatile Registers (OTP), p. 27
    - AGC and the magnet's field strength: Automatic Gain Control (AGC) and CORDIC Magnitude, p. 37

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## The shape of the API

| Group | Calls |
| --- | --- |
| Lifecycle | `EnsureInitialized()`, `IsInitialized()`, `Deinitialize()` |
| Configuration | `ApplyConfig()`, `GetConfig()`, `SetZeroPosition()`, `SetZeroHere()`, `SetDirection()` |
| Measurement | `ReadAngleCounts()`, `ReadAngleDegrees()`, `ReadAngleRadians()`, `ReadUncompensatedAngleCounts()`, `ReadVelocityCounts()`, `ReadVelocityDegPerSec()`, `ReadVelocityRpm()`, `ReadAgc()`, `ReadMagnitude()` |
| Diagnostics | `ReadDiagnostics()`, `ReadErrorFlags()`, `GetLastErrorFlags()`, `GetLatchedErrorFlags()`, `ClearLatchedErrorFlags()`, `GetLastReply()`, `ReadRegister()`, `WriteRegister()`, `ReadNonVolatile()` |
| OTP | `ProgramOtp()` |
| Free functions | `Validate()`, `CountsToDegrees()`, `CountsToRadians()`, `Crc8()`, `ToString()`, `GetDriverVersion()` |

Every call that touches the chip returns `Result<T>`: check `ok()` (or the `bool` conversion) and read `value`, or read
`error`. `Result` and `Error` are `[[nodiscard]]`, so an unchecked call is a compiler warning. A failed call's `value`
is `T{}`, never a stale or partial reading.

## Initialise

```cpp
--8<-- "tests/docs/api_tasks.cpp:lifecycle"
```

The constructor stores the port and the configuration and does no I/O. `EnsureInitialized()` is the one way to
start the chip. Its first call brings the bus up and waits for the chip to be ready: it reads DIA once per
millisecond, for at most `Config::startup_timeout_ms` (default t_pon, 10 ms; up to `kStartupTimeoutMaxMs`), until
every magnetic loop reports finished, and accepts that only when the next read says the same (datasheet "Timing
Characteristics", Figure 9; DIA, Figure 35). A chip that is already up costs no wait. It then reads ERRFL once to
clear what the power-up left there, writes the configuration and reads each register back. `write_settings = false`
skips the writes, for an OTP-burned part, and reads the chip's settings into `GetConfig()` instead.

| Start-up result | Meaning |
| --- | --- |
| `NoDevice` | Only line patterns until the deadline: MISO stuck or floating, MOSI looped back, DIA always 0 |
| `NotReady` | The chip answers, but its loops did not finish by the deadline (no magnet, slow supply) |
| `DeviceIdMismatch` | DIA has a bit set that reads 0 on an AS5047U: another device answers |

Once it has succeeded, `EnsureInitialized()` returns `Ok` at once with no I/O, so a caller (or a handler that owns
the driver) can call it before every use. After a failure the driver stays uninitialised and the next call starts
again from the beginning. No other call starts the chip: before `EnsureInitialized()` succeeds every read and write
returns `NotInitialized` and sends nothing, so a read in a control loop is always its own frames and nothing more.
`ApplyConfig()` before the start only stores the configuration for the first `EnsureInitialized()`.

`Deinitialize()` only marks the driver uninitialised: the chip has no low-power mode. It is also the recovery
after a fault: `Deinitialize()`, then `EnsureInitialized()` starts the chip again.

## Measure

```cpp
--8<-- "tests/docs/api_tasks.cpp:control-loop"
```

| Call | Register | Unit |
| --- | --- | --- |
| `ReadAngleCounts()` | Address 0x3FFF: ANGLECOM, with DAEC unless `dynamic_compensation` is false; ANGLEUNC when `uncompensated_at_angle_register` is true | 0 to 16383 counts, 0.022° per count |
| `ReadAngleDegrees()`, `ReadAngleRadians()` | Address 0x3FFF, as above | [0, 360) °, [0, 2π) rad |
| `ReadUncompensatedAngleCounts()` | ANGLEUNC (0x3FFE): never compensated | 0 to 16383 counts |
| `ReadVelocityCounts()` | VEL (0x3FFC), signed 14 bit | 24.141 °/s per count |
| `ReadVelocityDegPerSec()`, `ReadVelocityRpm()` | VEL | °/s, rpm |
| `ReadAgc()` | AGC (0x3FF9) | 0 to 255 |
| `ReadMagnitude()` | MAG (0x3FFD), CORDIC magnitude | 0 to 16383 |

Each read is two frames, CRC-checked unless the frame format is `Bits16`, and nothing more while the reply carries
no flag bit: this is the hot path, and no verification is ever added to it. When the reply carries the error or the
warning bit, the driver reads ERRFL (two more frames) to find out why. An angle or velocity read under an AGC or
magnetic-half warning fails with `MagnetWarning`: the field is out of the range the chip regulates, so the value is
not a measurement (handbook KB-CPP-127). A P2RAM single-bit warning (corrected by the ECC) does not refuse it.

| Reply of an angle read | Frames | Result |
| --- | --- | --- |
| No flag bit | 2 | The angle |
| Warning bit, ERRFL holds AGC_warning or MagHalf | 4 | `MagnetWarning` |
| Warning bit, ERRFL holds only P2ram_warning | 4 | The angle |
| Error bit | 4 | `Crc`, `Framing`, `Command`, `DeviceFault`; `NoDevice` when ERRFL holds no error |
| Error bit, ERRFL holds OffCompNotFinished or WDTST | 18 | `DeviceReset` when the settings registers changed, else `DeviceFault` ([below](#notice-a-reset-of-the-chip)) |
| Error bit, the ERRFL read fails (CRC, transfer, reserved bit) | 18 | `DeviceReset` when the settings registers changed, else the read's error; `flags_lost` |
| No flag bit, a compare is pending (it could not be read after a lost ERRFL) | 16 | The angle once the compare passes; `DeviceReset` on a difference; the compare's error while it cannot be read |

Two reads are two separate samples: the angle and the velocity of the example above are not taken at the same instant.
The velocity reads ±1 count (±24 °/s) at standstill, and more with a high filter K: detect "stopped" with a threshold,
not with zero.

## Set the zero and the direction

```cpp
--8<-- "tests/docs/api_tasks.cpp:zero"
```

`SetZeroHere()` writes the zero as 0, reads the angle, and writes that angle as the new zero; it returns the new
zero position in counts. `SetZeroPosition(counts)` writes a known value, and `SetDirection()` chooses the direction
in which the angle increases. Each reads the settings registers (to keep their diagnostic bits) and writes the
whole configuration again. The settings are lost at power-off unless you burn them ([OTP programming](otp.md)).

### Verify a write

A write is the two frames the datasheet needs, the command and the data, always with a CRC, and the reply's error
bit is checked as for every access. Reading the register back is chosen per call: `WriteRegister()`, `ApplyConfig()`,
`SetZeroPosition()`, `SetZeroHere()` and `SetDirection()` take a last parameter `as5047u::Verify`, `None` by default.

```cpp
--8<-- "tests/docs/api_tasks.cpp:verify"
```

`Verify::ReadBack` reads each written register back (two more frames per register) and fails with `WriteMismatch`
when it differs: it catches a write the chip lost, or a read-only bit. Use it off the timed path; a call in a
control loop stays at the frames the datasheet needs. Any other value is refused with `InvalidArgument` before any
I/O. Two calls always read back, whatever you pass elsewhere: `EnsureInitialized()` (start-up is not
timing-critical) and `ProgramOtp()` (a burn cannot be undone).

A write that fails (a reply CRC error, a lost frame, an error flag, `WriteMismatch`) may still have taken effect: the
chip stores the data frame when it arrives. The driver never retries it. It reads DISABLE to ECC back (14 frames) and
`GetConfig()` then holds what the chip holds, field by field. If that read fails too, the driver is uninitialised
and `EnsureInitialized()` writes the configuration again.

## Watch the magnet

Warnings refuse an angle or a velocity (`MagnetWarning`); the AGC, magnitude and diagnostics still read, so you can
see why. ERRFL clears when it is read, and the driver reads it inside other calls; every value it reads, those
included, is ORed into a latch the driver owns (handbook KB-CPP-119):

| Call | I/O | What it gives |
| --- | --- | --- |
| `ReadErrorFlags()` | 2 frames | ERRFL now (clears it on the chip; also ORed into the latch) |
| `GetLastErrorFlags()` | none | The last ERRFL value the driver read, by any call |
| `GetLatchedErrorFlags()` | none | Every ERRFL flag read since `ClearLatchedErrorFlags()`; `EnsureInitialized()` empties it, then holds what the power-up left |
| `ClearLatchedErrorFlags()` | none | Empties the latch, `flags_lost` included |
| `GetLastReply()` | none | The raw reply word of the last frame, warning and error bits included |

```cpp
--8<-- "tests/docs/api_tasks.cpp:health"
```

`ReadDiagnostics()` decodes DIA: the AGC limits (`agc_low`, `agc_high`), `mag_half`, whether the internal loops
have settled (`loops_finished`), and whether the chip runs from 5 V (`vdd_5v`).

| Flag | Meaning |
| --- | --- |
| `ErrorFlags::agc_warning` | AGC at 0 or 255: the magnet is too close or too far |
| `ErrorFlags::mag_half` | The field is at half the regulated value |
| `ErrorFlags::p2ram_warning` | The ECC corrected one bit of the OTP copy |
| `ErrorFlags::flags_lost` | In the latch only: a read of ERRFL failed (reply CRC, transfer, reserved bit) after its command may have reached the chip, which then cleared the flags it held: a fault may have been missed |

Reading ERRFL or AGC regularly is also what the datasheet requires for the AGC loop to work as specified.

## Notice a reset of the chip

A power-on reset or a brown-out of the sensor (a supply dip, a loose connector) reloads DISABLE to ECC from the OTP:
every setting written since start-up is gone ("Non-Volatile Registers (OTP)"), and on a blank part the zero and the
direction return to their reset values, so the angle shifts with no error from the chip. The driver checks for this
in two ways:

- **Automatically**, at no cost on the hot path: when any ERRFL read after start-up holds OffCompNotFinished or
  WDTST (the offset compensation restarts at power-up, Figure 33), or fails and so may have taken that flag, the
  driver reads DISABLE to ECC (14 frames) and compares them with what it last wrote or read there.
- **On request**: `CheckForReset()` makes the same comparison, 14 frames (about 34 µs of bus time at 10 MHz), for a
  supervision tick off the timed path. The datasheet does not promise that ERRFL holds the flag after a reset, so
  call it at the rate at which a reset must be noticed.

On a difference the call fails with `DeviceReset` and the driver is uninitialised: no call writes the cached
settings over the reset chip, and `EnsureInitialized()` starts it again with the stored configuration. A
configuration equal to the OTP (a burned part with `write_settings` false, or `Config{}` on a blank part) is not
changed by a reset, so nothing is reported and nothing is lost; the reads during t_pon after the reset still fail
with `DeviceFault`.

```cpp
--8<-- "tests/docs/api_tasks.cpp:reset"
```

## Use any register

`ReadRegister(address)` reads any 14-bit address once, with no retry, and returns the 14 data bits; a warning bit
alone does not fail it, an error bit is resolved through ERRFL. Reading ERRFL this way clears it on the chip and
feeds the latch; reading DIA restarts its frame counter. `WriteRegister(address, value)`
writes one (add `Verify::ReadBack` to read it back); it refuses PROG (0x0003) with `InvalidArgument`, so that only
`ProgramOtp()` can start a burn. `ReadNonVolatile()` returns DISABLE to ECC (0x0015 to 0x001B) as an array, in
address order. The addresses and fields are in [`inc/as5047u_registers.hpp`](../inc/as5047u_registers.hpp)
(`as5047u::reg`). A `WriteRegister()` to a settings register is overwritten by the next `ApplyConfig()`.

## Burn the OTP

```cpp
--8<-- "tests/docs/api_tasks.cpp:otp"
```

Irreversible. `ProgramOtp()` needs the typed `kOtpConfirm` (an `OtpBurnConfirm`; an integer does not compile), runs
steps 6 to 19 of the datasheet procedure, and fills an `OtpReport` with the step that failed and whether the burn
started. The steps before and after the call are yours. [OTP programming](otp.md) has the whole procedure.

## Handle errors

```cpp
--8<-- "tests/docs/api_tasks.cpp:errors"
```

| Signal | Meaning | What to do |
| --- | --- | --- |
| `Error::Bus` | The port failed a transfer or the bus bring-up | Check the port and the wiring; `EnsureInitialized()` again |
| `Error::NoDevice` | A reply a stuck, floating or looped-back line produces, at start-up or later | Wiring, MISO, MOSI, supply ([Troubleshooting](troubleshooting.md)) |
| `Error::NotReady` | At start-up the chip answered but its loops did not settle by `startup_timeout_ms` | Magnet, supply ramp; raise the deadline |
| `Error::DeviceIdMismatch` | Something answered at start-up, but not an AS5047U (DIA bit 10 set) | Chip select, the device on the bus |
| `Error::MagnetWarning` | An angle or velocity read under an AGC or magnetic-half warning: not a measurement | Magnet distance and centring; `ReadAgc()` |
| `Error::Crc` | A reply had a wrong CRC, or the chip saw one (ERRFL.CRC_error) | Lower the clock; shorten the wires |
| `Error::Framing` | The chip saw a frame of the wrong length (ERRFL.Framing_error) | CSn must go high between frames; one transfer per frame |
| `Error::Command` | The chip saw an invalid command (ERRFL.Command_error) | An unknown address in `ReadRegister()` or `WriteRegister()` |
| `Error::DeviceFault` | ERRFL reports a measurement fault: P2RAM, watchdog, CORDIC overflow, offset compensation | Check the magnet and `ReadDiagnostics()`; the value is not a measurement |
| `Error::DeviceReset` | The chip was reset (its settings registers no longer hold what the driver wrote) or a reset could not be ruled out; the driver is uninitialised | Check the supply; `EnsureInitialized()` again ([Notice a reset](#notice-a-reset-of-the-chip)) |
| `Error::WriteMismatch` | A register read back different from what was written (`Verify::ReadBack`, `EnsureInitialized()` or `ProgramOtp()`) | Read-only field, or a lost or corrupted write |
| `Error::Timeout` | The OTP burn did not finish in 100 ms | [OTP programming](otp.md#when-it-fails) |
| `Error::OtpVerify` | The burned part failed the guard-band test | Scrap the part |
| `Error::InvalidArgument` | A `Config` field or an argument is out of range; nothing was sent | Fix the value (`Validate()`) |
| `Error::NotInitialized` | `EnsureInitialized()` has not succeeded | Call it |

`Error::Busy` and `Error::Unsupported` belong to the shared base set but are never returned by this driver: the
chip has no busy state and the driver no optional feature. Every value, with the calls that return it, is on the
[Contract](contract.md#errors) page. `as5047u::ToString()` names an error for a log.

Next: [Configuration](configuration.md).
