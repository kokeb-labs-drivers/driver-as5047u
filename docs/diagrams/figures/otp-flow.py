"""ProgramOtp() step by step: datasheet steps 6 to 19 as the driver runs them, and where each can fail.

Reads: The call first refuses to run when the driver is not initialised or the confirm value is wrong, and sends
nothing then. It switches every frame to a CRC-protected format and runs three phases, top to bottom and left to
right: prepare (steps 6 to 11: read the settings, compute and write the ECC key, read everything back and compare),
burn (steps 12 to 14: enable programming, start the burn, poll PROG every millisecond until the burn is done or the
timeout passes) and verify (steps 15 to 19: clear the volatile copy, reload it from the OTP at the guard-band level
and compare it with what was burned). A pill on a step names the error that step adds; any step also fails on a
transfer or reply error. The call stops at the first failure and OtpReport::failed_step names the step.
OtpReport::burn_started becomes true just before the step 13 frame is sent, so from there on the fuses may be blown
even if the call fails. The steps, registers, timeout and errors are read from otpPrepare(), otpBurn() and
otpVerify() in src/as5047u.ipp.
"""

import re

from _source import IMPL, REGISTERS, TYPES, body, constants, meanings, names, need, read

from figkit import Fig

# The value written at a step when it is a local variable, not a register constant.
LOCALS = {"ecc": "ECC_en with the key"}


def _steps(text: str) -> list[tuple[int, str]]:
    parts = re.split(r"report\.failed_step = (\d+);", text)
    return [(int(parts[i]), parts[i + 1]) for i in range(1, len(parts), 2) if parts[i] != "0"]


def _describe(n: int, block: str, nm: dict[str, str], why: dict[str, str], seen_read: bool, timeout: int,
              delay_ms: float) -> dict:
    errors = []
    m = re.search(r"writeRaw\(reg::(k\w+), (reg::k\w+|\w+), (true|false)\)", block)
    loop_write = re.search(r"writeRaw\(static_cast<uint16_t>\(reg::kFirstNonVolatile \+ i\), (\w+?)(?:_u16)?, (true|false)\)",
                           block)
    if "kOtpBurnTimeoutMs" in block:
        done = need(r"prog\.value == reg::(k\w+)", block, "burn done test").group(1)
        title = f"Poll PROG until it reads {nm.get(done, done)}"
        detail = f"Every {delay_ms:g} ms, at most {timeout} ms"
        errors.append("Timeout")
    elif loop_write:
        title = f"Write {loop_write.group(1)} to DISABLE to ECC"
        detail = "Each write read back" if loop_write.group(2) == "true" else ""
        if loop_write.group(2) == "true":
            errors.append("WriteMismatch")
    elif m:
        value = m.group(2)
        value = nm.get(value[5:], value) if value.startswith("reg::") else LOCALS[value]
        title = f"{nm[m.group(1)]} = {value}"
        what = why.get(m.group(2)[5:], "") if m.group(2).startswith("reg::") else ""
        what = what[0].upper() + what[1:] + "; " if what else ""
        if m.group(3) == "true":
            detail = what + "read back"
            errors.append("WriteMismatch")
        else:
            detail = what + "sent once, not read back"
    elif "readNonVolatile()" in block:
        title = "Read DISABLE to ECC" + (" again" if seen_read else "")
        detail = "What will be burned" if not seen_read else ""
    elif c := re.search(r"readChecked\(reg::(k\w+)\)", block):
        title = f"Read {nm[c.group(1)]}"
        detail = "The ECC key"
    elif c := re.search(r"if \((\w+)\.value != (\w+(?:\.\w+)?)\)\s*\{\s*return [^;]*Error::(\w+)", block):
        title = "Compare with what was read before" if c.group(3) == "WriteMismatch" else "Compare with what was burned"
        detail = "Guard-band test" if c.group(3) == "OtpVerify" else "Must be equal"
        errors.append(c.group(3))
    else:
        raise SystemExit(f"figure source drift: cannot describe OTP step {n}")
    detail = detail[:1].upper() + detail[1:]
    return {"step": n, "title": title, "detail": detail, "errors": errors,
            "burn_started": "report.burn_started = true" in block}


def data():
    impl = read(IMPL)
    nm = names(REGISTERS)
    why = meanings(REGISTERS)
    types = constants(TYPES)
    top = body(impl, "AS5047U<Port>::ProgramOtp(OtpBurnConfirm confirm, OtpReport& report)")
    need(r"!initialized_\)\s*\{\s*return Result<void>::Failure\(Error::NotInitialized\)", top, "NotInitialized")
    need(r"confirm\.code != kOtpConfirm\.code\)\s*\{\s*return Result<void>::Failure\(Error::InvalidArgument\)", top, "confirm")
    need(r"config_\.frame = writeFormat\(\);", top, "CRC frames for the procedure")
    phases = []
    seen_read = False
    for fn, label in (("otpPrepare", "Prepare"), ("otpBurn", "Burn"), ("otpVerify", "Verify")):
        text = body(impl, f"AS5047U<Port>::{fn}(OtpReport& report)")
        delay = need(r"DelayUs\([^,]+, (\d+)(?:_u32)?\)", text, "poll delay").group(1) if "kOtpBurnTimeoutMs" in text else "0"
        steps = []
        for n, block in _steps(text):
            steps.append(_describe(n, block, nm, why, seen_read, types["kOtpBurnTimeoutMs"], int(delay) / 1000))
            seen_read = seen_read or "readNonVolatile()" in block
        phases.append({"function": fn, "label": label, "steps": steps})
    numbers = [s["step"] for p in phases for s in p["steps"]]
    if numbers != list(range(6, 20)):
        raise SystemExit(f"figure source drift: OTP steps are {numbers}, expected 6 to 19")
    return {"phases": phases, "timeout_ms": types["kOtpBurnTimeoutMs"],
            "sources": [f"{IMPL}:ProgramOtp", f"{IMPL}:otpPrepare", f"{IMPL}:otpBurn", f"{IMPL}:otpVerify",
                        f"{TYPES}:kOtpBurnTimeoutMs", f"{REGISTERS}:PROG and ECC"]}


def build(d):
    f = Fig(
        "otp-flow",
        "ProgramOtp(), step by step",
        alt=("Flowchart of ProgramOtp: prepare (steps 6 to 11), burn (steps 12 to 14, polled with a "
             f"{d['timeout_ms']} ms timeout) and guard-band verify (steps 15 to 19); burn_started becomes true at "
             "step 13, and each step can stop the call with an error."),
        width=1200,
        kicker="OTP programming",
        subtitle=("Datasheet steps 6 to 19. The call stops at the first failure; OtpReport::failed_step names "
                  "the step."),
        legend=[("teal", "fails here: nothing burned"), ("amber", "fails here: fuses may be blown"), ("rose", "error this step adds"),
                ("~solid", "next step")],
    )
    k = f.k
    W = f.body_w
    gap = 40
    cw = (W - 2 * gap) / 3
    inner = 16
    vgap = 34

    start = f.card(0, 0, W, "ProgramOtp(kOtpConfirm, report)",
                   ["Not initialised: NotInitialized. Any confirm other than kOtpConfirm: InvalidArgument. "
                    "Neither sends a frame.",
                    "Then every frame, reads included, carries a CRC until the call returns."],
                   tone="slate", accent=True)
    tones = {"Prepare": "slate", "Burn": "slate", "Verify": "slate"}
    subs = {"Prepare": "Nothing is burned if a step fails here",
            "Burn": "burn_started is true from step 13",
            "Verify": "Failure after the burn: do not burn again"}
    ry = start.b + 40
    columns = []
    for i, ph in enumerate(d["phases"]):
        columns.append((i * (cw + gap), tones[ph["label"]], ph["steps"]))
    cards = []
    burned = False
    y0 = f.region_inner_top(ry, sub_below=True) + 4
    bottoms = []
    for x, tone, steps in columns:
        y = y0
        col = []
        for s in steps:
            if s["burn_started"]:
                burned = True
                y += 40  # room for the burn_started label on the arrow
            lines = [s["detail"]] if s["detail"] else []
            c = f.card(x + inner, y, cw - 2 * inner, f"{s['step']}. {s['title']}", lines,
                       tone="amber" if burned else "teal",
                       accent=True, tag=s["errors"][0] if s["errors"] else "")
            col.append((c, s))
            y = c.b + vgap
        cards.append(col)
        bottoms.append(y - vgap)
    done = f.card(columns[2][0] + inner, bottoms[2] + vgap, cw - 2 * inner, "Done: failed_step = 0",
                  ["Then you: power cycle, read back, compare (steps 20 to 22)"], tone="green", accent=True)
    bottoms[2] = done.b
    # Regions are drawn last, once the tallest column is known; their layer sits under the cards.
    region_h = max(bottoms) + 18 - ry
    for i, ph in enumerate(d["phases"]):
        x, tone, steps = columns[i]
        f.region(x, ry, cw, region_h, f"{ph['label']}, steps {steps[0]['step']} to {steps[-1]['step']}",
                 tone=tone, sub=subs[ph["label"]], sub_below=True)

    f.connect(start, cards[0][0][0], "bottom", "top", at=0.25, bt=0.85)
    flat = [cs for col in cards for cs in col]
    for (a, sa), (b, sb) in zip(flat, flat[1:]):
        label = "burn_started = true" if sb["burn_started"] else ""
        if a.x == b.x:
            f.connect(a, b, label=label)
        else:
            f.connect(a, b, "right", "left", at=0.5, bt=0.5)
    f.connect(cards[2][-1][0], done)
    # Error pills: rose, drawn over the step's tag, so the colour says "error".
    for col in cards:
        for c, s in col:
            if s["errors"]:
                pw = f.pill_width(s["errors"][0])
                f.pill(c.r - k["pad"] - pw + 4, c.y + k["pad"] - 4, s["errors"][0], tone="rose")
    f.note(0, region_h + ry + 24, W,
           "Every step also stops on a transfer or reply error: Bus, Crc, Framing, Command or DeviceFault.",
           tone="rose", title="Any step")
    return f
