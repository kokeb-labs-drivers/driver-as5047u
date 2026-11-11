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
