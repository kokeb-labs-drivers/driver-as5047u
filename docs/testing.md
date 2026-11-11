# Testing and CI

How the driver is checked: host tests against a model of the chip, a fuzzer that corrupts what the chip sends back,
compiled documentation, an install test, ESP32 and Cortex-M builds, static analysis and measured quality gates, on
every pull request.

## Host tests

The tests build the real driver against a frame-level model of the AS5047U: the reply to each frame answers the previous
one, the frame length comes from the byte count, MOSI CRCs are checked, reading ERRFL clears it, writes take a command
and a data frame, PROG runs the OTP state machine with a burn time, and DIA reports the loops finished only after t_pon
on a virtual clock. Faults are injected per frame, per register or per line: a failed transfer, MISO stuck low or
floating high, MOSI looped back to MISO, a chip that cannot hear MOSI, a chip that is late or never ready, a corrupted
reply CRC, ERRFL flags (once, or persisting like a magnet out of range, and with either reading of which reply bit they
set), writes that do not stick, a burn that never finishes, a failed guard-band test. They build as ISO C++20
(`CMAKE_CXX_EXTENSIONS OFF`, `-pedantic-errors`, the C++20 deprecations as errors) with the firmware's flags,
`-fno-exceptions -fno-rtti`, and warnings as errors, on GCC and Clang; the target `as5047u_header_checks` compiles each
public header alone and asserts the dialect, and the Cortex-M cross-build compiles the same units. `as5047u_host_tests`
runs 68 test cases; with the Kconfig, version, install, Doxygen, host-example, compile-fail and fuzz-corpus tests CTest
runs 16 tests.

```bash
cmake -S . -B build -DAS5047U_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

| File | Covers |
| --- | --- |
| [`tests/test_as5047u.cpp`](../tests/test_as5047u.cpp) | `Validate()`, unit conversions, `NotInitialized` before `EnsureInitialized()`, settings written and read back at start-up, run-time writes sending no read-back by default and `Verify::ReadBack` catching a lost write, `write_settings = false` reading a burned part's settings (Data_select included) so a setter changes only its field, a missing chip in every frame format, every frame format with CRC-protected writes, reply CRC and error bit, warnings, PROG refused, measurements, direction, DAEC, `SetZeroHere()`, the OTP procedure and its failures (no confirmation, mismatch, timeout, guard band, device fault, CRC frames with 16-bit reads), logging |
| [`tests/test_startup.cpp`](../tests/test_startup.cpp) | The start-up wait: no wait for a chip that is up, a poll per millisecond for a late one, `NotReady` at the deadline and never later, a ready DIA counted only when confirmed, the deadline bounded by `Validate()`; each broken line (stuck low, floating high, looped back, deaf) refused at start-up in every frame format and on every read path after it |
| [`tests/test_diagnostics.cpp`](../tests/test_diagnostics.cpp) | The angle read is two frames; `MagnetWarning` for an angle or velocity under an AGC or magnetic-half warning, also when it persists, while diagnostic reads still answer; a P2RAM warning does not refuse it; the ERRFL latch fed by every read of ERRFL, internal ones included, until cleared; `NoDevice` for an error bit with no error and for ERRFL's reserved bit; `GetLastReply()`; a chip that sets the error bit for warnings gives the same results, writes included |
| [`tests/test_model.cpp`](../tests/test_model.cpp) | The model's register map, fields, timing and CRC against the driver's, with `static_assert`; the table CRC-8 against the bitwise one for all 65 536 words |
| [`tests/test_bus_faults.cpp`](../tests/test_bus_faults.cpp) | Every transfer of every read, configuration change, `EnsureInitialized()` and the OTP procedure fails in turn, and each returns `Bus`; after any lost frame of `ProgramOtp()` no later call starts a burn, and a lost write frame never turns the next command into data |
| [`tests/fuzz/`](../tests/fuzz/fuzz_as5047u.cpp) | The fuzz target; CTest replays its corpus (`as5047u_fuzz_as5047u_corpus`), see below |
| [`tests/test_reset.cpp`](../tests/test_reset.cpp) | A power-on reset of the model in mid-session (the settings reload from the OTP, t_pon restarts, ERRFL holds OffCompNotFinished): `DeviceReset` from the angle read, a setter and `ReadErrorFlags()`, during and after t_pon, and nothing written from the cache; `EnsureInitialized()` restores the settings; an unchanged chip under the same flags stays `DeviceFault`; raw settings writes and a burned part are not mistaken for a reset; `CheckForReset()` and its cost; `flags_lost` after an ERRFL read that fails its CRC, a failed transfer or a reserved bit; a reset whose only flag a corrupted ERRFL read took; a lost ERRFL compared at once, or on the next call when the compare fails too; raw writes of unimplemented bits; a hint from the read-back after a failed write resolved in the same call |
| [`tests/test_failed_writes.cpp`](../tests/test_failed_writes.cpp) | A write whose reply failed its CRC or whose frame was lost: `GetConfig()` follows what the chip took, field by field; the write is never retried; a failed read-back leaves the driver uninitialised; a rejected write keeps the chip's value; a failed raw settings write refreshes the reset check's copy |
| [`tests/test_errors.cpp`](../tests/test_errors.cpp) | The base `Error` values are 0 to 9; every chip-specific value (16 to 23) is returned by some call |
| [`tests/test_version.cpp`](../tests/test_version.cpp) | The version getters follow the generated header (also against a 9.8.7 probe) |
| [`tests/test_kconfig.cpp`](../tests/test_kconfig.cpp) | `KconfigDefaults()` with and without `CONFIG_` values |
| [`tests/compile_fail/`](../tests/compile_fail/port_without_delay.cpp) | Code that must not compile, each for its `EXPECT:` diagnostic: a port without `DelayUs()`, a `DelayUs(uint16_t)`, a `Transfer()` with a narrowed length or without `noexcept`, an integer in place of `OtpBurnConfirm`, a class that is not a port |
| [`tests/mock_as5047u.hpp`](../tests/mock_as5047u.hpp) | The chip model, with its own register map; the mock ports over it are in `mock_as5047u_bus.hpp` |
| [`tests/check_version_copy.cmake`](../tests/check_version_copy.cmake) | `_config/Doxyfile` carries the CMake version |
| [`examples/host/`](../examples/host/README.md) | The host example, built with the test flags and run (`as5047u_host_example`) |
| [`tests/check_install.cmake`](../tests/check_install.cmake) | Installs into a scratch prefix and builds [`tests/install_consumer/`](../tests/install_consumer/CMakeLists.txt) with `find_package(as5047u)` |
| [`tests/check_doxygen.cmake`](../tests/check_doxygen.cmake) | Doxygen with `_config/Doxyfile` and warnings as errors, when `doxygen` is installed |

The model does not reproduce the chip's ECC algorithm (its ECC_Checksum is a simple sum), the fuses, the
guard-band level, the magnetic loops or any timing other than the burn and t_pon: those need a bench.

### The model stands alone

[`tests/mock_as5047u.hpp`](../tests/mock_as5047u.hpp) is written from the datasheet, not from the driver: it does
not include `as5047u_registers.hpp`, `as5047u_types.hpp` or the driver's CRC. Its register map, field bits, timing
and bitwise CRC-8 are its own (namespace `as5047u_test::chip`, each with its datasheet figure), so a wrong constant
in the driver cannot hide behind the same wrong constant in the model (handbook KB-TST-030).
[`tests/test_model.cpp`](../tests/test_model.cpp) compares the two at compile time:

```cpp
--8<-- "tests/test_model.cpp:cross-check"
```

When the build stops on one of these, one of the two maps is wrong: check it against the figure the model names,
and fix that one. A new register goes into both, with its check. A behaviour of the chip that matters to the driver
becomes a fault mode of the model and a test; the model's virtual clock (`Elapse()`, driven by the mock port's
`DelayUs()`) carries t_pon and the burn time.

## Fuzz tests

Everything the driver receives comes from MISO, so MISO is what an attacker, a noisy cable or a broken chip
controls. [`tests/fuzz/fuzz_as5047u.cpp`](../tests/fuzz/fuzz_as5047u.cpp) lets libFuzzer choose a configuration,
then up to 64 driver calls, and on every transfer what the bus does: pass the model's reply, fail the transfer,
flip bits of the reply, replace it, or replay the previous reply (stale but with a valid CRC). It can also change
the magnet, the speed and ERRFL, hold a magnet warning, delay the chip's ready, break the lines in each of the four
ways above and power the chip again. After every call it checks the driver's promises and aborts, naming the line,
when one breaks:

- a failed call returns an `Error` the enum defines and `T{}`; a successful read is in its register's range;
- before `EnsureInitialized()` no call makes a transfer, and `InvalidArgument` means nothing was sent;
- a refused `SetZeroHere()` leaves the stored zero as it was;
- **no corrupted or lost frame ever starts an OTP burn**: the chip sees PROGOTP only inside
  `ProgramOtp(kOtpConfirm, ...)`, and then `OtpReport::burn_started` says so;
- with a broken line and no fuzzer change to the replies, no start-up succeeds and no angle read in a CRC-protected
  format returns a value.

The fuzzer found two bugs before the first release, both fixed with a unit test each: a write is a command frame
and a data frame, and when the data frame of "PROG = PROGOTP" was lost, the chip took the next read command as that
data and burned the OTP; and `SetZeroHere()` left the zero at 0 when its angle read failed.

```bash
# libFuzzer needs Clang (on macOS, Homebrew's LLVM)
cmake -S . -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DAS5047U_BUILD_TESTS=ON -DAS5047U_BUILD_FUZZERS=ON
cmake --build build-fuzz --target fuzz_as5047u
./build-fuzz/tests/fuzz/fuzz_as5047u -max_total_time=300 tests/fuzz/corpus/fuzz_as5047u
```

The same file also builds without libFuzzer as `fuzz_replay_fuzz_as5047u`, and CTest replays every input in
[`tests/fuzz/corpus/fuzz_as5047u/`](../tests/fuzz/corpus/fuzz_as5047u) on GCC and Clang: the seeds, and every input
that ever broke a promise (`regress-*`), so a fixed bug stays fixed. CI fuzzes for 120 s on every pull request,
from the seeds plus a corpus it keeps between runs (job `Host quality / fuzz`). 200 s of local fuzzing of the
current code (4.4 million runs, 652 coverage edges) broke no promise.

## Quality gates

The `Host quality` job measures the library code, `inc/` and `src/` only (tests and examples are left out), and
fails when a value is over the limits of this repository's assurance level, `high` (set in
[`.kokeb/handbook.yml`](../.kokeb/handbook.yml), defined by the handbook's ADR-0020):

| Gate | Tool and how it is measured | Limit at `high` | This driver (local run, GCC 15) |
| --- | --- | --- | --- |
| Complexity | lizard 1.24.1, modified cyclomatic complexity (`-m`: a `switch` counts once) per function | at most 15 | within the limit |
| Function length | lizard, lines of code per function | at most 60 | within the limit |
| Parameters | lizard, per function | at most 6 | within the limit |
| Line coverage | GCC 14 `--coverage -O0`, gcovr 8.7 `--merge-lines` (a template's lines count once across its instances), `--exclude-unreachable-branches --exclude-throw-branches` | at least 85 % | 99.3 % |
| Branch coverage | same run | at least 75 % | 90.4 % |
| MC/DC | GCC `-fcondition-coverage`, same run | measured, not gated at `high` (100 % at `safety`) | 90.0 % |
| Sanitizers | Clang, `-fsanitize=address,undefined -fno-sanitize-recover=all`, every CTest | no report | clean |
| Fuzzing | libFuzzer with ASan and UBSan, 120 s per target | no broken promise | clean |

The reports are in the run's `host-quality-reports` artifact and its step summary. To run the coverage gate
locally:

```bash
cmake -S . -B build-cov -DCMAKE_CXX_COMPILER=g++-14 -DAS5047U_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="--coverage -O0 -fcondition-coverage"
cmake --build build-cov && ctest --test-dir build-cov
gcovr --root . --filter inc/ --filter src/ --merge-lines --exclude-unreachable-branches --txt build-cov
lizard -m -C 15 -L 60 -a 6 inc src    # the complexity gate
```

clang-tidy 20 ([`_config/.clang-tidy`](../_config/.clang-tidy): bugprone, clang-analyzer, cppcoreguidelines,
modernize, performance, portability and readability, each disabled check with its reason) and cppcheck (strict)
run in the `C++ lint` and `Repository checks` jobs and fail on any finding.

## Documentation snippets

Every C++ block on this site is included from [`tests/docs/`](../tests/docs/my_port.hpp), which the host build
compiles (target `as5047u_doc_snippets`) with the driver instantiated over the documented port. A change to
the API that breaks a page breaks the build first. The README's example is a generated copy of the same file;
`kokeb-docs check` fails when it drifts.

## CI workflows

Everything runs from [`ci.yml`](../.github/workflows/ci.yml), which calls one workflow file per check and ends in
the `CI gate` job, the single required check (`CI gate / gate`).

| Job | What it checks |
| --- | --- |
| Host tests | GCC, then Clang (one job), build and run every test above, then build and run `examples/host` on its own |
| Host quality | The gates above: complexity, coverage, sanitizers, fuzzing |
| Cross-compile | `arm-none-eabi-g++` for Cortex-M0+, M3, M4F, M7F and M33F, warnings as errors |
| C++ lint | clang-format and clang-tidy over the sources |
| Docs | Strict site build (`_config/mkdocs.yml`), figure and generated-region drift, Doxygen (`_config/Doxyfile`), offline link check, Lighthouse |
| Repository checks | One job: DCO on pull requests, the org standard, Markdown and YAML lint, the [Kokeb Labs handbook](https://github.com/kokeb-labs/handbook) rules for `.kokeb/handbook.yml` (by rule ID), template drift (warns only) and cppcheck over the driver sources; a second job runs the handbook's source checks (include guards, bus requirements, raw access, the C++20 profile) |
| CI gate | Passes only if every job above passed: the one required check |

The **ESP32 examples** job builds every app in `examples/esp32/app_config.yml`, for each listed ESP-IDF version and
build type, with the repository root as the driver's component, and lints the example sources. The gate needs it
too.

The workflows call the shared, versioned workflows of
[kokeb-labs/ci-tools](https://github.com/kokeb-labs/ci-tools) at `@v1`.

## On hardware

The driver is **not verified on hardware** yet. An MCU example ([Examples](examples.md)) is the bench test: it logs
angle, speed and AGC twice a second. Turn the magnet by hand through a full turn and check that the angle follows
it from 0° to 360°, that the speed's sign follows the direction of rotation, and that the AGC stays well inside 0
to 255. Record the board and the result in the pull request's bench-status section. Test `ProgramOtp()` only on
parts you can scrap.

The start-up wait, the magnet warning and the line checks rest on the datasheet alone. Check each on the bench:

1. **Start-up wait.** Power the sensor from a switchable supply and call `EnsureInitialized()` in the same moment;
   log `GetLatchedErrorFlags()` and count the DIA polls on a logic analyser. Expected: `Ok` within 10 ms, at least
   two DIA reads with LoopsFinished set before the first settings write.
2. **No magnet.** Remove the magnet, start, and read the angle. Expected: `NotReady` at start-up or
   `MagnetWarning` on the angle read, never an angle; `ReadAgc()` near 255.
3. **Lines.** With the driver running in `Bits24`, disconnect MISO (pulled up, then pulled down), then MOSI, and
   read the angle. Expected: `NoDevice` (or `Crc`), never a value. `Bits16` frames have the limit described in
   [Status and limitations](limitations.md).
4. **A reset of the chip.** With the driver running, a configuration that differs from the OTP (a zero position
   other than 0) and the magnet still, cut the sensor's supply for 50 ms (or pull it below VDD minimum) and restore
   it while reading the angle every millisecond; log each result and `GetLatchedErrorFlags()`. Expected: the first
   read after the supply returns fails with `DeviceReset` (or `NoDevice` while the chip is unpowered), never an angle
   with the zero lost; `CheckForReset()` returns `DeviceReset` too. Record whether ERRFL held OffCompNotFinished
   on that first read: the driver's automatic check relies on it, the datasheet does not promise it.
5. **ERRFL after an error.** Provoke a CRC error (a corrupted MOSI frame from a test port) and check that the next
   read returns `Crc` and that DIA reads normally afterwards, with and without a `ReadDiagnostics()` in between.
