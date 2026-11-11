# Contract

What the AS5047U driver promises, what it needs, and the rules a caller follows.

The driver inherits the [Kokeb Labs driver contract](https://github.com/kokeb-labs/.github/blob/main/standards/driver.md);
this page states only what is specific to it. A change to anything here is a breaking change, listed under
**Breaking** in the [changelog](changelog.md).

## Promises

- The constructor is `noexcept`, does no I/O and cannot fail.
- `EnsureInitialized()` brings the bus up and waits for the chip: it polls DIA once per millisecond, for at most
  `Config::startup_timeout_ms` (default t_pon, 10 ms), until every magnetic loop reports finished, and accepts that
  only when the next DIA read confirms it. A DIA that is all zeros or all ones, or a line pattern with a wrong CRC,
  is `NoDevice`; a DIA with a bit set that reads 0 on an AS5047U is `DeviceIdMismatch`; a chip that answers but is
  not ready by the deadline is `NotReady`. Then it reads ERRFL once to clear what the power-up left (the value goes
  into the latch), and only then writes the configuration and reads it back. With `write_settings = false` it writes
  nothing and reads the chip's settings into `GetConfig()`. On any failure the driver stays uninitialised.
- Every call that touches the chip returns `Error::NotInitialized` before `EnsureInitialized()` succeeds. No call
  initialises the chip as a side effect.
- Every port failure propagates as `Error::Bus`; a failed read returns `T{}`, never a stale or partial value.
- After a failed transfer the next frame is a read of NOP, because the chip may still be waiting for the data frame
  of a write: taken as a command it reads nothing, taken as data it writes 0 (for PROG: no burn). A register whose
  write failed holds an unknown value (the old one, the new one or 0), so a failed `ApplyConfig()` or setter never
  retries the write and never keeps the old or the new value: it reads DISABLE to ECC back (14 frames, CRC- and
  flag-checked) and `GetConfig()` takes every chip field from what the chip holds; when that read fails too, the
  driver is uninitialised and `EnsureInitialized()` writes the configuration again. For ZPOSL
  and SETTINGS1 that 0 includes their DIA enable bits, which `ApplyConfig()` keeps as the chip holds them: power-cycle
  the chip (they reload from the OTP) or write them back with `WriteRegister()` if they matter to you.
- Every reply's CRC is checked (24- and 32-bit frames), and every reply's error bit is resolved through ERRFL:
  `Crc`, `Framing`, `Command` or `DeviceFault`. A reply that a stuck, floating or looped-back line produces is
  never a value: all zeros or all ones with a wrong CRC, an ERRFL with its reserved bit set, or an error bit with no
  error in ERRFL is `NoDevice` (handbook KB-CPP-116).
- An angle or velocity whose reply carries the warning bit is returned only when ERRFL holds no field warning; with
  an AGC or magnetic-half warning the call fails with `MagnetWarning` (handbook KB-CPP-127). A P2RAM single-bit
  warning (corrected by the ECC) does not refuse it. Other reads return their value under a warning.
- Every ERRFL value the driver reads, its own internal reads included, is ORed into one latch:
  `GetLatchedErrorFlags()` (no I/O) until `ClearLatchedErrorFlags()` (handbook KB-CPP-119). A read of ERRFL that
  fails once its command may have reached the chip (a reply CRC error, a failed transfer, the reserved bit set)
  sets `flags_lost` in the latch: ERRFL clears when read, so a fault may have been missed, a reset's
  OffCompNotFinished included. The driver then compares the settings registers as below (14 frames). When that
  compare cannot be read either, the call keeps its own error, the driver stays initialised and the compare stays
  pending: every later call runs it after its own access (14 more frames) and fails with the compare's error
  (`Bus`, `Crc`, ...) until it can be read, so no value is returned while a reset may have gone unseen. A compare
  that is read ends it: `DeviceReset` on a difference, else the call's own result.
- A reset of the chip is never followed by the old cached settings. When an ERRFL read after start-up holds
  OffCompNotFinished or WDTST, the driver reads DISABLE to ECC (14 frames) and compares them with what it last
  wrote or read there; on a difference, or when that read cannot be completed, the call fails with `DeviceReset`
  and the driver is uninitialised until `EnsureInitialized()` writes the configuration again. `CheckForReset()`
  makes the same comparison on request.
- An angle or velocity read is two frames when the reply has no flag bit set, four when it has one (the ERRFL read),
  and 18 when that ERRFL holds OffCompNotFinished or WDTST or its read fails (the settings check). Nothing else is
  ever added to it, except the pending compare described above (16 frames: the read and the compare) after a failed
  ERRFL read whose compare failed too.
  The compare covers the bits each register implements (DISABLE 6..0, the others 7..0, Figures 43 to 49).
- Every write is CRC-protected, in every frame format. A run-time write (`WriteRegister()`, `ApplyConfig()`, the
  setters) is the command and data frames only, unless the call passes `Verify::ReadBack`; `EnsureInitialized()`
  reads every settings write back. Every frame of `ProgramOtp()`, reads included, is CRC-protected, and its writes
  of steps 7, 9, 12 and 15 are always read back.
- The only waits are in `EnsureInitialized()` (1 ms between polls of DIA, at most `Config::startup_timeout_ms`, at most
  `kStartupTimeoutMaxMs`) and in `ProgramOtp()` (1 ms between polls of the burn, at most `kOtpBurnTimeoutMs`, 100 ms).
  Every loop has a fixed bound.
- `ProgramOtp()` takes a typed `OtpBurnConfirm`, burns nothing without `kOtpConfirm`, sends the burn command once and
  never retries it, and `WriteRegister()` refuses PROG.
- `Config{}` equals the register reset values of an unprogrammed part.
- The port concept checks each hook's exact signature and `noexcept`: a hook with a narrower parameter or without
  `noexcept` is a compile error. A missing `DelayUs()` is a compile error at `EnsureInitialized()`; a missing `Log()`
  logs nothing; a misdeclared `DelayUs()` or `Log()` is a compile error.
- What the chip needs from the bus is `constexpr` in `inc/as5047u_spi_interface.hpp` (`kSpiMode` to
  `kContinuousWord`), for a port to `static_assert` against.
- `Deinitialize()` does no I/O: the chip has no low-power mode.

## Depends on

| Dependency | Version | Why |
| --- | --- | --- |
| A C++20 compiler | GCC 10, Clang 13 or later | Concepts constrain the port |
| CMake | 3.16 or later | Generates `as5047u_version.h` |
| Your port | | The bus, within the bus requirements; the delay; optionally logging ([Porting](porting.md)) |

Nothing else: no RTOS, no SDK, no other repository of the org.

## Threading

The driver is not thread-safe and takes no lock. Use each driver object from one task at a time, or guard every
call on it with one mutex; the layer that owns the driver owns that lock, and the bus lock when several chips
share a bus. Nothing may run in an ISR: every call can block for a bus transfer or a delay. Separate objects
share no state.

## Errors

`as5047u::Error` inside `as5047u::Result<T>`. Values 0 to 9 are the shared base set; this driver returns all of
them except `Busy` and `Unsupported`, plus eight chip-specific values.

| `Error` | Value | Returned when |
| --- | --- | --- |
| `Ok` | 0 | The call succeeded |
| `NotInitialized` | 1 | `EnsureInitialized()` has not succeeded, or `Deinitialize()` ran since |
| `InvalidArgument` | 2 | A `Config` field or argument is out of range (`Validate()`, a `Verify` that is not an enumerator, a `startup_timeout_ms` above `kStartupTimeoutMaxMs`), `WriteRegister()` on PROG, or `ProgramOtp()` without `kOtpConfirm`; nothing was sent |
| `Bus` | 3 | A port hook returned `false` |
| `Timeout` | 4 | The OTP burn did not finish in `kOtpBurnTimeoutMs` (100 ms) |
| `DeviceIdMismatch` | 5 | Something answers at `EnsureInitialized()`, but DIA has bit 10 set, which reads 0 on an AS5047U |
| `Crc` | 6 | A reply's CRC was wrong, or ERRFL.CRC_error is set (the chip saw a wrong CRC) |
| `Busy` | 7 | Never returned: the chip has no busy state |
| `DeviceFault` | 8 | ERRFL reports P2RAM_error, Watchdog_error, Off_comp_error or CORDIC_overflow |
| `Unsupported` | 9 | Never returned: the driver has no optional feature |
| `Framing` | 16 | ERRFL.Framing_error is set: the chip saw a frame of the wrong length |
| `Command` | 17 | ERRFL.Command_error is set: the chip saw an invalid command |
| `WriteMismatch` | 18 | A register read back different from what was written: a call with `Verify::ReadBack`, `EnsureInitialized()`, or OTP steps 7, 9, 11, 12 and 15 |
| `OtpVerify` | 19 | The OTP guard-band read-back differs from what was burned (OTP step 19) |
| `NoDevice` | 20 | A reply a stuck, floating or looped-back line produces: only such replies at start-up until the deadline, or at run time a line pattern with a wrong CRC, an ERRFL with its reserved bit 8 set, or an error bit with no error in ERRFL |
| `NotReady` | 21 | At `EnsureInitialized()`: the chip answers, but DIA did not report every loop finished (twice in a row) by `Config::startup_timeout_ms` |
| `MagnetWarning` | 22 | An angle or velocity reply carried the warning bit and ERRFL holds AGC_warning or MagHalf: the field is out of range and the value is not a measurement |
| `DeviceReset` | 23 | The chip was reset, or a reset could not be ruled out: after OffCompNotFinished or WDTST in ERRFL, or in `CheckForReset()`, the settings registers did not hold what the driver wrote (or could not be read after such a flag). The driver is uninitialised |

The [API guide](api-guide.md#handle-errors) says what to do about each.

## Memory

No heap, no exceptions, no RTTI, no `std::function`, no `std::string`, no `printf` and no static mutable state. A
driver object holds a reference to the port, a `Config`, the last reply word, the last ERRFL value, the ERRFL latch,
the 14-byte copy of the settings registers it compares a reset against and five flags: under 80 bytes. Stack use
is small and fixed: 4-byte frame buffers, and 14-byte register arrays in `ReadNonVolatile()`, `CheckForReset()` and
`ProgramOtp()`. The CRC-8 table is 256 bytes of read-only data.

## Platform

Platform-free: the same code runs on any target with a C++20 compiler, including the host tests. The ESP-IDF port
in [`examples/esp32`](examples.md) is an example, not part of the driver.

## Deviations from the standard

None.
