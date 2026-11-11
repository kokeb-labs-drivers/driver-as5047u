---
hide:
  - navigation
  - toc
---

<div class="kb-hero" markdown>
<span class="kb-kicker">Driver</span>

# AS5047U Driver

<p>A C++20 driver for the AS5047U 14-bit magnetic rotary position sensor. It reads the angle and the velocity
over CRC-protected SPI frames, resolves the chip's error flags, sets the zero and the outputs, and burns the OTP,
on any MCU.</p>

[Quickstart](quickstart.md){ .md-button .md-button--primary }
[API guide](api-guide.md){ .md-button }
[Source on GitHub](https://github.com/kokeb-labs-drivers/driver-as5047u){ .md-button }
</div>

The driver has no platform code. You give it a small class that brings the bus up and moves bytes; it does the
rest: the start-up wait for the chip's ready indication, the frame format and CRC, ERRFL handling and its latch,
unit conversion and the OTP procedure.

<div class="kb-chip" markdown>
<div class="kb-chip__head">
<span class="kb-chip__kicker">Magnetic rotary position sensor</span>
<span class="kb-chip__name">AS5047U</span>
<span class="kb-chip__vendor">ams OSRAM</span>
</div>

Function
:   Absolute angle over 360°, 14 bits (16384 counts, 0.022° per count); velocity at 24.141 °/s per count; up to
    28000 rpm

Interface
:   SPI mode 1, up to 10 MHz; 16-bit frames, or 24- and 32-bit frames with CRC-8

Compensation
:   Dynamic angle error compensation (DAEC) and an adaptive velocity filter

Integrity
:   CRC-8 on frames; ERRFL error and warning flags; AGC and DIA for the magnet and the internal loops

Outputs
:   ABI quadrature (up to 4096 pulses per revolution), UVW commutation (1 to 7 pole pairs) and PWM

Non-volatile memory
:   One-time-programmable zero position and settings

Supply
:   3.0 V to 3.6 V, or 4.5 V to 5.5 V through the on-chip LDO
</div>


## What you get

- **One object per chip.** `AS5047U<YourPort>` takes a reference to your port and does no I/O until
  `EnsureInitialized()`. See [API guide](api-guide.md).
- **Errors you can act on.** Every call returns `Result<T>` with an `Error` such as `Bus`, `Crc` or
  `DeviceFault`. See [API guide](api-guide.md#handle-errors).
- **Datasheet defaults.** `Config{}` holds the reset values of an unprogrammed part; a burned part is read back
  instead. See [Configuration](configuration.md) and [OTP programming](otp.md).
- **Zero-overhead portability.** The port is a CRTP class checked by a concept: two bus hooks, plus optional
  delay (for the OTP burn) and logging hooks found at compile time. See [Porting](porting.md).
- **No heap, no exceptions, no RTTI.** The host tests build with `-fno-exceptions -fno-rtti`. See
  [Contract](contract.md).
- **Tested without hardware.** Host tests run the driver against a register model of the chip. See
  [Testing and CI](testing.md).

## Datasheet

<!-- generated: datasheet-card -->
| Document | Revision | Get it |
| --- | --- | --- |
| Datasheet: AS5047U High Resolution Rotary Position Sensor (ams OSRAM DS000637) | v1-01, 2020-12-16 | [vendor page](https://ams-osram.com/products/sensor-solutions/position-sensors/ams-as5047u-high-resolution-rotary-position-sensor) |

Written against these revisions; the SHA-256 of each file is in [`docs/datasheets.yml`](datasheets.yml). Where each part
of the driver is in the datasheet: [Datasheet map](datasheet.md).
<!-- /generated -->

## Where to go next

<div class="grid cards" markdown>

-   :material-rocket-launch-outline: **[Quickstart](quickstart.md)**

    ---

    Wire the chip, write the port, read the angle in about 15 lines.

-   :material-chip: **[Hardware](hardware.md)**

    ---

    Pins, SPI frames, timing, power and the magnet.

-   :material-file-document-check-outline: **[Contract](contract.md)**

    ---

    What the driver promises, what it needs, threading and errors.

-   :material-code-braces: **[API guide](api-guide.md)**

    ---

    Initialise, measure, set the zero and handle errors, task by task.

-   :material-swap-horizontal: **[Porting](porting.md)**

    ---

    Implement the port for your MCU.

-   :material-book-open-variant: **[API reference](api-reference.md)**

    ---

    Every class and member, generated from the headers.

</div>
