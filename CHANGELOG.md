# Changelog

All notable changes to this repository are listed here. Versions follow [Semantic Versioning](https://semver.org/).
Entries under **Breaking** need source changes in code that used the affected API; any of them calls for a major
version bump (a minor bump before 1.0).

## [Unreleased]

### Changed

- The repository moved to kokeb-labs-drivers; URLs follow.
- The Hardware page draws the chip from the new `docs/hardware.yml`: the TSSOP-14 pinout with each pin's port hook,
  the block diagram of Figure 2, the minimum circuit of Figure 61 (5 V) and the 3.3 V supply option of Figure 11,
  and the pin and parts tables, every fact with its datasheet section and page. TEST, which Figure 4 gives no pin
  type, is marked as not confirmed. The hand-written pin table is gone.
- The `hardware-lessons-review` skill and `tools/preflight.sh` follow the template.

## [0.1.0] - 2026-11-09

### Breaking

- **A reset of the chip is reported as the new `Error::DeviceReset` (23).** When an ERRFL read after start-up holds
  OffCompNotFinished or WDTST, the driver reads DISABLE to ECC (14 frames) and compares them with what it last wrote
  or read there; on a difference, or when that read cannot be completed, the call (an angle or register read, a
  setter, `ReadErrorFlags()`) fails with `DeviceReset` instead of `DeviceFault`, and the driver is uninitialised
  until `EnsureInitialized()` writes the configuration again. Before, a power-on reset that reloaded the OTP went
  unnoticed: the angle moved with the lost zero and direction, the reads succeeded and `GetConfig()` still showed
  the old settings. A `switch` over `Error` with `-Wswitch-enum` needs the new case.
- **A failed settings write is read back, not assumed.** A write whose reply failed (a CRC error, a lost frame, an
  error flag, a read-back mismatch) may still have reached the chip. `ApplyConfig()` and the setters used to restore
  the old configuration in `GetConfig()`. Now they read DISABLE to ECC back (14 frames) and `GetConfig()` takes every
  chip field from what the chip holds. The write is never retried. When that read fails too, the driver is
  uninitialised and `EnsureInitialized()` writes the configuration again. A failed `WriteRegister()` to a settings
  register reads the registers back the same way, for the reset check.
- **An angle or velocity under a magnet warning is refused.** When the reply carries the warning bit and ERRFL holds
  AGC_warning or MagHalf, `ReadAngleCounts()`, `ReadUncompensatedAngleCounts()`, `ReadAngleDegrees()`,
  `ReadAngleRadians()` and the velocity reads fail with the new `Error::MagnetWarning` instead of returning the value
  (handbook KB-CPP-127): the chip says the field is out of the range it regulates. Before, warnings never failed a
  read. A P2RAM single-bit warning (corrected by the ECC) still returns the value; AGC, magnitude and diagnostics
  read as before. Such a read costs four frames (the ERRFL read); a read whose reply has no flag bit stays two.
- **Start-up waits for the chip to be ready.** `EnsureInitialized()` polls DIA once per millisecond until every loop
  reports finished, confirmed by a second read, for at most the new `Config::startup_timeout_ms` (default 10 ms,
  t_pon; 0 to `kStartupTimeoutMaxMs` = 1000). A chip that answers but is not ready fails with the new
  `Error::NotReady`; before, any non-zero DIA started the driver (handbook KB-CPP-124).
- **Stuck lines are `NoDevice`.** A missing chip, MISO stuck low or floating high, a looped-back MOSI or a chip
  that cannot hear MOSI now fail with the new `Error::NoDevice` (20) instead of `DeviceIdMismatch`, at start-up and,
  for a line pattern with a wrong CRC, an ERRFL with its reserved bit set or an error bit with no error in ERRFL, on
  later reads (handbook KB-CPP-116, KB-CPP-047). `DeviceIdMismatch` now means only that DIA has a bit set that reads
  0 on an AS5047U. An absent chip is reported after the start-up deadline (10 ms by default), not at once.
- **Every port needs `DelayUs()`**, because start-up polls DIA: a port without it no longer compiles, also when it
  never calls `ProgramOtp()`.
- **The port concept checks exact hook signatures and `noexcept`.** A `Transfer()` with a length narrower than
  `std::size_t`, a hook without `noexcept`, a `DelayUs()` that does not take `uint32_t` or a `Log()` with another
  signature is a compile error.
- **`ProgramOtp()` takes an `OtpBurnConfirm`.** `kOtpConfirm` is now that type, so `ProgramOtp(kOtpConfirm, report)`
  compiles unchanged; passing an integer does not compile.
- **`Error` is `[[nodiscard]]`**: a call whose `Error` is dropped (for example `Validate(config);`) warns.
- `GetConfig()` on a temporary driver is deleted (the reference would dangle).

### Added

- `CheckForReset()`: the same comparison on request, 14 frames, for a periodic check where a reset must not go
  unnoticed (the datasheet does not promise the ERRFL flag after a reset).
- `ErrorFlags::flags_lost`, set in `GetLatchedErrorFlags()` when a read of ERRFL fails after its command may have
  reached the chip (a reply CRC error, a failed transfer, `NoDevice` for the reserved bit): ERRFL clears when read,
  so the flags it held may have been missed. `ClearLatchedErrorFlags()` and `EnsureInitialized()` clear it. Such a
  read may have taken a reset's OffCompNotFinished, so it also makes the driver compare the settings registers
  (14 frames; `DeviceReset` on a difference). When that compare cannot be read either, it stays pending and every
  later call fails with its error until it is read: no value is returned while a reset may have gone unseen.
- An ERRFL latch: every ERRFL value the driver reads, inside other calls included, is ORed into
  `GetLatchedErrorFlags()` (no I/O) until `ClearLatchedErrorFlags()` (handbook KB-CPP-119). Before, an internal
  read overwrote the last value, so a flag could be lost. `ReadRegister(reg::kErrfl)` feeds it too.
- `GetLastReply()`: the raw reply word of the last frame, flag bits included (handbook KB-CPP-129).
- The bus requirements as `constexpr` in `inc/as5047u_spi_interface.hpp`, with datasheet references: `kSpiMode`
  (1), `kMsbFirst`, `kMaxClockHz` (10 MHz), `kMinCsHighNs` (350), `kCsSetupNs` (350), `kCsHoldNs` (50),
  `kContinuousWord`, `kFrameBits` (handbook KB-CPP-123); the host and ESP32 ports `static_assert` their settings, and
  the ESP32 port refuses a clock above the limit.
- `Config::operator==`, `reg::NvIndex` and `reg::At()` for the settings array, `kPowerOnTimeMs`,
  `kStartupTimeoutMaxMs`, `inc/as5047u_detail.hpp` (exact hook probes, enumeration checks).
- Strict-dialect builds (handbook KB-CPP20-001): `CMAKE_CXX_EXTENSIONS OFF` for every target, `-pedantic-errors`
  and the C++20 deprecation warnings as errors in the tests, a header self-check per public header
  (`as5047u_header_checks`, also in the Cortex-M build), and an ISO C++20 check of the headers inside the ESP-IDF
  build (`examples/esp32/main/as5047u_cxx20_check.cpp`).
- An independent chip model (handbook KB-TST-030): `tests/mock_as5047u.hpp` no longer includes the driver's register
  header or CRC, carries its own map with datasheet figures, a virtual clock with t_pon and four line modes (stuck
  low, floating high, looped back, deaf); `tests/test_model.cpp` cross-checks the two maps. New tests for start-up,
  line patterns, the latch and the magnet warning; new compile-fail tests for narrowed, throwing and misdeclared
  hooks and an integer OTP confirmation. The fuzzer breaks lines, replays stale replies, holds magnet warnings and
  delays the chip's ready, and checks that no broken line gives a start-up or an angle.
- The review skills `cpp20-profile`, `misra-review` and `hardware-lessons-review` and the `safety-reviewer` agent,
  from the driver template; CI runs the handbook's source checks.


- clang-tidy runs the CERT C++ checks (`cert-*`) as well; the library code had no finding.
- The `Log()` hook is documented as `void Log(LogLevel, const char* message)`: the message is plain text, and the
  examples print it with `"%s"` instead of using it as a format string (no C variadic function, CERT DCL50-CPP;
  no format string from data). A port with the old variadic signature still compiles.

- AS5047U driver, first version: `as5047u::AS5047U<Port>` over an SPI port (mode 1) with 16-bit, 24-bit (CRC-8,
  default) and 32-bit frames; writes always CRC-protected.
- One start-up entry point: the constructor stores the configuration with no I/O, `EnsureInitialized()` starts the
  chip once (and again after a failure or `Deinitialize()`), and no read or write starts it as a side effect.
- Reads of the angle (counts, degrees, radians, uncompensated), velocity (counts, °/s, RPM), AGC, magnitude, DIA
  and ERRFL; each reply's error bit resolved through ERRFL into `Crc`, `Framing`, `Command` or `DeviceFault`.
- Every setting as a `Config` field (`Config{}` is an unprogrammed part); `write_settings = false` reads a burned
  part's settings into `GetConfig()`. `SetZeroHere()`, `SetZeroPosition()`, `SetDirection()`, raw register access.
- `ProgramOtp(kOtpConfirm, report)`: the datasheet OTP procedure, steps 6 to 19, CRC frames throughout, reporting
  the failing step and whether the burn started.
- Kconfig options for the frame format, direction and DAEC; host and ESP32 examples; an OTP page.
- A libFuzzer target over corrupted, lost and replaced replies (`tests/fuzz/`, `-DAS5047U_BUILD_FUZZERS=ON`); its
  corpus replays as a CTest on every compiler, and CI fuzzes every pull request.

### Changed

- The `misra-review`, `cpp20-profile` and `new-driver-from-template` skills and `docs/repo-setup.md` follow the
  template: reading the MISRA report, the known tool misfires, enumeration ranges without a sentinel, and the
  settings tool.
- `tools/preflight.sh` and the `verify` and `hardware-lessons-review` skills follow the template: every check CI runs
  that a workstation can run, in one command, and the rules for device resets, faults the chip clears itself, dropped
  writes, switch-offs that no uncertainty refuses and a model that changes state on its own.
- Follows the driver template's portable ownership: copyright belongs to `The driver-as5047u authors` (`LICENSE`, every
  source header, `driver.yml` `authors`, the docs footer). `tools/check_standalone.py` runs in the *Handbook source
  checks* job and keeps the code and docs about this chip only. `AGENTS.md` follows the template's structure; the
  issue and PR templates ask for the board, MCU and port in one field.
- The port concepts match each hook's address against exactly one `noexcept` signature with
  `detail::ExactHook<Port, Sig>::Take(&Port::Hook)` instead of probing it with a converting `detail::Exactly<T>`:
  no conversion operator is left. A misdeclared hook is still a compile error. `kAlwaysFalse` takes a class and
  the enumeration helpers an enumeration (`ClassType`, `Enumeration`), each with a compile-fail test.
- `Crc8()` is table-driven: a 256-byte `constexpr` table built and checked at compile time against the bitwise
  definition, two lookups per word. On the host the 24-bit angle read is faster (docs/architecture.md); the 16-bit
  read has no CRC and is unchanged within measurement noise.
- Register work is done in `uint32_t` and narrowed once (MISRA C++:2023 Rule 7.0.5, KB-CPP20-021, -022); the
  settings encoder and decoder are free `constexpr` functions with `static_assert` round trips.
- Include guards instead of `#pragma once` in every header (handbook KB-CPP-019).
- The figure references in `inc/as5047u_registers.hpp` follow the datasheet: ERRFL is Figure 33, PROG 34, DIA 35,
  ECC_s 40.
- The template files (skills, agent, CI repository checks, clang-tidy profile) follow the driver template.
- **Not verified on hardware**: the start-up wait, the magnet-warning refusal and the line-pattern checks rest on the
  datasheet ("Timing Characteristics", Figure 9; DIA, Figure 35; ERRFL, Figure 33; the reply flag bits, Figures 22,
  25, 28). Bench steps: the "On hardware" section of the Testing and CI page.

- CI runs fewer jobs, from the driver template: the host tests build GCC and Clang in one job; DCO, the org
  standard, Markdown and YAML lint, the handbook rules, template drift and cppcheck are steps of one `Repository
  checks` job (`ci-repo-checks.yml`, replacing `ci-handbook.yml`, `ci-cpp-analysis.yml`, `ci-markdown-lint.yml` and
  `ci-yaml-lint.yml`); cross-compile and host quality use ci-tools' one-job layouts. No check is dropped. The
  workflows have no manual (`workflow_dispatch`) trigger any more.
- Releases also carry signed build provenance for every file (ci-tools `ru-attest-release`); check a
  download with `gh attestation verify`.
- The repository is public: the README points to the published site and its Doxygen API reference.
- Reading a write back is chosen per call: `WriteRegister()`, `ApplyConfig()`, `SetZeroPosition()`, `SetZeroHere()`
  and `SetDirection()` take a last parameter `Verify` (`as5047u::Verify::None` or `ReadBack`), `None` by default. By
  default a run-time write is only the command and data frames the datasheet needs (still CRC-protected, the
  reply's error bit checked); `ReadBack` reads each register back (two more frames) and fails with `WriteMismatch`.
  `EnsureInitialized()` and `ProgramOtp()` read their writes back as before.
- MISRA C++:2023 conformance: integer constants passed as arguments have the parameter's exact type, through the
  new `consteval` literals `_u8`, `_u16` and `_u32` in `as5047u::detail` (a value that does not fit is a compile
  error), and narrower values are widened by an explicit cast (`BitIf()` takes the `uint16_t` register bit), Rule
  7.0.6; `kMaxAngleCount` is computed with an unsigned literal (Rule 7.0.5); operands of `?:`, `&&`, `||` and
  comparisons are parenthesised (Rule 8.0.1); two loops read a non-constant bound into a `const` local first (Rule
  9.5.1); every switch over an enumeration ends with `default: break;` (Rule 9.4.2) and the test build adds
  `-Wswitch-enum`, so a new enumerator still warns; `Config::operator==` is a hidden friend (Rule 16.6.1); the
  include of `src/as5047u.ipp` carries its deviation marker (Rule 19.0.3). Behaviour is unchanged.

### Fixed

- ERRFL flags taken from the chip by a read whose reply failed its CRC were lost without a trace; the latch now
  says so (`flags_lost`).
- The reset compare covers the bits each settings register implements (DISABLE 6..0, the others 7..0, Figures 43 to
  49), so a raw write to an undescribed bit is not taken for a reset.
- The warning that `Bits16` reads carry no CRC is on `Config::frame` and its row in Configuration, next to the
  setting that selects it.
- A lost data frame no longer turns the next command into data: after any failed transfer the driver sends a NOP
  read first. Before, losing the data frame of "PROG = PROGOTP" let the next read burn the OTP (found by fuzzing).
- `SetZeroHere()` writes the previous zero back when its angle read fails, and returns `NotInitialized` before
  start-up without changing the stored zero.

