# Status and limitations

What this driver has and has not been shown to do, so you can decide whether it is ready for your use.

## Status

| | |
| --- | --- |
| **Version** | 0.1.0, not released |
| **On hardware** | **Not verified.** No board has run this driver yet |
| **Tested against** | A frame-level model of the chip ([Testing and CI](testing.md)), a fuzzer and the quality gates |
| **Assurance level** | `high` (Kokeb Labs handbook): not qualified for use in a safety function |

## What the model cannot show

The host tests and the fuzzer run the driver against a model written from the datasheet, by the same reading of the
datasheet as the driver. They prove the driver keeps its promises against that reading; they cannot prove the
reading is right. In particular the model does not reproduce:

- the ECC algorithm of the OTP block (the model's checksum is a simple sum), the fuses or the guard-band level;
- the magnetic signal path: AGC, magnitude, DAEC and the velocity filter are values a test sets;
- any timing except the OTP burn time and the power-on time t_pon (a virtual clock): SPI timing and the 16-bit
  read latency need a bench;
- the DIA value before the loops settle (the datasheet gives none; the model reads some loops done) and whether
  two DIA reads in a row both report ready on a real part;
- what the settings registers read in their undescribed bits (DISABLE 13..7, the others 13..8, Figures 43 to 49):
  the model reads them as 0 and the reset compare ignores them;
- what ERRFL holds right after a power-on reset (the model sets OffCompNotFinished until t_pon has passed);
- whether the flag bits of the reply that carries ERRFL describe ERRFL before or after the read clears it (the
  driver accepts both, and refuses only a flag bit with ERRFL empty);
- which ERRFL flags set the reply's error bit and which its warning bit: the datasheet names both bits (Figure 22)
  and calls each flag an error or a warning (Figure 33), but gives no mapping. The driver therefore judges a
  flagged reply by ERRFL, not by which bit was set, and the model tests both readings;
- whether DIA bit 13 reads 0 (the datasheet leaves it undescribed, so the driver does not check it);
- the OTP burn time: the datasheet gives none, and `kOtpBurnTimeoutMs` (100 ms) is the driver's bound;
- how the chip treats the NOP read the driver sends after a failed transfer when a write was pending. The driver
  relies on the datasheet's frame rules (a data frame after a write command, 14 data bits), under which that frame
  writes 0; confirm it on a bench before relying on `ProgramOtp()` over an unreliable bus.

## What the driver cannot detect

- **A reset of the chip that ERRFL does not report, or that changes nothing.** The datasheet says a hardware reset
  loses the soft-written settings and reloads the OTP ("Non-Volatile Registers (OTP)"), and that OffCompNotFinished
  (ERRFL bit 9, Figure 33) is set while the offset compensation is not finished. It does **not** say that ERRFL
  holds that flag after a power-on reset, or for how long: the driver's automatic check (on OffCompNotFinished or
  WDTST after start-up) is a reading of Figure 33, **not verified on hardware** (bench step 4 in
  [Testing and CI](testing.md#on-hardware)). If a real part clears the flag before the next read, only
  `CheckForReset()` sees the reset: call it periodically (14 frames). Neither sees a reset that leaves the settings
  as they were (a configuration equal to the OTP), which loses nothing, or one between the check and the next read.
  The model raises the flag at every reset, so the host tests cannot show what the chip does.
- **16-bit frames carry no CRC.** MISO stuck low, or a chip that hears MOSI as all zeros, reads as angle 0 with no
  flag bit; nothing in the frame tells it from a real 0. Floating high and looped-back lines are still caught. Use
  `Bits24` (the default) wherever a stuck line must never read as a position.
- **A write without read-back cannot see a looped-back MOSI**: the echo of the data frame carries no flag. Writes
  that matter use `Verify::ReadBack`, and `EnsureInitialized()` and `ProgramOtp()` always read back.
- **The datasheet asks for a DIA read after an error flag** ("Volatile Registers", after Figure 33). The driver
  returns the error and leaves that read to the caller (`ReadDiagnostics()`), so the error path stays two frames
  shorter; confirm on a bench whether the chip needs it.

## Before you depend on it

1. Run the bench check in [Testing and CI](testing.md#on-hardware) on your board: a full turn of the magnet, the
   direction of the velocity sign, the AGC range, the start-up wait, the magnet warning and the line checks.
2. Burn the OTP only on parts you can scrap until `ProgramOtp()` has been verified on hardware.
3. Report what you found in an issue, with the board and the driver commit; a verified board is listed here.

## Hardware verification record

| Board | Driver commit | Procedure | Result | Evidence |
| --- | --- | --- | --- | --- |
| (none yet) | | | | |
