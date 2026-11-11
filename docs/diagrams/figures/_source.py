"""Shared helpers for the AS5047U figures: read constants and function bodies from inc/ and src/.

Every helper raises when the code no longer looks the way a figure expects, so a figure fails to build instead of
drawing something the code does not do.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
REGISTERS = "inc/as5047u_registers.hpp"
SPI = "inc/as5047u_spi_interface.hpp"
TYPES = "inc/as5047u_types.hpp"
UMBRELLA = "inc/as5047u.hpp"
IMPL = "src/as5047u.ipp"


def read(rel: str) -> str:
    return (ROOT / rel).read_text()


def need(pattern: str, text: str, what: str, flags: int = re.S) -> re.Match:
    m = re.search(pattern, text, flags)
    if not m:
        raise SystemExit(f"figure source drift: {what} not found (pattern {pattern!r})")
    return m


def _value(expr: str, known: dict[str, int]) -> int:
    expr = expr.strip()
    expr = re.sub(r"(0x[0-9A-Fa-f']+|\d+)U", r"\1", expr).replace("'", "")
    expr = re.sub(r"uint32_t\{([^{}]*)\}", r"(\1)", expr).replace("\n", " ")  # widened operands, wrapped lines
    for name in sorted(known, key=len, reverse=True):
        expr = re.sub(rf"\b{name}\b", str(known[name]), expr)
    if not re.fullmatch(r"[0-9xA-Fa-f<>|&~() +\n-]+", expr):
        raise SystemExit(f"figure source drift: cannot evaluate constant {expr!r}")
    return int(eval(expr))  # noqa: S307 - digits and operators only, checked above


def constants(rel: str) -> dict[str, int]:
    """``inline constexpr`` integer constants of a header, by name (``kReadBit`` -> 16384)."""
    out: dict[str, int] = {}
    for name, expr in re.findall(r"inline constexpr u?int\d+_t (k\w+) =\s*([^;]+);", read(rel)):
        out[name] = _value(expr, out)
    return out


def names(rel: str) -> dict[str, str]:
    """The datasheet name in each constant's comment (``kProgBurn`` -> ``PROGOTP``); an alias
    (``kProgDone = kProgEnable``) gets the name of the constant it equals."""
    text = read(rel)
    out = dict(re.findall(r"inline constexpr \w+ (k\w+) = [^;]+;\s*///< ([A-Za-z_0-9]+)", text))
    for alias, target in re.findall(r"inline constexpr \w+ (k\w+) = (k\w+);", text):
        if target in out:
            out.setdefault(alias, out[target])
    return out


def meanings(rel: str) -> dict[str, str]:
    """What each constant's comment says after its name (``kProgBurn`` -> ``start burning the OTP``)."""
    return {k: v[0].lower() + v[1:].rstrip(".") for k, v in
            re.findall(r"inline constexpr \w+ (k\w+) = [^;]+;\s*///< [A-Za-z_0-9]+: ([^\n]+)", read(rel))}


def body(text: str, signature: str) -> str:
    """The body of the function whose definition contains ``signature``, braces matched."""
    start = need(re.escape(signature) + r"[^{;]*\{", text, signature).end()
    depth, i = 1, start
    while depth:
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        i += 1
    return text[start : i - 1]
