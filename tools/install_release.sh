#!/usr/bin/env bash
# TENOMI μT-Kernel — Release Flash installer (bash twin of install_release.ps1)
#
# Signs the Release Appli .bin and writes FSBL + Appli + palm + HL + MotionMLP
# in one CubeProgrammer step. MotionMLP stays a separate NOR slot so a later
# retrain (28_motion_train_dk) can replace only that hex — no Appli rebuild.
#
#   ./tools/install_release.sh
#   ./tools/install_release.sh --no-flash
#   ./tools/install_release.sh --motion-only
#   ./tools/install_release.sh --motion-only --motion-hex path/to/motion_mlp_data.hex
#
# Programming needs Development mode: SW1=H, SW2=L.
# After a successful full write: SW1=L, SW2=L (Boot from flash) and power-cycle.
# After --motion-only, power-cycle is enough (boot switches stay as they were).
#
# NOR map:
#   0x70000000  FSBL (landmarks/FSBL/ai_fsbl.hex)
#   0x70100000  signed Release Appli .bin
#   0x70380000  palm_detector_data.hex          (fixed)
#   0x70580000  hand_landmark_data.hex          (fixed)
#   0x70A00000  motion_mlp_data.hex             (replaceable)

set -euo pipefail

usage() {
  cat <<'EOF'
Usage: install_release.sh [options]

  --no-flash              Sign and merge only (do not program)
  --motion-only           Write MotionMLP hex @ 0x70A00000 only (no Appli sign)
  --skip-motion           Omit MotionMLP from the combined HEX
  --erase                 Erase NOR before programming (not with --motion-only)
  --bin PATH              Release Appli .bin
  --motion-hex PATH       motion_mlp_data.hex (default: landmarks/Model/...)
  --out-hex PATH          Combined HEX (default: dist/tenomi_utk_release.hex)
  -h, --help              Show this help
EOF
}

die() { echo "error: $*" >&2; exit 1; }

NO_FLASH=0
MOTION_ONLY=0
SKIP_MOTION=0
ERASE=0
BIN_PATH=""
MOTION_HEX=""
OUT_HEX=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-flash|-NoFlash) NO_FLASH=1; shift ;;
    --motion-only|-MotionOnly) MOTION_ONLY=1; shift ;;
    --skip-motion|-SkipMotion) SKIP_MOTION=1; shift ;;
    --erase|-Erase) ERASE=1; shift ;;
    --bin|--BinPath)
      [[ $# -ge 2 ]] || die "$1 needs a path"
      BIN_PATH=$2
      shift 2
      ;;
    --motion-hex|-MotionHex)
      [[ $# -ge 2 ]] || die "$1 needs a path"
      MOTION_HEX=$2
      shift 2
      ;;
    --out-hex|-OutHex)
      [[ $# -ge 2 ]] || die "$1 needs a path"
      OUT_HEX=$2
      shift 2
      ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown argument: $1 (see --help)" ;;
  esac
done

if [[ $MOTION_ONLY -eq 1 && $SKIP_MOTION -eq 1 ]]; then
  die "use either --motion-only or --skip-motion, not both"
fi

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
TENOMI_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
[[ -d "$TENOMI_ROOT/landmarks" && -d "$TENOMI_ROOT/mtk3bsp2_stm32n657" ]] || \
  die "expected landmarks/ and mtk3bsp2_stm32n657/ under $TENOMI_ROOT"

# Windows .exe callers often want native paths (Git Bash / MSYS).
to_tool_path() {
  local p=$1
  if command -v cygpath >/dev/null 2>&1; then
    cygpath -w "$p"
  else
    printf '%s' "$p"
  fi
}

file_size() {
  if stat -c%s "$1" >/dev/null 2>&1; then
    stat -c%s "$1"
  else
    stat -f%z "$1"
  fi
}

find_cube_bin() {
  local dirs=()
  local pf pf86
  pf=${PROGRAMFILES:-}
  pf86=$(printenv 'ProgramFiles(x86)' 2>/dev/null || true)
  [[ -n $pf ]] && dirs+=("$pf/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin")
  [[ -n $pf86 ]] && dirs+=("$pf86/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin")
  dirs+=(
    "/c/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin"
    "/c/Program Files (x86)/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin"
    "$HOME/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin"
    "/usr/local/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin"
  )
  local d
  for d in "${dirs[@]}"; do
    if [[ -e "$d/STM32_Programmer_CLI.exe" || -e "$d/STM32_Programmer_CLI" ]]; then
      printf '%s' "$d"
      return 0
    fi
  done
  local found
  found=$(command -v STM32_Programmer_CLI.exe 2>/dev/null || command -v STM32_Programmer_CLI 2>/dev/null || true)
  [[ -n $found ]] || die "STM32_Programmer_CLI not found. Install STM32CubeProgrammer."
  dirname "$found"
}

tool_in() {
  local dir=$1 name=$2
  if [[ -e "$dir/${name}.exe" ]]; then
    printf '%s' "$dir/${name}.exe"
  elif [[ -e "$dir/$name" ]]; then
    printf '%s' "$dir/$name"
  else
    return 1
  fi
}

nor_write() {
  local image=$1 erase_all=$2
  echo "Programming NOR via $EXT_LOADER ..."
  echo "  image: $image"
  local args=(
    -c port=SWD mode=HOTPLUG
    -el "$(to_tool_path "$EXT_LOADER")"
    -hardRst
  )
  if [[ $erase_all -eq 1 ]]; then
    args+=(-e all)
  fi
  args+=(-w "$(to_tool_path "$image")")
  "$PROGRAMMER" "${args[@]}"
}

CUBE_BIN=$(find_cube_bin)
PROGRAMMER=$(tool_in "$CUBE_BIN" STM32_Programmer_CLI) || die "missing STM32_Programmer_CLI in $CUBE_BIN"
EXT_LOADER="$CUBE_BIN/ExternalLoader/MX66UW1G45G_STM32N6570-DK.stldr"
[[ -e $EXT_LOADER ]] || die "missing External Loader $EXT_LOADER"

if [[ -z $MOTION_HEX ]]; then
  MOTION_HEX="$TENOMI_ROOT/landmarks/Model/motion_mlp_data.hex"
fi

if [[ $MOTION_ONLY -eq 1 ]]; then
  [[ -f $MOTION_HEX ]] || die "missing MotionMLP hex: $MOTION_HEX
Export from 28_motion_train_dk (export_motion_mlp_nor.py) first."
  echo "MotionMLP replace-only @ 0x70A00000"
  echo "  $MOTION_HEX"
  echo "  Appli / palm / HL are not rewritten."
  if [[ $NO_FLASH -eq 1 ]]; then
    echo "Skipping flash (--no-flash). Write this hex at 0x70A00000 when ready."
    exit 0
  fi
  [[ $ERASE -eq 0 ]] || die "--erase with --motion-only would wipe Appli/palm/HL. Omit --erase."
  nor_write "$MOTION_HEX" 0
  echo
  echo "MotionMLP updated. Power-cycle the board (Debug: F11 again, or Flash boot: leave SW1=L, SW2=L)."
  exit 0
fi

SIGNING_TOOL=$(tool_in "$CUBE_BIN" STM32_SigningTool_CLI) || die "missing STM32_SigningTool_CLI in $CUBE_BIN"

if [[ -z $BIN_PATH ]]; then
  BIN_PATH="$TENOMI_ROOT/mtk3bsp2_stm32n657/Appli/Release/mtk3bsp2_stm32n657_Appli.bin"
fi
[[ -f $BIN_PATH ]] || die "Release Appli .bin not found: $BIN_PATH
Build mtk3bsp2_stm32n657_Appli / Release in CubeIDE first."

PALM_HEX="$TENOMI_ROOT/landmarks/Model/palm_detector_data.hex"
HL_HEX="$TENOMI_ROOT/landmarks/Model/hand_landmark_data.hex"
FSBL_HEX="$TENOMI_ROOT/landmarks/FSBL/ai_fsbl.hex"
for f in "$PALM_HEX" "$HL_HEX" "$FSBL_HEX"; do
  [[ -f $f ]] || die "missing $f"
done
if [[ $SKIP_MOTION -eq 0 && ! -f $MOTION_HEX ]]; then
  die "missing $MOTION_HEX
Export MotionMLP or pass --skip-motion."
fi

DIST="$TENOMI_ROOT/dist"
mkdir -p "$DIST"
SIGNED_BIN="$DIST/mtk3bsp2_stm32n657_Appli_sign.bin"
if [[ -z $OUT_HEX ]]; then
  OUT_HEX="$DIST/tenomi_utk_release.hex"
fi

APP_SLOT_START=$((0x70100000))
MAX_BIN=$((0x70380000 - 0x70100000 - 0x10000))
BIN_SIZE=$(file_size "$BIN_PATH")
if [[ $BIN_SIZE -gt $MAX_BIN ]]; then
  die "Appli .bin is $BIN_SIZE bytes — too large for NOR slot 0x70100000..0x70380000"
fi

echo "Signing Release Appli -> $SIGNED_BIN"
"$SIGNING_TOOL" \
  -bin "$(to_tool_path "$BIN_PATH")" \
  -nk \
  -t ssbl \
  -hv 2.3 \
  -align \
  -s \
  -o "$(to_tool_path "$SIGNED_BIN")"
[[ -f $SIGNED_BIN ]] || die "signing produced no file: $SIGNED_BIN"

pick_python() {
  local c path
  for c in python python3 python.exe python3.exe; do
    path=$(command -v "$c" 2>/dev/null || true)
    [[ -n $path ]] || continue
    case "$path" in
      *WindowsApps*) continue ;;
    esac
    printf '%s' "$path"
    return 0
  done
  return 1
}

py_arg() {
  local p=$1
  case "$PY" in
    *.exe|*/Scripts/python*) to_tool_path "$p" ;;
    *) printf '%s' "$p" ;;
  esac
}

PY=$(pick_python) || die "python not found (needed to merge Intel HEX)"

MERGE_PY="$SCRIPT_DIR/merge_intel_hex.py"
printf -v BIN_SPEC '0x%X:%s' "$APP_SLOT_START" "$(py_arg "$SIGNED_BIN")"
MERGE_ARGS=(
  "$(py_arg "$MERGE_PY")"
  -o "$(py_arg "$OUT_HEX")"
  --hex "$(py_arg "$FSBL_HEX")"
  --hex "$(py_arg "$PALM_HEX")"
  --hex "$(py_arg "$HL_HEX")"
  --bin "$BIN_SPEC"
)
MOTION_NOTE=""
if [[ $SKIP_MOTION -eq 0 ]]; then
  MERGE_ARGS+=(--hex "$(py_arg "$MOTION_HEX")")
  MOTION_NOTE=" + MotionMLP"
fi

echo "Merging FSBL + signed Appli + palm + HL$MOTION_NOTE"
"$PY" "${MERGE_ARGS[@]}"

echo
echo "Combined HEX: $OUT_HEX"
echo "  SW1=H, SW2=L  (Development mode) to program"
echo "  SW1=L, SW2=L  (Boot from flash) after programming, then power-cycle"
if [[ $SKIP_MOTION -eq 1 ]]; then
  echo "  MotionMLP omitted (--skip-motion). Replace later with --motion-only."
else
  echo "  Retrain replace: ./tools/install_release.sh --motion-only   (no Appli rebuild)"
fi

if [[ $NO_FLASH -eq 1 ]]; then
  echo "Skipping flash (--no-flash)."
  exit 0
fi

nor_write "$OUT_HEX" "$ERASE"
echo
echo "Install done. Set SW1=L, SW2=L and power-cycle to boot from flash."
