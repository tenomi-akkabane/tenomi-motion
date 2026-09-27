#!/usr/bin/env python3
"""Merge Intel HEX files and optional address-placed binaries into one HEX.

Used by tools/install_release.sh and install_release.ps1 to assemble:
  FSBL @ 0x70000000 + signed Appli @ 0x70100000 + palm + HL + MotionMLP
  MotionMLP is a separate NOR slot (0x70A00000) so retrains can replace it alone.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


class HexError(RuntimeError):
    pass


def _checksum(payload: bytes) -> int:
    return ((~sum(payload) + 1) & 0xFF)


def parse_hex(path: Path) -> dict[int, int]:
    """Return sparse map of address -> byte. Last file wins on overlap within one file."""
    data: dict[int, int] = {}
    ela = 0
    with path.open("r", encoding="ascii", errors="replace") as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.strip()
            if not line:
                continue
            if not line.startswith(":"):
                raise HexError(f"{path}:{lineno}: not an Intel HEX line")
            rec = bytes.fromhex(line[1:])
            if len(rec) < 5:
                raise HexError(f"{path}:{lineno}: truncated record")
            count, addr_hi, addr_lo, rtype = rec[0], rec[1], rec[2], rec[3]
            addr = (addr_hi << 8) | addr_lo
            if len(rec) != 5 + count:
                raise HexError(f"{path}:{lineno}: length mismatch")
            if _checksum(rec[:-1]) != rec[-1]:
                raise HexError(f"{path}:{lineno}: checksum mismatch")
            payload = rec[4 : 4 + count]
            if rtype == 0:
                base = ela + addr
                for i, b in enumerate(payload):
                    data[base + i] = b
            elif rtype == 1:
                break
            elif rtype == 2:
                ela = int.from_bytes(payload, "big") << 4
            elif rtype == 4:
                ela = int.from_bytes(payload, "big") << 16
            elif rtype in (3, 5):
                continue
            else:
                raise HexError(f"{path}:{lineno}: unsupported record type {rtype:#x}")
    return data


def parse_bin(path: Path, base: int) -> dict[int, int]:
    blob = path.read_bytes()
    return {base + i: b for i, b in enumerate(blob)}


def merge_maps(parts: list[tuple[str, dict[int, int]]]) -> dict[int, int]:
    out: dict[int, int] = {}
    owners: dict[int, str] = {}
    for name, mp in parts:
        for addr, val in mp.items():
            prev = owners.get(addr)
            if prev is not None and prev != name:
                raise HexError(
                    f"overlap at 0x{addr:08X} between {prev} and {name}"
                )
            out[addr] = val
            owners[addr] = name
    return out


def ranges(addrs: list[int]) -> list[tuple[int, int]]:
    if not addrs:
        return []
    addrs = sorted(addrs)
    segs = []
    start = prev = addrs[0]
    for a in addrs[1:]:
        if a == prev + 1:
            prev = a
            continue
        segs.append((start, prev + 1))
        start = prev = a
    segs.append((start, prev + 1))
    return segs


def write_hex(path: Path, data: dict[int, int], rec_len: int = 16) -> None:
    addrs = sorted(data)
    ela = None
    lines: list[str] = []
    i = 0
    n = len(addrs)
    while i < n:
        addr = addrs[i]
        high = addr >> 16
        if ela != high:
            ela = high
            payload = bytes((0x02, 0x00, 0x00, 0x04, (high >> 8) & 0xFF, high & 0xFF))
            rec = payload + bytes((_checksum(payload),))
            lines.append(":" + rec.hex().upper())
        chunk = bytearray()
        base = addr
        while (
            i < n
            and addrs[i] == addr
            and (addrs[i] >> 16) == ela
            and len(chunk) < rec_len
        ):
            chunk.append(data[addrs[i]])
            i += 1
            addr += 1
            if (addr & 0xFFFF) == 0:
                break
        hdr = bytes((len(chunk), (base >> 8) & 0xFF, base & 0xFF, 0x00)) + bytes(chunk)
        rec = hdr + bytes((_checksum(hdr),))
        lines.append(":" + rec.hex().upper())
    lines.append(":00000001FF")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("-o", "--output", required=True, type=Path)
    p.add_argument(
        "--hex",
        action="append",
        default=[],
        metavar="FILE",
        help="Intel HEX input (repeatable)",
    )
    p.add_argument(
        "--bin",
        action="append",
        default=[],
        metavar="ADDR:FILE",
        help="binary placed at ADDR (e.g. 0x70100000:app_sign.bin)",
    )
    args = p.parse_args(argv)

    parts: list[tuple[str, dict[int, int]]] = []
    for hx in args.hex:
        path = Path(hx)
        if not path.is_file():
            raise HexError(f"missing hex: {path}")
        parts.append((str(path), parse_hex(path)))
    for spec in args.bin:
        if ":" not in spec:
            raise HexError(f"--bin expects ADDR:FILE, got {spec!r}")
        addr_s, file_s = spec.split(":", 1)
        path = Path(file_s)
        if not path.is_file():
            raise HexError(f"missing bin: {path}")
        addr = int(addr_s, 0)
        parts.append((f"{path}@{addr:#x}", parse_bin(path, addr)))

    if not parts:
        raise HexError("no inputs")

    merged = merge_maps(parts)
    segs = ranges(list(merged))
    write_hex(args.output, merged)
    total = len(merged)
    print(f"wrote {args.output}  ({total / (1024 * 1024):.3f} MiB payload, {len(segs)} segments)")
    for start, end in segs:
        print(f"  0x{start:08X}-0x{end:08X}  ({(end - start) / 1024:.1f} KiB)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except HexError as e:
        print(f"error: {e}", file=sys.stderr)
        raise SystemExit(1)
