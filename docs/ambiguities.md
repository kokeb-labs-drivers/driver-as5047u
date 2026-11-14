# Ambiguities

Places where the datasheet (DS000637, v1-01) gives two answers to one question, the reading the driver takes and
why. Page numbers are the datasheet's printed ones.

| # | The datasheet says | Reading | Where it shows |
| --- | --- | --- | --- |
| A1 | The highest ABI resolution: the general description (p. 1) says 16989 steps or 4096 pulses per revolution; the electrical characteristics (RES_ABI, p. 7) give 4096 steps at most; the ABI settings table (Figure 51, p. 32) lists 4096 pulses, 16384 steps, for ABIRES = 100 | Figure 51: 4096 pulses (16384 steps) per revolution. It is the table the ABIRES and ABI_DEC codes are written from, and its pulse and step counts agree (four steps per pulse); 16989 matches no code and 4096 steps is the default setting's step count | `Config::abi_resolution_code` (4 is the highest); the hero tile "4096 ppr" and the coverage table |
| A2 | The address of ECC_Checksum: the volatile register table (Figure 32, p. 24) prints `0x00D1`; the register description (ECC_s, Figure 40, p. 27), the ECC section (p. 38) and the OTP programming procedure (p. 41) give `0x3FD0` | `0x3FD0`: three places, including the procedure the driver follows, agree on it | `kEccChecksum` in `inc/as5047u_registers.hpp`; `ProgramOtp()` steps 7 to 9 ([OTP programming](otp.md)) |

Both readings are what the host-test model implements; neither is verified on a chip ([Status and
limitations](limitations.md)).
