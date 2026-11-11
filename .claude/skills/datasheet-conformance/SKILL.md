---
name: datasheet-conformance
description: Check driver code against the chip's datasheet, citing sections, test first. Use when writing or reviewing register maps, timing, reset values or error handling.
---

# Datasheet conformance

The datasheet (link only; never commit the vendor PDF):
[AS5047U, ams OSRAM DS000637](https://ams-osram.com/products/sensor-solutions/position-sensors/ams-as5047u-high-resolution-rotary-position-sensor).

Every number in the driver has a source. Work one subsystem at a time (presence check, frame formats and CRC,
ERRFL handling, one register block, the OTP procedure). For the AS5047U the source is DS000637 v1-01; cite its
section and figure numbers ("SPI Transaction", Figures 20 to 28; "Register Description", Figures 32 to 51; the OTP
procedure and Figure 60).

1. **Collect the facts** from the datasheet into a table in the PR description: register, address, bits,
   reset value, access (R, W, R/W, self-clearing, clear-on-read), timing (minimum, typical, maximum), and the
   section, table or figure number. Use the maximum for waits and the minimum for pulses.
2. **Write the model first.** Make `tests/mock_<chip>.hpp` follow the table: reset values, read-only and
   self-clearing bits, clear-on-read flags, timing, invalid writes.
3. **Write the tests** that pin each fact: reset values read back after `EnsureInitialized()`, the exact bytes of each
   frame and its CRC, the waits (the OTP burn poll against `kOtpBurnTimeoutMs`), every documented error condition.
4. **Then change the driver** until the tests pass. Each register access and wait carries a comment with the
   section or figure (`// ERRFL, Figure 34`).
5. **Cross-check** `Config{}` against the reset values, `Validate()` against the documented ranges, and every
   `Error` against a datasheet condition or a port failure.
6. **Mark what you could not verify**: values the datasheet leaves open, errata, anything inferred. Say so in the
   code comment and the PR, and list it under "not verified on hardware" with a bench step.

Never invent a register, bit or timing. If the datasheet is silent, ask, or leave the feature out.
