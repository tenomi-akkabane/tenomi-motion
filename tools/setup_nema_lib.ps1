# NemaGFX ライブラリ本体（.a / .lib）のセットアップ
# NemaGFXのヘッダ・テンプレートは本リポジトリに同梱済み。コンパイル済みの
# .a/.libのみ、STのライセンス条件（Ported Software Packageに組み込んだバイナリ
# 形式でのみ配布可）によりTENOMI側で再配布できないため同梱していない。
# 本スクリプトはSTMicroelectronics公式リポジトリ（x-cube-n6-ai-hand-landmarks,
# タグ v2.2.0）の Lib/NemaGFX/lib を実行時に都度取得して landmarks/Lib/NemaGFX/lib
# に配置する（TENOMI側でのキャッシュ・ミラーは行わない）。
param(
    [string]$TenomiRoot = (Split-Path $PSScriptRoot -Parent),
    [string]$SourceRepo = "https://github.com/STMicroelectronics/x-cube-n6-ai-hand-landmarks.git",
    [string]$SourceRef = "v2.2.0"
)

$ErrorActionPreference = "Stop"
$destDir = Join-Path $TenomiRoot "landmarks\Lib\NemaGFX\lib"
$checkFile = Join-Path $destDir "core\cortex_m55\gcc\libnemagfx-float-abi-hard.a"

Write-Host "TENOMI: NemaGFX lib setup"
Write-Host "  Destination: $destDir"
Write-Host "  Source:      $SourceRepo (ref: $SourceRef)"

if (Test-Path $checkFile) {
    Write-Host "  Already present — OK"
    exit 0
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Host ""
    Write-Host "ERROR: git が見つかりません。" -ForegroundColor Red
    exit 1
}

$tempDir = Join-Path ([System.IO.Path]::GetTempPath()) ("tenomi_nema_" + [System.Guid]::NewGuid().ToString("N"))

try {
    Write-Host "  Cloning $SourceRepo@$SourceRef (sparse) ..."
    git clone --filter=blob:none --sparse --branch $SourceRef --depth 1 $SourceRepo $tempDir
    if ($LASTEXITCODE -ne 0) { throw "git clone failed" }

    Push-Location $tempDir
    git sparse-checkout set Lib/NemaGFX/lib
    if ($LASTEXITCODE -ne 0) { throw "git sparse-checkout failed" }
    Pop-Location

    $fetchedDir = Join-Path $tempDir "Lib\NemaGFX\lib"
    if (-not (Test-Path $fetchedDir)) { throw "Lib/NemaGFX/lib not found after sparse-checkout" }

    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    Copy-Item -Recurse -Force (Join-Path $fetchedDir "*") $destDir

    Write-Host "  Done. Placed at: $destDir"
}
catch {
    Write-Host ""
    Write-Host "ERROR: NemaGFXライブラリの取得に失敗しました: $_" -ForegroundColor Red
    Write-Host ""
    Write-Host "手動での取得方法:"
    Write-Host "  $SourceRepo の $SourceRef タグから Lib/NemaGFX/lib を取得し、"
    Write-Host "  $destDir にそのまま配置してください。"
    exit 1
}
finally {
    if (Test-Path $tempDir) {
        Remove-Item -Recurse -Force $tempDir -ErrorAction SilentlyContinue
    }
}
