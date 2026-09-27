# TENOMI μT-Kernel — Release Flash installer
#
# Signs the Release Appli .bin and writes FSBL + Appli + palm + HL + MotionMLP
# in one CubeProgrammer step. MotionMLP stays a separate NOR slot so a later
# retrain (28_motion_train_dk) can replace only that hex — no Appli rebuild.
#
#   ./tools/install_release.sh
#   ./tools/install_release.sh --no-flash
#   ./tools/install_release.sh --motion-only
#   powershell -ExecutionPolicy Bypass -File .\tools\install_release.ps1
#
# Programming needs Development mode: SW1=H, SW2=L.
# After a successful full write: SW1=L, SW2=L (Boot from flash) and power-cycle.
# After -MotionOnly, power-cycle is enough (boot switches stay as they were).
#
# NOR map:
#   0x70000000  FSBL (landmarks/FSBL/ai_fsbl.hex)
#   0x70100000  signed Release Appli .bin
#   0x70380000  palm_detector_data.hex          (fixed)
#   0x70580000  hand_landmark_data.hex          (fixed)
#   0x70A00000  motion_mlp_data.hex             (replaceable)

param(
    [switch]$NoFlash,
    [switch]$MotionOnly,
    [switch]$SkipMotion,
    [switch]$Erase,
    [string]$BinPath = "",
    [string]$MotionHex = "",
    [string]$OutHex = ""
)

$ErrorActionPreference = "Stop"

if ($MotionOnly -and $SkipMotion) {
    throw "Use either -MotionOnly or -SkipMotion, not both."
}

$TenomiRoot = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path (Join-Path $TenomiRoot "landmarks")) -or
    -not (Test-Path (Join-Path $TenomiRoot "mtk3bsp2_stm32n657"))) {
    throw "Run from tenomi/tools. Expected landmarks/ and mtk3bsp2_stm32n657/ under $TenomiRoot"
}

function Find-CubeProgBin {
    $candidates = @(
        "${env:ProgramFiles}\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin",
        "${env:ProgramFiles(x86)}\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin"
    )
    foreach ($dir in $candidates) {
        if (Test-Path (Join-Path $dir "STM32_Programmer_CLI.exe")) { return $dir }
    }
    $fromPath = Get-Command "STM32_Programmer_CLI.exe" -ErrorAction SilentlyContinue
    if ($fromPath) { return (Split-Path -Parent $fromPath.Source) }
    throw "STM32_Programmer_CLI.exe not found. Install STM32CubeProgrammer."
}

function Invoke-NorWrite([string]$Programmer, [string]$ExtLoader, [string]$Image, [switch]$EraseAll) {
    Write-Host "Programming NOR via $ExtLoader ..."
    Write-Host "  image: $Image"
    $progArgs = @(
        "-c", "port=SWD", "mode=HOTPLUG",
        "-el", $ExtLoader,
        "-hardRst"
    )
    if ($EraseAll) {
        $progArgs += @("-e", "all")
    }
    $progArgs += @("-w", $Image)
    & $Programmer @progArgs
    if ($LASTEXITCODE -ne 0) {
        throw "STM32_Programmer_CLI failed (exit $LASTEXITCODE). Confirm SW1=H, SW2=L and CN6 is connected."
    }
}

$CubeBin = Find-CubeProgBin
$SigningTool = Join-Path $CubeBin "STM32_SigningTool_CLI.exe"
$Programmer = Join-Path $CubeBin "STM32_Programmer_CLI.exe"
$ExtLoader = Join-Path $CubeBin "ExternalLoader\MX66UW1G45G_STM32N6570-DK.stldr"
if (-not (Test-Path $Programmer)) { throw "Missing $Programmer" }
if (-not (Test-Path $ExtLoader)) { throw "Missing External Loader $ExtLoader" }

if (-not $MotionHex) {
    $MotionHex = Join-Path $TenomiRoot "landmarks\Model\motion_mlp_data.hex"
}

# --- Motion-only replace: no Appli sign, palm/HL untouched ---
if ($MotionOnly) {
    if (-not (Test-Path $MotionHex)) {
        throw "Missing MotionMLP hex: $MotionHex`nExport from 28_motion_train_dk (export_motion_mlp_nor.py) first."
    }
    Write-Host "MotionMLP replace-only @ 0x70A00000"
    Write-Host "  $MotionHex"
    Write-Host "  Appli / palm / HL are not rewritten."
    if ($NoFlash) {
        Write-Host "Skipping flash (-NoFlash). Write this hex at 0x70A00000 when ready."
        exit 0
    }
    if ($Erase) {
        throw "-Erase with -MotionOnly would wipe Appli/palm/HL. Omit -Erase for a motion replace."
    }
    Invoke-NorWrite -Programmer $Programmer -ExtLoader $ExtLoader -Image $MotionHex
    Write-Host ""
    Write-Host "MotionMLP updated. Power-cycle the board (Debug: F11 again, or Flash boot: leave SW1=L, SW2=L)."
    exit 0
}

if (-not (Test-Path $SigningTool)) { throw "Missing $SigningTool" }

if (-not $BinPath) {
    $BinPath = Join-Path $TenomiRoot "mtk3bsp2_stm32n657\Appli\Release\mtk3bsp2_stm32n657_Appli.bin"
}
if (-not (Test-Path $BinPath)) {
    throw "Release Appli .bin not found: $BinPath`nBuild mtk3bsp2_stm32n657_Appli / Release in CubeIDE first."
}

$PalmHex = Join-Path $TenomiRoot "landmarks\Model\palm_detector_data.hex"
$HlHex = Join-Path $TenomiRoot "landmarks\Model\hand_landmark_data.hex"
$FsblHex = Join-Path $TenomiRoot "landmarks\FSBL\ai_fsbl.hex"
foreach ($f in @($PalmHex, $HlHex, $FsblHex)) {
    if (-not (Test-Path $f)) { throw "Missing $f" }
}
if (-not $SkipMotion -and -not (Test-Path $MotionHex)) {
    throw "Missing $MotionHex`nExport MotionMLP or pass -SkipMotion."
}

$Dist = Join-Path $TenomiRoot "dist"
New-Item -ItemType Directory -Force -Path $Dist | Out-Null
$SignedBin = Join-Path $Dist "mtk3bsp2_stm32n657_Appli_sign.bin"
if (-not $OutHex) {
    $OutHex = Join-Path $Dist "tenomi_utk_release.hex"
}

$AppSlotEnd = 0x70380000
$AppSlotStart = 0x70100000
$binSize = (Get-Item $BinPath).Length
# Header 0x400 + optional align padding; keep a 64 KiB margin under palm.
if ($binSize -gt ($AppSlotEnd - $AppSlotStart - 0x10000)) {
    throw "Appli .bin is $binSize bytes — too large for NOR slot 0x70100000..0x70380000"
}

Write-Host "Signing Release Appli -> $SignedBin"
$signArgs = @(
    "-bin", $BinPath,
    "-nk",
    "-t", "ssbl",
    "-hv", "2.3",
    "-align",
    "-s",
    "-o", $SignedBin
)
& $SigningTool @signArgs
if ($LASTEXITCODE -ne 0) {
    throw "STM32_SigningTool_CLI failed (exit $LASTEXITCODE)"
}
if (-not (Test-Path $SignedBin)) {
    throw "Signing produced no file: $SignedBin"
}

$mergePy = Join-Path $PSScriptRoot "merge_intel_hex.py"
$py = Get-Command python -ErrorAction SilentlyContinue
if (-not $py) { $py = Get-Command python3 -ErrorAction SilentlyContinue }
if (-not $py) { throw "python not found (needed to merge Intel HEX)" }

$binSpec = "0x{0:X}:{1}" -f $AppSlotStart, $SignedBin
$mergeArgs = @(
    $mergePy,
    "-o", $OutHex,
    "--hex", $FsblHex,
    "--hex", $PalmHex,
    "--hex", $HlHex,
    "--bin", $binSpec
)
$motionNote = ""
if (-not $SkipMotion) {
    $mergeArgs += @("--hex", $MotionHex)
    $motionNote = " + MotionMLP"
}

Write-Host "Merging FSBL + signed Appli + palm + HL$motionNote"
& $py.Source @mergeArgs
if ($LASTEXITCODE -ne 0) {
    throw "merge_intel_hex.py failed (exit $LASTEXITCODE)"
}

Write-Host ""
Write-Host "Combined HEX: $OutHex"
Write-Host "  SW1=H, SW2=L  (Development mode) to program"
Write-Host "  SW1=L, SW2=L  (Boot from flash) after programming, then power-cycle"
if ($SkipMotion) {
    Write-Host "  MotionMLP omitted (-SkipMotion). Replace later with -MotionOnly."
} else {
    Write-Host "  Retrain replace: .\tools\install_release.ps1 -MotionOnly   (no Appli rebuild)"
}

if ($NoFlash) {
    Write-Host "Skipping flash (-NoFlash)."
    exit 0
}

Invoke-NorWrite -Programmer $Programmer -ExtLoader $ExtLoader -Image $OutHex -EraseAll:$Erase

Write-Host ""
Write-Host "Install done. Set SW1=L, SW2=L and power-cycle to boot from flash."
