/**
 * @file as5047u_registers.hpp
 * @brief AS5047U register map: addresses, field masks and shifts.
 * @copyright Copyright (c) 2026 The driver-as5047u authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Addresses and fields follow the AS5047U datasheet (ams OSRAM DS000637, v1-01), section
 * "Register Description" (Figures 32 to 49). Every register is 14 bits wide on the bus; fields are
 * plain masks and shifts so the code does not depend on how a compiler lays out bit-fields.
 *
 * The non-volatile registers (`kDisable` to `kEcc`) hold a volatile copy that SPI writes change any
 * number of times; it is lost at power-off unless the OTP was burned (`AS5047U::ProgramOtp()`).
 * Their reset value is 0 on a part whose OTP was never burned.
 */
#ifndef AS5047U_REGISTERS_HPP
#define AS5047U_REGISTERS_HPP

#include <cstddef>
#include <cstdint>

/** @brief Register addresses and fields of the AS5047U (datasheet section "Register Description").
 */
namespace as5047u::reg {

// --- Volatile registers ----------------------------------------------------------------------

inline constexpr uint16_t kNop = 0x0000;     ///< NOP: no operation (Figure 32).
inline constexpr uint16_t kErrfl = 0x0001;   ///< ERRFL: error flags, cleared by a read (Figure 33).
inline constexpr uint16_t kProg = 0x0003;    ///< PROG: OTP programming control (Figure 34).
inline constexpr uint16_t kDia = 0x3FF5;     ///< DIA: diagnostics (Figure 35).
inline constexpr uint16_t kAgc = 0x3FF9;     ///< AGC: automatic gain control value.
inline constexpr uint16_t kSinData = 0x3FFA; ///< SINDATA: raw sine channel (signed 14 bit).
inline constexpr uint16_t kCosData = 0x3FFB; ///< COSDATA: raw cosine channel (signed 14 bit).
inline constexpr uint16_t kVel = 0x3FFC;     ///< VEL: angular velocity (signed 14 bit).
inline constexpr uint16_t kMag = 0x3FFD;     ///< MAG: CORDIC magnitude.
inline constexpr uint16_t kAngleUnc = 0x3FFE; ///< ANGLEUNC: angle without DAEC.
inline constexpr uint16_t kAngleCom = 0x3FFF; ///< ANGLECOM: angle with DAEC.
// ECC_s: Figure 40 and OTP procedure step 8 give 0x3FD0; the table of Figure 32 prints 0x00D1.
inline constexpr uint16_t kEccChecksum = 0x3FD0; ///< ECC_Checksum: ECC key of the OTP content.

// --- CRC-8 of every 24- and 32-bit frame (Figure 31) -----------------------------------------

inline constexpr uint8_t kCrcPolynomial = 0x1D; ///< x^8 + x^4 + x^3 + x^2 + 1, not reflected.
inline constexpr uint8_t kCrcInit = 0xC4;       ///< Initial value.
inline constexpr uint8_t kCrcFinalXor = 0xFF;   ///< XORed into the result.

// --- Non-volatile registers (OTP, with a volatile copy) --------------------------------------

inline constexpr uint16_t kDisable = 0x0015;   ///< DISABLE: output and filter disable.
inline constexpr uint16_t kZposm = 0x0016;     ///< ZPOSM: zero position, bits 13..6.
inline constexpr uint16_t kZposl = 0x0017;     ///< ZPOSL: zero position, bits 5..0, and Dia1/Dia2.
inline constexpr uint16_t kSettings1 = 0x0018; ///< SETTINGS1: velocity filter K_max, K_min.
inline constexpr uint16_t kSettings2 = 0x0019; ///< SETTINGS2: direction, DAEC, outputs.
inline constexpr uint16_t kSettings3 = 0x001A; ///< SETTINGS3: UVW pole pairs, hysteresis, ABIRES.
inline constexpr uint16_t kEcc = 0x001B;       ///< ECC: ECC checksum and enable.

/// First non-volatile register (DISABLE).
inline constexpr uint16_t kFirstNonVolatile = kDisable;
/// Number of non-volatile registers, DISABLE to ECC.
inline constexpr uint16_t kNonVolatileCount = 7;

/// Index of a non-volatile register in `NonVolatileRegisters` (address order from DISABLE).
enum class NvIndex : uint8_t {
  Disable = 0,   ///< DISABLE (0x0015).
  Zposm = 1,     ///< ZPOSM (0x0016).
  Zposl = 2,     ///< ZPOSL (0x0017).
  Settings1 = 3, ///< SETTINGS1 (0x0018).
  Settings2 = 4, ///< SETTINGS2 (0x0019).
  Settings3 = 5, ///< SETTINGS3 (0x001A).
  Ecc = 6,       ///< ECC (0x001B).
};

/**
 * @brief The array index of a non-volatile register.
 * @param index The register.
 * @return Its position in `NonVolatileRegisters`, 0 to 6.
 */
constexpr std::size_t At(NvIndex index) noexcept {
  return static_cast<std::size_t>(index);
}
static_assert((At(NvIndex::Ecc) + 1U) == kNonVolatileCount);

/// DISABLE implements bits 6..0 (UVW_off, ABI_off, bits 5..2 "na", FILTER_disable; Figure 43).
inline constexpr uint16_t kDisableImplemented = 0x7F;
/// ZPOSM to ECC implement bits 7..0 (Figures 44 to 49).
inline constexpr uint16_t kNvByteImplemented = 0xFF;

/**
 * @brief The bits a non-volatile register implements; the datasheet describes no others, so the
 * driver expects them to read 0 whatever was written there.
 * @param index Position in `NonVolatileRegisters`, 0 to 6.
 * @return `kDisableImplemented` for DISABLE, `kNvByteImplemented` for the rest.
 */
constexpr uint16_t NvImplementedBits(std::size_t index) noexcept {
  return (index == At(NvIndex::Disable)) ? kDisableImplemented : kNvByteImplemented;
}
static_assert((kFirstNonVolatile + At(NvIndex::Settings3)) == kSettings3);

// --- Frame bits --------------------------------------------------------------------------------

/// The 14 address or data bits of a frame word.
inline constexpr uint16_t kDataMask = 0x3FFF;
/// MOSI command word: bit 14 = 1 read, 0 write (Figures 21, 24, 27).
inline constexpr uint16_t kReadBit = 1U << 14;
/// MISO word: bit 14, an error flag is set in ERRFL (Figures 22, 25, 28).
inline constexpr uint16_t kReplyErrorBit = 1U << 14;
/// MISO word: bit 15, a warning flag is set in ERRFL (Figures 22, 25, 28).
inline constexpr uint16_t kReplyWarningBit = 1U << 15;
/// Both flag bits of a MISO word: when neither is set, the data bits are the whole answer.
inline constexpr uint16_t kReplyFlagBits = uint32_t{kReplyErrorBit} | uint32_t{kReplyWarningBit};

// --- ERRFL (Figure 33): cleared on the chip by reading it ------------------------------------

inline constexpr uint16_t kErrflAgcWarning = 1U << 0;   ///< AGC at its minimum or maximum.
inline constexpr uint16_t kErrflMagHalf = 1U << 1;      ///< Field at half the regulated value.
inline constexpr uint16_t kErrflP2ramWarning = 1U << 2; ///< ECC corrected one bit in P2RAM.
inline constexpr uint16_t kErrflP2ramError = 1U << 3;   ///< ECC found an uncorrectable P2RAM error.
inline constexpr uint16_t kErrflFraming = 1U << 4;      ///< SPI framing error.
inline constexpr uint16_t kErrflCommand = 1U << 5;      ///< Invalid SPI command.
inline constexpr uint16_t kErrflCrc = 1U << 6;          ///< CRC error on a MOSI frame.
inline constexpr uint16_t kErrflWatchdog = 1U << 7;     ///< Internal oscillator or watchdog fault.
inline constexpr uint16_t kErrflOffComp = 1U << 9;      ///< Offset compensation not finished.
inline constexpr uint16_t kErrflCordic = 1U << 10;      ///< CORDIC overflow.
/// The flags that mean the last frame was rejected.
inline constexpr uint16_t kErrflFrameRejected =
    uint32_t{kErrflFraming} | uint32_t{kErrflCommand} | uint32_t{kErrflCrc};
/// The flags that mean the measurement path is faulty.
inline constexpr uint16_t kErrflDeviceFault = uint32_t{kErrflP2ramError} |
                                              uint32_t{kErrflWatchdog} | uint32_t{kErrflOffComp} |
                                              uint32_t{kErrflCordic};
/// The warnings about the magnetic field: an angle read under either is not a measurement
/// (handbook KB-CPP-127).
inline constexpr uint16_t kErrflFieldWarning = uint32_t{kErrflAgcWarning} | uint32_t{kErrflMagHalf};
/// ERRFL bit 8 has no function and reads 0 (Figure 33): set, the reply cannot be ERRFL.
inline constexpr uint16_t kErrflReserved = 1U << 8;
/// ERRFL flags that may follow a reset of the chip after start-up: offset compensation not
/// finished (it restarts at power-up) and the watchdog (Figure 33). The driver then compares the
/// settings registers with what it wrote (`AS5047U::CheckForReset()`).
inline constexpr uint16_t kErrflResetHint = uint32_t{kErrflOffComp} | uint32_t{kErrflWatchdog};

// --- PROG (Figure 34) --------------------------------------------------------------------------

inline constexpr uint16_t kProgEnable = 1U << 0;  ///< PROGEN: enable OTP programming.
inline constexpr uint16_t kProgRefresh = 1U << 2; ///< OTPREF: reload the registers from the OTP.
inline constexpr uint16_t kProgBurn = 1U << 3;    ///< PROGOTP: start burning the OTP.
inline constexpr uint16_t kProgVerify = 1U << 6;  ///< PROGVER: guard-band verification.
/// PROG reads PROGEN alone once a burn has finished (OTP flowchart, Figure 60).
inline constexpr uint16_t kProgDone = kProgEnable;

// --- DIA (Figure 35) ---------------------------------------------------------------------------

inline constexpr uint16_t kDiaVdd5V = 1U << 0;           ///< VDD_mode: 1 = 5 V operation.
inline constexpr uint16_t kDiaLoopsFinished = 1U << 1;   ///< All magnetic loops have settled.
inline constexpr uint16_t kDiaCordicOverflow = 1U << 2;  ///< CORDIC overflow.
inline constexpr uint16_t kDiaCompLow = 1U << 3;         ///< AGC at its low limit.
inline constexpr uint16_t kDiaCompHigh = 1U << 4;        ///< AGC at its high limit.
inline constexpr uint16_t kDiaMagHalf = 1U << 5;         ///< Field at half the regulated value.
inline constexpr uint16_t kDiaCosOffsetDone = 1U << 6;   ///< Cosine offset loop finished.
inline constexpr uint16_t kDiaSinOffsetDone = 1U << 7;   ///< Sine offset loop finished.
inline constexpr uint16_t kDiaOffsetCompDone = 1U << 8;  ///< Offset compensation finished.
inline constexpr uint16_t kDiaAgcDone = 1U << 9;         ///< AGC loop finished.
inline constexpr uint16_t kDiaSpiCountShift = 11;        ///< SPI_cnt: frame counter, bits 12..11.
inline constexpr uint16_t kDiaSpiCountMask = 0x3U << 11; ///< SPI_cnt mask.
/// DIA bit 10 has no function and reads 0 (Figure 35): a reply with it set does not come from an
/// AS5047U. Bit 13 is not described (the reset value 0xX3C2 leaves bits 15..12 open), so it is
/// not checked; all ones is caught as a line pattern.
inline constexpr uint16_t kDiaReserved = 1U << 10;
/// DIA after power-up once every loop has settled, 3.3 V mode, SPI_cnt 0 (Figure 32: 0xX3C2).
inline constexpr uint16_t kDiaReadyValue = 0x03C2;

// --- DISABLE (Figure 43) -----------------------------------------------------------------------

inline constexpr uint16_t kDisableUvwOff = 1U << 0;    ///< UVW_off: UVW pins tristate.
inline constexpr uint16_t kDisableAbiOff = 1U << 1;    ///< ABI_off: A, B and I pins tristate.
inline constexpr uint16_t kDisableFilterOff = 1U << 6; ///< FILTER_disable: adaptive filter off.

// --- ZPOSM / ZPOSL (Figures 44, 45) ------------------------------------------------------------

inline constexpr uint16_t kZposmMask = 0xFFU;    ///< ZPOSM holds zero-position bits 13..6.
inline constexpr uint16_t kZposlMask = 0x3FU;    ///< ZPOSL bits 5..0 hold zero-position bits 5..0.
inline constexpr uint16_t kZposlDiaBits = 0xC0U; ///< ZPOSL bits 7..6: Dia1_en, Dia2_en (kept).
inline constexpr uint16_t kZeroHighShift = 6;    ///< Zero position bits 13..6 go in ZPOSM.

// --- SETTINGS1 (Figure 46) ---------------------------------------------------------------------

inline constexpr uint16_t kS1KmaxMask = 0x07U;      ///< K_max code, bits 2..0.
inline constexpr uint16_t kS1KminShift = 3;         ///< K_min code, bits 5..3.
inline constexpr uint16_t kS1KminMask = 0x07U << 3; ///< K_min mask.
inline constexpr uint16_t kS1DiaBits = 0xC0U;       ///< Dia3_en, Dia4_en (kept).

// --- SETTINGS2 (Figure 47) ---------------------------------------------------------------------

inline constexpr uint16_t kS2IndexWidth1 = 1U << 0; ///< IWIDTH: 1 = 1 LSB index pulse, 0 = 3 LSB.
inline constexpr uint16_t kS2NoiseSet = 1U << 1;    ///< NOISESET: reduced noise at 150 °C, 3.3 V.
inline constexpr uint16_t kS2Dir = 1U << 2;         ///< DIR: invert the rotation direction.
inline constexpr uint16_t kS2UvwAbi = 1U << 3;      ///< UVW_ABI: 0 = ABI with PWM on W, 1 = UVW.
inline constexpr uint16_t kS2DaecDis = 1U << 4;     ///< DAECDIS: dynamic compensation off.
inline constexpr uint16_t kS2AbiDecimal = 1U << 5;  ///< ABI_DEC: decimal ABI resolution.
inline constexpr uint16_t kS2DataSelect = 1U << 6;  ///< Data_select: 0x3FFF reads ANGLEUNC.
inline constexpr uint16_t kS2PwmOn = 1U << 7;       ///< PWMon: PWM output on.

// --- SETTINGS3 (Figure 48) ---------------------------------------------------------------------

inline constexpr uint16_t kS3UvwppMask = 0x07U;       ///< UVWPP: pole pairs minus one, bits 2..0.
inline constexpr uint16_t kS3HysShift = 3;            ///< HYS, bits 4..3.
inline constexpr uint16_t kS3HysMask = 0x03U << 3;    ///< HYS mask.
inline constexpr uint16_t kS3AbiresShift = 5;         ///< ABIRES, bits 7..5.
inline constexpr uint16_t kS3AbiresMask = 0x07U << 5; ///< ABIRES mask.

// --- ECC (Figure 49) and ECC_s (Figure 40) -----------------------------------------------------

inline constexpr uint16_t kEccChecksumMask = 0x7FU; ///< ECC_chsum / ECC_s: the 7-bit key.
inline constexpr uint16_t kEccEnable = 1U << 7;     ///< ECC_en.

} // namespace as5047u::reg

#endif // AS5047U_REGISTERS_HPP
