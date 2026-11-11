"""Why a read is two frames, and what a write sends: the frames of one register access, in time order.

Reads: Time runs left to right, one column per SPI frame; the top row is what the driver sends on MOSI and the
bottom row what the chip returns on MISO in the same frame. The chip answers a frame during the next one (dashed
arrows), so a read sends the read command and then a NOP read, whose reply carries the register. The reply in the
first frame belongs to the frame before it, so the driver fails the call there only when the transfer itself fails.
When the reply that answers the call has its error bit set, the driver reads ERRFL (two more frames, dashed) to
find out why and fails the call. A write sends the command frame (R/W at 0, the address) and the data frame, always
with a CRC; the reply in the data frame reports on the command frame. The read-back after it (two more frames,
dashed) is chosen per call with Verify::ReadBack; EnsureInitialized() and ProgramOtp() always read back. The
frame words come from readRaw(), writeRaw() and resolveFlags() in src/as5047u.ipp. A note at the bottom gives the rule
of FrameBus::Exchange(): after a failed transfer the next frame is a NOP read, so a lost data frame cannot turn the
next command into data.
"""

import re

from _source import IMPL, REGISTERS, SPI, UMBRELLA, body, constants, names, need, read

from figkit import Fig


def data():
    reg = constants(REGISTERS)
    nm = names(REGISTERS)
    impl = read(IMPL)
    rd = body(impl, "AS5047U<Port>::readRaw(uint16_t address)")
    wr = body(impl, "AS5047U<Port>::writeRaw(uint16_t address, uint16_t value, bool verify)")
    chk = body(impl, "AS5047U<Port>::resolveFlags(bool measurement)")
    errfl = body(impl, "AS5047U<Port>::readErrfl()")
    need(r"reg::kReadBit\) \|\s*\(static_cast<uint32_t>\(address\) & reg::kDataMask\)", rd, "read command = read bit | address")
    need(r"!first && \(?first\.error == Error::Bus\)?\)\s*\{\s*return first;", rd, "first reply: only Bus fails")
    nop = need(r"kNopRead = [^;]*reg::kReadBit\) \|\s*static_cast<uint32_t>\(reg::(kNop)\)", rd, "second frame: NOP read").group(1)
    need(r"Exchange\([^;]*kNopRead\)", rd, "second frame sent")
    need(r"Exchange\(\s*format, [^,]+,\s*static_cast<uint16_t>\(static_cast<uint32_t>\(address\) & reg::kDataMask\)\)", wr, "write command frame = address, R/W 0")
    need(r"!command && \(?command\.error == Error::Bus\)?\)\s*\{", wr, "write command reply: only Bus fails")
    need(r"masked = static_cast<uint16_t>\(static_cast<uint32_t>\(value\) & reg::kDataMask\)", wr, "write data value")
    need(r"Exchange\(format, [^,]+, masked\)", wr, "write data frame = value")
    need(r"value\) & reg::kReplyErrorBit\) != 0U\) \{\s*if \(auto c = resolveFlags\(false\)", wr, "data-frame reply checked")
    need(r"if \(!verify\)\s*\{\s*return Result<void>::Success\(\);", wr, "read-back only when asked")
    need(r"readChecked\(address\)", wr, "read-back")
    need(r"Error::WriteMismatch", wr, "read-back mismatch")
    need(r"errfl\.value == 0U", chk, "flag bit with ERRFL empty")
    need(r"readErrfl\(\)", chk, "ERRFL read")
    need(r"readRaw\(reg::kErrfl\)", errfl, "ERRFL frames")
    classify = body(read(UMBRELLA), "constexpr Error ClassifyErrorFlags(uint16_t errfl)")
    errors = [e for e in re.findall(r"return Error::(\w+);", classify) if e != "Ok"]
    spi = read(SPI)
    resync = need(r"kResyncWord = (0x[0-9A-Fa-f]+)U?;", spi, "resync word after a failed transfer").group(1)
    need(r"if \(resync_\) \{\s*\(void\)transfer\(format, pad, kResyncWord\)", spi, "resync before the next frame")
    example = "kAngleCom"
    return {
        "resync": f"0x{int(resync, 16):04X}",
        "read_bit": reg["kReadBit"],
        "nop_read": f"0x{reg['kReadBit'] | reg[nop]:04X}",
        "example": {"name": nm[example], "address": f"0x{reg[example]:04X}",
                    "command": f"0x{reg['kReadBit'] | reg[example]:04X}"},
        "errfl": {"name": nm["kErrfl"], "command": f"0x{reg['kReadBit'] | reg['kErrfl']:04X}"},
        "errfl_errors": errors,
        "sources": [f"{IMPL}:readRaw", f"{IMPL}:writeRaw", f"{IMPL}:resolveFlags",
                    f"{UMBRELLA}:detail::ClassifyErrorFlags", f"{REGISTERS}:frame bits", f"{SPI}:FrameBus::Exchange"],
    }


def build(d):
    ex, nop, errfl = d["example"], d["nop_read"], d["errfl"]
    f = Fig(
        "read-pipeline",
        "Every access is two frames",
        alt=(f"Timing of SPI frames: a read sends the command and then a NOP read ({nop}), because the chip answers "
             f"each frame during the next one; a write sends a command frame and a data frame, then reads the "
             f"register back."),
        width=1200,
        kicker="Architecture",
        subtitle="One column per SPI frame. The chip answers each frame during the next one.",
        legend=[("blue", "driver sends (MOSI)"), ("green", "the reply the call uses"),
                ("slate", "reply that belongs to the frame before"), ("~dashed", "answered in the next frame")],
    )
    k = f.k
    lane_x, col_x, col_w, gap = 16, 92, 230, 34
    W = f.body_w
    H = 128

    def col(i):
        return col_x + i * (col_w + gap)

    def panel(y, label, sub, frames):
        """frames: [(mosi_title, mosi_lines, miso_title, miso_lines, miso_tone, dashed)]."""
        top = f.region_inner_top(y)
        mosi_y, miso_y = top + 8, top + 8 + H + 44
        f.region(0, y, W, miso_y + H + 18 - y, label, tone="slate", sub=sub)
        for ly, s in ((mosi_y, "MOSI"), (miso_y, "MISO")):
            f.text(lane_x, ly + H / 2 + k["card_body"] * 0.36, s, size=k["card_body"], weight=700, color="slate.text",
                   bg="slate.tint")
        tops, bots = [], []
        for i, (mt, ml, rt, rl, rtone, dashed) in enumerate(frames):
            tops.append(f.card(col(i), mosi_y, col_w, mt, ml, tone="blue", h=H, accent=not dashed, dashed=dashed))
            bots.append(f.card(col(i), miso_y, col_w, rt, rl, tone=rtone, h=H, accent=rtone == "green",
                               dashed=dashed))
        for i in range(len(frames) - 1):
            f.connect(tops[i], bots[i + 1], "right", "left", at=0.75, bt=0.3, dashed=True)
        return miso_y + H + 18

    hy = k["card_title"]
    heads = [f.text(col(i) + col_w / 2, hy, f"Frame {i + 1}", size=k["card_title"], weight=700, color="ink2",
                    anchor="middle") for i in range(4)]
    for a, b in zip(heads, heads[1:]):
        f.edge([(a.r + 16, hy - 5), (b.x - 16, hy - 5)])
    y = panel(hy + 18, "Read", f"ReadRegister({ex['name']}), and every other read", [
        (f"Read {ex['name']}", [f"{ex['command']}: R/W 1, address {ex['address']}"],
         "Reply to the frame before", ["Not used; only a failed transfer fails the call"], "slate", False),
        ("NOP read", [f"{nop}: R/W 1, address 0", "Asks for nothing new"],
         f"{ex['name']} data", ["The value of the call. Checked: CRC, then the error bit"], "green", False),
        (f"Only if the error bit is set: read {errfl['name']}", [f"{errfl['command']}"],
         "Reply to the NOP read", ["Not used"], "slate", True),
        ("NOP read", [nop],
         f"{errfl['name']}, cleared by the read", ["Fails the call with " + ", ".join(d["errfl_errors"][:-1]) + " or " + d["errfl_errors"][-1]], "green",
         True),
    ])
    y += 28
    y = panel(y, "Write", "WriteRegister(address, value) and every settings write", [
        ("Write command", ["R/W 0, the address; always with a CRC"],
         "Reply to the frame before", ["Not used; only a failed transfer fails the call"], "slate", False),
        ("Data", ["The 14-bit value, R/W 0; always with a CRC"],
         "Reports on the command frame", [f"Error bit set: read {errfl['name']} and fail"], "green", False),
        ("Read command, same address", ["Only with Verify::ReadBack,", "at start-up and in ProgramOtp()"],
         "Reply to the data frame", ["Not used; only a failed transfer fails the call"], "slate", True),
        ("NOP read", [nop],
         "The register", ["Must equal the value written, else WriteMismatch"], "green", True),
    ])
    f.note(0, y + 20, f.body_w,
           f"After any failed transfer the chip may still wait for a write's data frame, so the next frame the "
           f"driver sends is a NOP read ({d['resync']}): as a command it reads nothing, as data it writes 0. "
           f"Without it a lost PROG data frame let the next read burn the OTP.",
           tone="amber", title="After a failed transfer")
    return f
