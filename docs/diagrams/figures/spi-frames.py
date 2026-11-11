"""The AS5047U SPI frames: 16, 24 and 32 bits, MOSI and MISO, bit by bit.

Reads: Each frame is drawn in the order it is clocked, most significant bit first, and the numbers above a row are
bit positions in that frame. On MOSI the 16-bit command word holds the R/W bit (1 to read, 0 to write) and a 14-bit
address; the data frame of a write has the same layout with R/W at 0 and the 14-bit value in place of the
address. On MISO the reply word holds the warning bit, the error bit (a flag is set in ERRFL) and 14 data bits.
The 24-bit frame adds the CRC-8 of the word in both directions. The 32-bit frame adds the pad byte: first on MOSI
(the driver sends Config::pad_byte), last on MISO (the driver ignores it). The bit positions come from the frame
constants in inc/as5047u_registers.hpp and the byte order from detail::FrameBus::Exchange in
inc/as5047u_spi_interface.hpp. With 16-bit frames the driver sends writes as 24-bit frames.
"""

import re

from _source import REGISTERS, SPI, UMBRELLA, constants, need, read

from figkit import Fig

TONES = {"zero": "slate", "rw": "green", "addr": "blue", "warn": "amber", "err": "rose", "crc": "teal",
         "pad": "violet"}


def _bit(mask: int) -> int:
    if mask & (mask - 1):
        raise SystemExit(f"figure source drift: {mask:#x} is not a single bit")
    return mask.bit_length() - 1


def data():
    reg = constants(REGISTERS)
    spi = read(SPI)
    data_bits = bin(reg["kDataMask"]).count("1")
    if reg["kDataMask"] != (1 << data_bits) - 1:
        raise SystemExit("figure source drift: kDataMask is not the low bits of the word")
    rw, warn, err = _bit(reg["kReadBit"]), _bit(reg["kReplyWarningBit"]), _bit(reg["kReplyErrorBit"])
    word_bits = 16
    # MOSI word, top bit first: everything that is neither R/W nor address is sent as 0.
    mosi_word = []
    for b in range(word_bits - 1, -1, -1):
        kind = "rw" if b == rw else "addr" if b < data_bits else "zero"
        mosi_word.append(kind)
    miso_word = ["warn" if b == warn else "err" if b == err else "addr" if b < data_bits else "zero"
                 for b in range(word_bits - 1, -1, -1)]

    exchange = need(r"Result<uint16_t> transfer\(.*?\n  \}\n", spi, "FrameBus::transfer (one frame)").group(0)
    need(r"Bits32\) \{\s*tx\[0\] = pad;", exchange, "pad byte first on MOSI")
    need(r"rx\[0\]\) << 8U\) \|\s*static_cast<uint32_t>\(rx\[1\]\)", exchange, "reply word in MISO bytes 0 and 1")
    need(r"rx\[2\] != Crc8\(reply\)", exchange, "reply CRC in MISO byte 2")
    sizes = dict(re.findall(r"case FrameFormat::(Bits\d+):\s*return (\d+)U?;", spi))
    regs = read(REGISTERS)
    crc = need(r"kCrcInit = (0x[0-9A-F]+);", regs, "CRC initial value")
    crc_poly = need(r"kCrcPolynomial = (0x[0-9A-F]+);", regs, "CRC polynomial").group(1)
    crc_xor = need(r"kCrcFinalXor = (0x[0-9A-F]+);", regs, "CRC final XOR").group(1)
    need(r"constexpr uint8_t Crc8\(uint16_t word\).*?kCrcInit.*?kCrcFinalXor", spi, "Crc8")
    write16 = need(r"FrameFormat::Bits16\)? \? FrameFormat::(Bits\d+)", read(UMBRELLA), "writeFormat()").group(1)

    frames = []
    for name in ("Bits16", "Bits24", "Bits32"):
        nbytes = int(sizes[name])
        has_crc, has_pad = nbytes >= 3, nbytes == 4
        mosi = (["pad"] * 8 if has_pad else []) + mosi_word + (["crc"] * 8 if has_crc else [])
        miso = miso_word + (["crc"] * 8 if has_crc else []) + (["pad"] * 8 if has_pad else [])
        if len(mosi) != nbytes * 8 or len(miso) != nbytes * 8:
            raise SystemExit(f"figure source drift: {name} is not {nbytes} bytes")
        frames.append({"format": name, "bits": nbytes * 8, "mosi": _runs(mosi), "miso": _runs(miso)})
    return {
        "frames": frames,
        "read_bit": rw, "warning_bit": warn, "error_bit": err, "data_bits": data_bits,
        "crc": {"init": crc.group(1), "poly": crc_poly, "xorout": crc_xor},
        "write_format_for_bits16": write16,
        "sources": [f"{REGISTERS}:frame bits", f"{SPI}:detail::FrameBus::Exchange", f"{SPI}:Crc8",
                    f"{UMBRELLA}:writeFormat"],
    }


def _runs(kinds):
    """[{field, msb, lsb}] for a frame given top bit first."""
    out, n = [], len(kinds)
    for i, k in enumerate(kinds):
        bit = n - 1 - i
        if out and out[-1]["field"] == k:
            out[-1]["lsb"] = bit
        else:
            out.append({"field": k, "msb": bit, "lsb": bit})
    return out


def build(d):
    f = Fig(
        "spi-frames",
        "SPI frames, bit by bit",
        alt=("Bit layouts of the 16-, 24- and 32-bit SPI frames: on MOSI the R/W bit and a 14-bit address, on MISO "
             "a warning bit, an error bit and 14 data bits; 24-bit frames add a CRC-8 byte, and 32-bit frames add "
             "a pad byte, first on MOSI and last on MISO."),
        width=1200,
        kicker="Hardware",
        subtitle="Bits in the order they are clocked, most significant first. Numbers are bit positions in the frame.",
        legend=[("green", "R/W: 1 read, 0 write"), ("amber", "W: warning"), ("rose", "E: error"),
                ("blue", "address or data, 14 bits"), ("teal", "CRC-8 of the word"), ("violet", "pad byte"),
                ("slate", "always 0")],
    )
    k = f.k
    lab_w = 96
    bw = 32  # px per bit
    rh = 34
    num = k["small"]
    label = {"zero": "0", "rw": "R/W", "warn": "W", "err": "E", "addr": None, "crc": "CRC-8", "pad": None}
    notes = {
        "Bits16": f"No CRC. Reads only: the driver sends writes as {d['write_format_for_bits16'][4:]}-bit frames.",
        "Bits24": "The driver's default. The CRC protects the word in both directions.",
        "Bits32": "For daisy chains. The pad byte is Config::pad_byte on MOSI; on MISO the driver ignores it.",
    }
    y = 0.0
    for fr in d["frames"]:
        n = fr["bits"]
        f.text(0, y + k["card_title"], f"{n}-bit frame", size=k["card_title"], weight=700)
        f.text(150, y + k["card_title"], notes[fr["format"]], size=k["card_body"],
               color="muted")
        y += k["card_title"] + 14
        for side in ("mosi", "miso"):
            y += num + 6
            f.text(0, y + rh / 2 + k["card_body"] * 0.36, "MOSI" if side == "mosi" else "MISO", size=k["card_body"], weight=700,
                   color="ink2")
            for run in fr[side]:
                kind, msb, lsb = run["field"], run["msb"], run["lsb"]
                x = lab_w + (n - 1 - msb) * bw
                w = (msb - lsb + 1) * bw
                tone = TONES[kind]
                f.rect(x, y, w, rh, fill=f"{tone}.tint", stroke=f"{tone}.strong", rx=4, sw=1.2)
                text = label[kind]
                if text is None:
                    if kind == "pad":
                        text = "pad byte" if side == "mosi" else "pad byte (ignored)"
                    else:
                        text = "address (in a write data frame: value)" if side == "mosi" else "data"
                f.text(x + w / 2, y + rh / 2 + k["card_body"] * 0.36, text, size=k["card_body"], weight=600,
                       color=f"{tone}.text", bg=f"{tone}.tint", anchor="middle")
                f.text(x + bw / 2, y - 5, str(msb), size=num, color="muted", anchor="middle")
                if lsb != msb:
                    f.text(x + w - bw / 2, y - 5, str(lsb), size=num, color="muted", anchor="middle")
            y += rh + 8
        y += 18
    f.note(0, y, f.body_w,
           f"CRC-8 over the 16-bit word: polynomial {d['crc']['poly']}, initial value {d['crc']['init']}, final XOR "
           f"{d['crc']['xorout']}. A MOSI frame with a wrong CRC is not executed and sets ERRFL; a MISO frame with a "
           f"wrong CRC fails the call with Error::Crc.", tone="teal", title="CRC")
    return f
