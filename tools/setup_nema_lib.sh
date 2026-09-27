#!/bin/sh
# NemaGFX ライブラリ本体（.a / .lib）のセットアップ
# NemaGFXのヘッダ・テンプレートは本リポジトリに同梱済み。コンパイル済みの
# .a/.libのみ、STのライセンス条件（Ported Software Packageに組み込んだバイナリ
# 形式でのみ配布可）によりTENOMI側で再配布できないため同梱していない。
# 本スクリプトはSTMicroelectronics公式リポジトリ（x-cube-n6-ai-hand-landmarks,
# タグ v2.2.0）の Lib/NemaGFX/lib を実行時に都度取得して landmarks/Lib/NemaGFX/lib
# に配置する（TENOMI側でのキャッシュ・ミラーは行わない）。
set -eu

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
TENOMI_ROOT=${TENOMI_ROOT:-"$(dirname "$SCRIPT_DIR")"}
SOURCE_REPO=${SOURCE_REPO:-"https://github.com/STMicroelectronics/x-cube-n6-ai-hand-landmarks.git"}
SOURCE_REF=${SOURCE_REF:-"v2.2.0"}

DEST_DIR="$TENOMI_ROOT/landmarks/Lib/NemaGFX/lib"
CHECK_FILE="$DEST_DIR/core/cortex_m55/gcc/libnemagfx-float-abi-hard.a"

echo "TENOMI: NemaGFX lib setup"
echo "  Destination: $DEST_DIR"
echo "  Source:      $SOURCE_REPO (ref: $SOURCE_REF)"

if [ -f "$CHECK_FILE" ]; then
    echo "  Already present — OK"
    exit 0
fi

if ! command -v git >/dev/null 2>&1; then
    echo ""
    echo "ERROR: git が見つかりません。" >&2
    exit 1
fi

TEMP_DIR=$(mktemp -d)
cleanup() {
    rm -rf "$TEMP_DIR"
}
trap cleanup EXIT

echo "  Cloning $SOURCE_REPO@$SOURCE_REF (sparse) ..."
if ! git clone --filter=blob:none --sparse --branch "$SOURCE_REF" --depth 1 "$SOURCE_REPO" "$TEMP_DIR"; then
    echo ""
    echo "ERROR: git clone に失敗しました。" >&2
    echo "手動での取得方法:" >&2
    echo "  $SOURCE_REPO の $SOURCE_REF タグから Lib/NemaGFX/lib を取得し、" >&2
    echo "  $DEST_DIR にそのまま配置してください。" >&2
    exit 1
fi

(cd "$TEMP_DIR" && git sparse-checkout set Lib/NemaGFX/lib)

FETCHED_DIR="$TEMP_DIR/Lib/NemaGFX/lib"
if [ ! -d "$FETCHED_DIR" ]; then
    echo "ERROR: sparse-checkout後にLib/NemaGFX/libが見つかりません。" >&2
    exit 1
fi

mkdir -p "$DEST_DIR"
cp -R "$FETCHED_DIR"/. "$DEST_DIR"/

echo "  Done. Placed at: $DEST_DIR"
