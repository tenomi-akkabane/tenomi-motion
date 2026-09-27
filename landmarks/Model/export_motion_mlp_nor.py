#!/usr/bin/env python3
"""PyTorch MotionMLP checkpoint → NOR hex (same update path as palm/HL).

Primary:
  python export_motion_mlp_nor.py --pt path/to/motion_mlp.pt

Checkpoint format matches 15_gesture-motion-dk/scripts/train_motion_mlp.py:
  model_state_dict, input_dim, hidden_sizes, num_classes, window_frames, class_names

Writes motion_mlp_data.bin and motion_mlp_data.hex next to this script.
Flash hex @ 0x70A00000 only (Appli / palm / HL stay). From tenomi:

  ./tools/install_release.sh --motion-only
  ./tools/install_release.sh --motion-only --motion-hex path/to/motion_mlp_data.hex

Fallbacks (not required for retraining):
  --c  legacy C arrays
  --bin existing NOR blob (re-hex only)
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

MAGIC = 0x504C4D4D
VERSION = 1
WINDOW_FRAMES = 8
LANDMARKS = 21
FEAT_DIM = 42
INPUT_DIM = 336
HIDDEN0 = 128
HIDDEN1 = 64
NUM_CLASSES = 5
MAX_CLASSES = 8
NAME_LEN = 32
HEADER_SIZE = 320
NOR_ADDR = 0x70A00000
DEFAULT_CLASS_NAMES = ["come_here", "go_away", "right", "left", "none"]

W0_N = HIDDEN0 * INPUT_DIM
B0_N = HIDDEN0
W1_N = HIDDEN1 * HIDDEN0
B1_N = HIDDEN1
W2_N = NUM_CLASSES * HIDDEN1
B2_N = NUM_CLASSES
PAYLOAD_N = W0_N + B0_N + W1_N + B1_N + W2_N + B2_N
PAYLOAD_BYTES = PAYLOAD_N * 4


def parse_c_floats(text: str, name: str, expect: int) -> list[float]:
    m = re.search(rf"const float {re.escape(name)}\[\d+\] = \{{(.*?)\}};", text, re.S)
    if not m:
        raise SystemExit(f"array {name} not found")
    vals = [float(x) for x in re.findall(r"[-+]?(?:\d+\.\d*|\d*\.\d+|\d+)(?:[eE][-+]?\d+)?", m.group(1))]
    if len(vals) != expect:
        raise SystemExit(f"{name}: expected {expect} floats, got {len(vals)}")
    return vals


def parse_c_names(text: str) -> list[str]:
    m = re.search(r"motion_mlp_class_names\[.*?\] = \{([^}]+)\};", text, re.S)
    if not m:
        raise SystemExit("class names not found")
    names = re.findall(r'"([^"]+)"', m.group(1))
    if len(names) != NUM_CLASSES:
        raise SystemExit(f"expected {NUM_CLASSES} class names, got {len(names)}")
    return names


def load_from_c(path: Path) -> tuple[list[float], list[str]]:
    text = path.read_text(encoding="utf-8")
    floats: list[float] = []
    for name, n in (
        ("motion_mlp_w0", W0_N),
        ("motion_mlp_b0", B0_N),
        ("motion_mlp_w1", W1_N),
        ("motion_mlp_b1", B1_N),
        ("motion_mlp_w2", W2_N),
        ("motion_mlp_b2", B2_N),
    ):
        floats.extend(parse_c_floats(text, name, n))
    return floats, parse_c_names(text)


def load_from_bin(path: Path) -> tuple[list[float], list[str]]:
    raw = path.read_bytes()
    if len(raw) < HEADER_SIZE + PAYLOAD_BYTES:
        raise SystemExit(f"{path}: too small ({len(raw)} bytes)")
    magic, version = struct.unpack_from("<II", raw, 0)
    if magic != MAGIC or version != VERSION:
        raise SystemExit(f"{path}: bad magic/version {magic:#x}/{version}")
    names = []
    for i in range(NUM_CLASSES):
        off = 64 + i * NAME_LEN
        names.append(raw[off : off + NAME_LEN].split(b"\x00", 1)[0].decode("ascii"))
    floats = list(struct.unpack_from(f"<{PAYLOAD_N}f", raw, HEADER_SIZE))
    return floats, names


def linear_prefixes(state: dict) -> list[str]:
    prefixes: list[str] = []
    for key in sorted(
        state.keys(),
        key=lambda k: [int(x) if x.isdigit() else x for x in k.replace(".", " ").split()],
    ):
        if key.endswith(".weight") and (key.startswith("net.") or key.count(".") >= 1):
            prefixes.append(key[: -len(".weight")])
    return prefixes


def _as_state_dict(ckpt: object) -> tuple[dict, dict]:
    """Return (linear state, metadata dict)."""
    if not isinstance(ckpt, dict):
        raise SystemExit("checkpoint is not a dict (expected train_motion_mlp.py save)")
    if "model_state_dict" in ckpt:
        return ckpt["model_state_dict"], ckpt
    if any(k.endswith(".weight") for k in ckpt.keys()):
        return ckpt, {}
    raise SystemExit("no model_state_dict or Linear weights in checkpoint")


def load_from_pt(path: Path) -> tuple[list[float], list[str]]:
    try:
        import torch
    except ImportError as exc:
        raise SystemExit(
            "torch is required for --pt. Use the motion_train venv, e.g.\n"
            "  <15_gesture-motion-dk>/.venv/Scripts/python.exe "
            "landmarks/Model/export_motion_mlp_nor.py --pt <model.pt>"
        ) from exc

    try:
        ckpt = torch.load(path, map_location="cpu", weights_only=False)
    except TypeError:
        ckpt = torch.load(path, map_location="cpu")

    state, meta = _as_state_dict(ckpt)
    prefixes = [p for p in linear_prefixes(state) if f"{p}.bias" in state]
    if len(prefixes) != 3:
        raise SystemExit(f"expected 3 Linear layers, found {prefixes}")

    input_dim = int(meta.get("input_dim", state[f"{prefixes[0]}.weight"].shape[1]))
    hidden = list(meta.get("hidden_sizes", [
        int(state[f"{prefixes[0]}.weight"].shape[0]),
        int(state[f"{prefixes[1]}.weight"].shape[0]),
    ]))
    num_classes = int(meta.get("num_classes", state[f"{prefixes[2]}.weight"].shape[0]))
    window = int(meta.get("window_frames", input_dim // FEAT_DIM))
    names = list(meta.get("class_names", DEFAULT_CLASS_NAMES))

    if (
        input_dim != INPUT_DIM
        or hidden != [HIDDEN0, HIDDEN1]
        or num_classes != NUM_CLASSES
        or window != WINDOW_FRAMES
    ):
        raise SystemExit(
            f"ABI mismatch: got {input_dim}->{hidden}->{num_classes} T={window}, "
            f"expected {INPUT_DIM}->{HIDDEN0}->{HIDDEN1}->{NUM_CLASSES} T={WINDOW_FRAMES}"
        )
    if len(names) != NUM_CLASSES:
        raise SystemExit(f"class_names length {len(names)} != {NUM_CLASSES}")

    expect = [
        (HIDDEN0, INPUT_DIM),
        (HIDDEN1, HIDDEN0),
        (NUM_CLASSES, HIDDEN1),
    ]
    floats: list[float] = []
    for prefix, shape in zip(prefixes, expect):
        w = state[f"{prefix}.weight"].detach().cpu().contiguous().float()
        b = state[f"{prefix}.bias"].detach().cpu().contiguous().float()
        if tuple(w.shape) != shape:
            raise SystemExit(f"{prefix}.weight shape {tuple(w.shape)} != {shape}")
        if tuple(b.shape) != (shape[0],):
            raise SystemExit(f"{prefix}.bias shape {tuple(b.shape)} != ({shape[0]},)")
        floats.extend(w.view(-1).tolist())
        floats.extend(b.view(-1).tolist())
    return floats, [str(n) for n in names]


def pack_names(names: list[str]) -> bytes:
    block = bytearray(MAX_CLASSES * NAME_LEN)
    for i, name in enumerate(names):
        b = name.encode("ascii")
        if len(b) >= NAME_LEN:
            raise SystemExit(f"class name too long: {name}")
        block[i * NAME_LEN : i * NAME_LEN + len(b)] = b
    return bytes(block)


def build_blob(floats: list[float], names: list[str]) -> bytes:
    if len(floats) != PAYLOAD_N:
        raise SystemExit(f"payload floats {len(floats)} != {PAYLOAD_N}")
    if len(names) != NUM_CLASSES:
        raise SystemExit(f"names {len(names)} != {NUM_CLASSES}")
    hdr = struct.pack(
        "<II8HI36s",
        MAGIC,
        VERSION,
        WINDOW_FRAMES,
        LANDMARKS,
        FEAT_DIM,
        INPUT_DIM,
        HIDDEN0,
        HIDDEN1,
        NUM_CLASSES,
        NAME_LEN,
        PAYLOAD_BYTES,
        bytes(36),
    )
    hdr += pack_names(names)
    if len(hdr) != HEADER_SIZE:
        raise SystemExit(f"header size {len(hdr)} != {HEADER_SIZE}")
    return hdr + struct.pack(f"<{PAYLOAD_N}f", *floats)


def ihex_checksum(payload: bytes) -> int:
    return (0x100 - (sum(payload) & 0xFF)) & 0xFF


def write_ihex(path: Path, base: int, data: bytes, rec_len: int = 32) -> None:
    lines: list[str] = []
    i = 0
    current_ext = None
    while i < len(data):
        abs_addr = base + i
        ext = (abs_addr >> 16) & 0xFFFF
        if ext != current_ext:
            rec = bytes([0x02, 0x00, 0x00, 0x04, (ext >> 8) & 0xFF, ext & 0xFF])
            lines.append(":%s%02X" % (rec.hex().upper(), ihex_checksum(rec)))
            current_ext = ext
        room = 0x10000 - (abs_addr & 0xFFFF)
        chunk = data[i : i + min(rec_len, room)]
        lo = abs_addr & 0xFFFF
        rec = bytes([len(chunk), (lo >> 8) & 0xFF, lo & 0xFF, 0x00]) + chunk
        lines.append(":%s%02X" % (rec.hex().upper(), ihex_checksum(rec)))
        i += len(chunk)
    lines.append(":00000001FF")
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def main() -> int:
    ap = argparse.ArgumentParser(description="Export MotionMLP NOR blob from PyTorch")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument(
        "--pt",
        type=Path,
        help="PyTorch checkpoint from train_motion_mlp.py (motion_mlp.pt)",
    )
    src.add_argument("--c", type=Path, help="legacy motion_mlp_weights.c")
    src.add_argument("--bin", type=Path, help="existing motion_mlp_data.bin to re-hex")
    ap.add_argument("--addr", type=lambda s: int(s, 0), default=NOR_ADDR)
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=None,
        help="directory for motion_mlp_data.bin/.hex (default: this script's folder)",
    )
    args = ap.parse_args()

    here = args.out_dir if args.out_dir is not None else Path(__file__).resolve().parent
    here.mkdir(parents=True, exist_ok=True)
    if args.pt:
        if not args.pt.exists():
            raise SystemExit(f"model not found: {args.pt}")
        floats, names = load_from_pt(args.pt)
        source = args.pt
    elif args.c:
        floats, names = load_from_c(args.c)
        source = args.c
    else:
        floats, names = load_from_bin(args.bin)
        source = args.bin

    blob = build_blob(floats, names)
    bin_path = here / "motion_mlp_data.bin"
    hex_path = here / "motion_mlp_data.hex"
    bin_path.write_bytes(blob)
    write_ihex(hex_path, args.addr, blob)
    print(f"source: {source}")
    print(f"classes: {names}")
    print(f"blob: {len(blob)} bytes ({HEADER_SIZE} header + {PAYLOAD_BYTES} weights)")
    print(f"wrote {bin_path.name} and {hex_path.name} @ {args.addr:#x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
