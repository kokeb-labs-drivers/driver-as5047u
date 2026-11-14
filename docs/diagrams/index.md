# Figures

<!-- Written by kokeb-docs figures from docs/diagrams/figures/*.py. Do not edit by hand. -->

Every figure on this site is drawn by a Python module from facts extracted from the code, so it is rebuilt,
not redrawn, when the code changes. CI runs `kokeb-docs figures --check` and fails when a
committed figure no longer matches the code. Each figure below lists the files it is drawn from.

## Inside the AS5047U

<!-- figure: chip-blocks -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-blocks.dark.svg">
  <img src="chip-blocks.svg"
    alt="Block diagram of the AS5047U: its function blocks, how they connect and which package pins reach
    each block.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/as5047u_spi_interface.hpp`](../../inc/as5047u_spi_interface.hpp)

## What the AS5047U does

<!-- figure: chip-capabilities -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-capabilities.dark.svg">
  <img src="chip-capabilities.svg"
    alt="Capability map of the AS5047U: Absolute angle (supported): Measures the magnet's angle over a full
    turn in 14 bits, with or without the dynamic compensation; Rotation speed (supported): Measures the
    angular velocity in 14 bits, 24.141 °/s per count, up to 28000 rpm either way; DAEC and adaptive
    filter (supported): Corrects the angle for the measurement delay at speed, and filters transition
    noise at low speed; Zero position and direction (supported): Sets which angle reads 0 and which way
    the count rises, seen from above the chip; ABI incremental interface (supported): Quadrature A and B
    with an index pulse, 25 to 4096 pulses per revolution; UVW commutation and PWM (supported): Emulates
    the Hall switches of a BLDC motor for 1 to 7 pole pairs, or puts the angle out as PWM; Magnet and
    fault diagnostics (supported): Reports the gain control, the field magnitude and the error flags;
    each flag becomes a driver error; SPI frames with CRC (supported): 16-bit frames for the fastest
    reads, or 24- and 32-bit frames with a CRC-8 and a daisy-chain pad byte; OTP programming (guarded):
    Burns the zero and the settings into the chip once, with the datasheet's step-by-step procedure.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/as5047u_spi_interface.hpp`](../../inc/as5047u_spi_interface.hpp)

## AS5047U: 3.3 V operation

<!-- figure: chip-circuit-supply-3v3 -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-circuit-supply-3v3.dark.svg">
  <img src="chip-circuit-supply-3v3.svg"
    alt="Schematic of the AS5047U 3.3 v operation: the chip with U2 MCU, C1 capacitor 100 nF; the MCU wires
    carry the port hooks Transfer().">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/as5047u_spi_interface.hpp`](../../inc/as5047u_spi_interface.hpp)

## AS5047U: Minimum circuit, 5 V

<!-- figure: chip-circuit-typical -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-circuit-typical.dark.svg">
  <img src="chip-circuit-typical.svg"
    alt="Schematic of the AS5047U minimum circuit, 5 v: the chip with U2 MCU, C1 capacitor 100 nF, C2
    capacitor 1 µF; the MCU wires carry the port hooks Transfer().">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/as5047u_spi_interface.hpp`](../../inc/as5047u_spi_interface.hpp)

## AS5047U: 16-bit SPI frame

<!-- figure: chip-frame-spi16 -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-frame-spi16.dark.svg">
  <img src="chip-frame-spi16.svg"
    alt="16-bit SPI frame of the AS5047U, 16 bits, most significant bit first. Command: don't care (bit 15),
    R (bit 14), ADDR[13:0] (bits 13 to 0); Reply: Warning (bit 15), Error (bit 14), DATA[13:0] (bits 13
    to 0). Below, the SPI mode 1 waveform of one transfer in the same bit grid.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/as5047u_spi_interface.hpp`](../../inc/as5047u_spi_interface.hpp)

## AS5047U: 24-bit SPI frame with CRC

<!-- figure: chip-frame-spi24 -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-frame-spi24.dark.svg">
  <img src="chip-frame-spi24.svg"
    alt="24-bit SPI frame with CRC of the AS5047U, 24 bits, most significant bit first. Command: don't care
    (bit 23), RW (bit 22), ADDR[13:0] (bits 21 to 8), CRC-8 (bits 7 to 0); Reply: Warning (bit 23),
    Error (bit 22), DATA[13:0] (bits 21 to 8), CRC-8 (bits 7 to 0). Below, the SPI mode 1 waveform of
    one transfer in the same bit grid.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/as5047u_spi_interface.hpp`](../../inc/as5047u_spi_interface.hpp)

## AS5047U

<!-- figure: chip-hero -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-hero.dark.svg">
  <img src="chip-hero.svg"
    alt="Overview of the AS5047U, a magnetic rotary position sensor: it measures the absolute angle of a
    rotating magnet over a full 360 degrees in 14 bits and gives it over SPI with a CRC, and as
    incremental ABI, UVW commutation or PWM signals. A MCU talks to it over SPI with CRC-8; it drives
    Motor controller; it reads back Magnet on the shaft. Headline figures: 14 bits angle resolution,
    16384 counts per turn; 28000 rpm maximum speed; 10 MHz SPI clock, mode 1; 4096 ppr ABI pulses per
    revolution, at most; 3.3 or 5 V supply; 5 V through the on-chip LDO; 5 x 4.4 mm TSSOP-14 body.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/as5047u_spi_interface.hpp`](../../inc/as5047u_spi_interface.hpp)

## AS5047U package, TSSOP-14

<!-- figure: chip-outline -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-outline.dark.svg">
  <img src="chip-outline.svg"
    alt="Outline of the AS5047U in TSSOP-14 drawn to scale from above: body 5.0 x 4.4 x 1.2 mm, pitch 0.65
    mm, 14 pins.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/as5047u_spi_interface.hpp`](../../inc/as5047u_spi_interface.hpp)

## AS5047U pinout, TSSOP-14

<!-- figure: chip-pinout -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-pinout.dark.svg">
  <img src="chip-pinout.svg"
    alt="Pinout of the AS5047U in TSSOP-14, top view: 14 pins, each with its name, its electrical type as a
    colour and the driver hook that uses it.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/as5047u_spi_interface.hpp`](../../inc/as5047u_spi_interface.hpp)

## Wiring: 3.3 V operation

<!-- figure: chip-wiring-supply-3v3 -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-wiring-supply-3v3.dark.svg">
  <img src="chip-wiring-supply-3v3.svg"
    alt="Wiring of the AS5047U for the 3.3 V operation: MCU SPI SCK to CLK (Transfer()); MCU SPI MOSI to MOSI
    (Transfer()); MCU SPI MISO to MISO (Transfer()); MCU GPIO / CS to CSn (Transfer()). Supplies,
    grounds and the support parts are listed under the drawing.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/as5047u_spi_interface.hpp`](../../inc/as5047u_spi_interface.hpp)

## Wiring: Minimum circuit, 5 V

<!-- figure: chip-wiring-typical -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-wiring-typical.dark.svg">
  <img src="chip-wiring-typical.svg"
    alt="Wiring of the AS5047U for the Minimum circuit, 5 V: MCU SPI SCK to CLK (Transfer()); MCU SPI MOSI to
    MOSI (Transfer()); MCU SPI MISO to MISO (Transfer()); MCU GPIO / CS to CSn (Transfer()). Supplies,
    grounds and the support parts are listed under the drawing.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/as5047u_spi_interface.hpp`](../../inc/as5047u_spi_interface.hpp)

## The layers, one of them yours

<!-- figure: driver-layers -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="driver-layers.dark.svg">
  <img src="driver-layers.svg"
    alt="Layer diagram: your application calls the AS5047U core, which sends register reads and writes
    through the SPI port contract to the port class you write, which drives the chip; the core and the
    contract are the same on every MCU.">
</picture>
<!-- /figure -->

Figure module: [`docs/diagrams/figures/driver-layers.py`](../../docs/diagrams/figures/driver-layers.py)

Drawn from:

- `public calls` in [`inc/as5047u.hpp`](../../inc/as5047u.hpp)
- `SpiInterface` in [`inc/as5047u_spi_interface.hpp`](../../inc/as5047u_spi_interface.hpp)

## One call through the layers

<!-- figure: one-call-through-the-layers -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="one-call-through-the-layers.dark.svg">
  <img src="one-call-through-the-layers.svg"
    alt="One call through the layers: your code calls ReadAngleDegrees(), the core exchanges frames with the
    chip through your port and returns the result to your code.">
</picture>
<!-- /figure -->

Hardware description: [`docs/model/one-call-through-the-layers.seq.yml`](../model/one-call-through-the-layers.seq.yml)

Drawn from:

- [`docs/model/one-call-through-the-layers.seq.yml`](../model/one-call-through-the-layers.seq.yml)

## ProgramOtp(), step by step

<!-- figure: otp-flow -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="otp-flow.dark.svg">
  <img src="otp-flow.svg"
    alt="Flowchart of ProgramOtp: prepare (steps 6 to 11), burn (steps 12 to 14, polled with a 100 ms
    timeout) and guard-band verify (steps 15 to 19); burn_started becomes true at step 13, and each step
    can stop the call with an error.">
</picture>
<!-- /figure -->

Figure module: [`docs/diagrams/figures/otp-flow.py`](../../docs/diagrams/figures/otp-flow.py)

Drawn from:

- `ProgramOtp` in [`src/as5047u.ipp`](../../src/as5047u.ipp)
- `otpPrepare` in [`src/as5047u.ipp`](../../src/as5047u.ipp)
- `otpBurn` in [`src/as5047u.ipp`](../../src/as5047u.ipp)
- `otpVerify` in [`src/as5047u.ipp`](../../src/as5047u.ipp)
- `kOtpBurnTimeoutMs` in [`inc/as5047u_types.hpp`](../../inc/as5047u_types.hpp)
- `PROG and ECC` in [`inc/as5047u_registers.hpp`](../../inc/as5047u_registers.hpp)

## Every access is two frames

<!-- figure: read-pipeline -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="read-pipeline.dark.svg">
  <img src="read-pipeline.svg"
    alt="Timing of SPI frames: a read sends the command and then a NOP read (0x4000), because the chip
    answers each frame during the next one; a write sends a command frame and a data frame, then reads
    the register back.">
</picture>
<!-- /figure -->

Figure module: [`docs/diagrams/figures/read-pipeline.py`](../../docs/diagrams/figures/read-pipeline.py)

Drawn from:

- `readRaw` in [`src/as5047u.ipp`](../../src/as5047u.ipp)
- `writeRaw` in [`src/as5047u.ipp`](../../src/as5047u.ipp)
- `resolveFlags` in [`src/as5047u.ipp`](../../src/as5047u.ipp)
- `detail::ClassifyErrorFlags` in [`inc/as5047u.hpp`](../../inc/as5047u.hpp)
- `frame bits` in [`inc/as5047u_registers.hpp`](../../inc/as5047u_registers.hpp)
- `FrameBus::Exchange` in [`inc/as5047u_spi_interface.hpp`](../../inc/as5047u_spi_interface.hpp)

## SPI frames, bit by bit

<!-- figure: spi-frames -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="spi-frames.dark.svg">
  <img src="spi-frames.svg"
    alt="Bit layouts of the 16-, 24- and 32-bit SPI frames: on MOSI the R/W bit and a 14-bit address, on MISO
    a warning bit, an error bit and 14 data bits; 24-bit frames add a CRC-8 byte, and 32-bit frames add
    a pad byte, first on MOSI and last on MISO.">
</picture>
<!-- /figure -->

Figure module: [`docs/diagrams/figures/spi-frames.py`](../../docs/diagrams/figures/spi-frames.py)

Drawn from:

- `frame bits` in [`inc/as5047u_registers.hpp`](../../inc/as5047u_registers.hpp)
- `detail::FrameBus::Exchange` in [`inc/as5047u_spi_interface.hpp`](../../inc/as5047u_spi_interface.hpp)
- `Crc8` in [`inc/as5047u_spi_interface.hpp`](../../inc/as5047u_spi_interface.hpp)
- `writeFormat` in [`inc/as5047u.hpp`](../../inc/as5047u.hpp)

## One read on the SPI bus

<!-- figure: spi-read -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="spi-read.dark.svg">
  <img src="spi-read.svg"
    alt="One AS5047U read on the SPI bus: the driver sends the ANGLECOM read, discards the reply to the
    previous frame, sends a NOP read to collect the angle with its flags and CRC, and reads ERRFL if an
    error bit is set.">
</picture>
<!-- /figure -->

Hardware description: [`docs/model/spi-read.seq.yml`](../model/spi-read.seq.yml)

Drawn from:

- [`docs/model/spi-read.seq.yml`](../model/spi-read.seq.yml)
