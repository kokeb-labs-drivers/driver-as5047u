# Porting

Run the driver on a new MCU by writing one class: the port.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From ams OSRAM DS000637 v1-01:

    - Power-up time and SPI timing: Timing Characteristics; SPI Timing, p. 9
    - SPI mode, frames and the read pipeline: SPI Interface (Slave); SPI Transaction, p. 16

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

The driver never calls your SDK. It calls hooks on a class you derive from `as5047u::SpiInterface<Self>` (in
[`inc/as5047u_spi_interface.hpp`](../inc/as5047u_spi_interface.hpp)). The base uses CRTP and declares no hook
itself; the driver template is constrained by the `as5047u::SpiPort` concept, so calls are resolved at compile
time (no virtual functions, nothing to allocate) and a missing or misdeclared hook is a compile error at the point
of use. The concept checks each hook's exact parameter types and `noexcept`: a `Transfer()` that takes a `uint16_t`
length, or one without `noexcept`, does not satisfy it.

## What you implement

| Method | Required | Contract |
| --- | --- | --- |
| `bool EnsureInitialized() noexcept` | yes | Bring the bus up on the first call; return `true` at once afterwards. `false` makes `EnsureInitialized()` return `Bus` |
| `bool Transfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept` | yes | One full-duplex frame of `len` bytes (2, 3 or 4) with CSn low for all of them. `rx` is never `nullptr`: every frame carries a reply. `false` on any bus error |
| `void DelayUs(uint32_t us) noexcept` | yes | Wait at least `us` microseconds, never less: start-up polls DIA and `ProgramOtp()` polls the burn once per millisecond |
| `void Log(LogLevel, const char* message) noexcept` | no | Print one line; `message` is plain text, never a format string |

The chip has no reset or interrupt pin, so there is no GPIO hook. CSn is the only control output: it idles high,
and the port drives it high before it enables the pin as an output and before the first frame (handbook KB-HW-018).
A port whose SPI peripheral drives CSn gets this from the peripheral; a port that drives CSn as a GPIO writes the
level first, then sets the direction.

## The port

```cpp title="my_port.hpp"
--8<-- "tests/docs/my_port.hpp:port"
```

Rules the transfer must follow:

- **One call, one frame.** Keep CSn low for all `len` bytes and raise it afterwards, for at least `kMinCsHighNs`.
  The chip executes a frame at the rising CSn edge and finds its length from the clocks in between, so two frames in
  one chip-select period are a framing error.
- **The bus requirements below**: mode 1, MSB first, up to `kMaxClockHz`. Start at 1 MHz on jumper wires.
- **Full duplex.** Clock `tx` out and fill `rx` with what the chip sends in the same frame. `tx` and `rx` are the
  driver's stack buffers, valid only during the call: a port that uses DMA copies them into DMA-capable memory it
  owns (handbook KB-HW-019).
- **Raw bytes only.** Do not interpret, retry or reorder bytes; the driver owns the framing, the CRC and the
  error handling.
- **Return `false` for a bus error.** The driver turns it into `Error::Bus` and never ignores it.
- **Bounded time-outs.** A stuck bus fails the call; it does not block forever.
- **No other access between the two frames of a read.** A read is the command frame and the NOP frame that carries
  the answer, inside one driver call (handbook KB-ARCH-043). On a shared bus, hold the bus lock for the whole driver
  call, not per frame.

## Bus requirements

The driver states what the AS5047U needs from the bus as `constexpr` values in
[`inc/as5047u_spi_interface.hpp`](../inc/as5047u_spi_interface.hpp) (handbook KB-CPP-123). Check your port's
settings against them with `static_assert`, as both example ports do, so a port that clocks too fast or in the
wrong mode does not compile:

| Constant | Value | Meaning (datasheet "SPI Interface (Slave)", "SPI Timing", Figures 18 and 19) |
| --- | --- | --- |
| `kSpiMode` | `1` | CPOL 0, CPHA 1: MOSI sampled on the falling edge of CLK |
| `kMsbFirst` | `true` | Most significant bit first |
| `kMaxClockHz` | `10'000'000` | t_clk at least 100 ns |
| `kMinCsHighNs` | `350` | CSn high between two frames, t_CSn (2 t_CLK_SYS + 10 ns) |
| `kCsSetupNs`, `kCsHoldNs` | `350`, `50` | CSn falling edge to the first CLK rising edge, t_L; last CLK falling edge to CSn rising edge, t_H (t_clk / 2) |
| `kContinuousWord` | `false` | Idle CLK time between the bytes of one frame is allowed; CSn must stay low |
| `kFrameBits` | `{16, 24, 32}` | The frame lengths the chip accepts (Figures 20, 23, 26) |

## The delay and the optional hook

The driver finds the hooks beyond the bus at compile time, with named concepts in one place (`as5047u::detail`:
`DeclaresDelayUs`/`HasDelayUs`, `DeclaresLog`/`HasLog`). A missing hook falls back in a documented, safe way or
fails to compile; it never skips a step silently. A hook that is declared with the wrong signature (`DelayUs(uint16_t)`,
a `Log` without `noexcept`) is a compile error, not a hook the driver quietly ignores.

| Hook | Used for | Without it |
| --- | --- | --- |
| `DelayUs()` | 1 ms between polls of DIA at start-up and of the OTP burn in `ProgramOtp()` | Compile error at `EnsureInitialized()`: the waits are required |
| `Log()` | One line per lifecycle event or failure | Nothing is logged |

A port with every hook:

```cpp
--8<-- "tests/docs/ports.cpp:full-port"
```

The start-up wait and the burn wait are requirements of the chip, so a port without `DelayUs()` is a compile error,
not a start without waiting. The [compile-fail tests](testing.md) check this rule, the exact hook signatures, and
that a class which is not a port is rejected.

## Reference implementations

Every port lives with its example, in `examples/<platform>/`; the driver has none. [Platforms](platforms.md) lists
them and how to add one.

- [`examples/host/host_as5047u_port.hpp`](../examples/host/host_as5047u_port.hpp): any PC, against a simulated
  chip. Start from it for a host build or a test bench; it is also the smallest complete port.
- [`examples/esp32/main/esp32_as5047u_port.hpp`](../examples/esp32/main/esp32_as5047u_port.hpp): ESP-IDF, with
  every hook. Start from it for any ESP-IDF target.

## Check your port

1. Build it: the ports on this page are compiled by the host CI from [`tests/docs/`](../tests/docs/my_port.hpp),
   with the driver instantiated over them.
2. Call `EnsureInitialized()`: `Ok` proves that reads, writes and the CRC work and that an AS5047U answers and is
   ready.
3. Call `ReadAngleDegrees()` twice a second and turn the magnet by hand: the angle follows it over the full turn.
   `ReadAgc()` should read well inside 0 to 255 with the magnet in place.

4. With a scope on CSn and CLK, measure t_L, t_H and t_CSn at the clock you use against `kCsSetupNs`, `kCsHoldNs`
   and `kMinCsHighNs`; and check that CSn is high from reset until the first frame.

These checks are **not verified on hardware** for the ports in this repository yet.

## Threads

The driver is not thread-safe. Give each driver object one owner task, or guard every call with one mutex. When
several chips share a bus, the port must also serialise the bus. See [Contract](contract.md#threading).

## Editor setup

The clang-format and clang-tidy configuration is in `_config/`, and the repository root has no copy of it. Tools
that take a path use it directly; CI does the same:

| Tool | Setting |
| --- | --- |
| Command line | `clang-format --style=file:_config/.clang-format`, `clang-tidy --config-file=_config/.clang-tidy -p build` |
| VS Code (C/C++ extension) | `"C_Cpp.clang_format_style": "file:${workspaceFolder}/_config/.clang-format"` and `"C_Cpp.codeAnalysis.clangTidy.args": ["--config-file=${workspaceFolder}/_config/.clang-tidy"]` |
| clangd (VS Code clangd, Neovim, Emacs), CLion | These find `.clang-format` and `.clang-tidy` only by name, from the file upwards: copy both to the root once (`cp _config/.clang-format _config/.clang-tidy .`). `.gitignore` lists the copies, so they never reach a commit; copy again after `_config/` changes |

Configure with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` (clangd: `--compile-commands-dir=build`) so the editor sees the
same flags as the build.
