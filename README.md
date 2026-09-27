# tenomi-motion

TRONプログラミングコンテスト2026 応募作品「TENOMI」— **DKボード部**（本体）のファームウェアです。STM32N6570-DK上で μT-Kernel 3.0 を用い、カメラ映像から手の動き（ジェスチャー）をボード単体で判定します。

## TENOMIとは

TENOMIは3部構成の作品です。

- **DKボード部（本体・本リポジトリ）** — STM32N6570-DK。カメラでパーム検出→ハンドランドマーク検出→手の動きのモーション推論までを、**ボード単体で完結**させます。外部PCでの後処理は不要です。
- **Bridge部** — Raspberry Pi 5。DKボードの判定結果を受け取り、他デバイスへ橋渡しします。
- **Rover部** — micro:bit v2 搭載の走行ロボット。ジェスチャーに応じて動作します。

Bridge部・Rover部は別リポジトリで扱うため、本READMEでは概要のみとします。

## 構成

```
tenomi-motion/
├── landmarks/            カメラ・NPU推論パイプライン（パーム検出・ハンドランドマーク・
│                          NemaGFXによるGPU回転・モーション推論）
├── mtk3bsp2_stm32n657/    μT-Kernel 3.0 プロジェクト（STM32CubeIDE、Appli と FSBL の2構成）
├── tools/                 セットアップ・書込み用スクリプト
└── dist/                  ビルド／書込みの出力先（ビルドすると生成されます）
```

## 必要な環境

- [STM32CubeIDE](https://www.st.com/en/development-tools/stm32cubeide.html)
- [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html)（External Loaderを同梱。書込みに使用）
- Git（NemaGFXライブラリの取得に使用）
- Python 3（HEXファイルのマージに使用）

## クローンからビルドまで

### 1. クローン

```sh
git clone https://github.com/tenomi-akkabane/tenomi-motion.git
```

### 2. NemaGFXライブラリ本体（`.a`）を取得する

NemaGFX（Think Silicon社製のGPU描画ライブラリ）は、ヘッダ・テンプレートは同梱していますが、コンパイル済みのライブラリ本体（`.a`／`.lib`）だけは本リポジトリに含めていません。STのライセンス条件が、配布を「完成したファームウェアに組み込んだバイナリ形式」に限っているためです。
STMicroelectronics公式リポジトリ（`x-cube-n6-ai-hand-landmarks`、タグ `v2.2.0`）から直接取得し、`landmarks/Lib/NemaGFX/lib/` に配置します。

```sh
# Windows
.\tools\setup_nema_lib.ps1

# macOS / Linux
./tools/setup_nema_lib.sh
```

取得できない場合でもビルドは可能です。`mtk3bsp2_stm32n657/Appli/Release/Application/hand_landmark/tenomi_build_opts.mk`（Debug側も同様）の `TENOMI_NEMA_ENABLE := 1` を `0` にすると、GPU回転を使わないCPU経路でビルドできます（検出精度は多少低下します）。

### 3. STM32CubeIDEでプロジェクトを開く

STM32CubeIDEで `mtk3bsp2_stm32n657/FSBL` と `mtk3bsp2_stm32n657/Appli` の2プロジェクトをインポートします（File > Open Projects from File System...）。

### 4. ビルド

1. `mtk3bsp2_stm32n657_FSBL` を **Release** 構成でビルド
2. `mtk3bsp2_stm32n657_Appli` を **Release** 構成でビルド

## 実機への書き込みと実行

### 1. 書き込み

DKボードを **Development mode**（SW1=H、SW2=L）にしてPCに接続し、以下を実行します。

```sh
# Windows
.\tools\install_release.ps1

# macOS / Linux
./tools/install_release.sh
```

Appliバイナリの署名 → FSBL・Appli・パーム検出・ハンドランドマーク・モーション推論モデルのHEXを1本にマージ → STM32CubeProgrammerでNORフラッシュへ書き込み、を1コマンドで行います（出力は `dist/tenomi_utk_release.hex` 等）。

### 2. 起動

書き込み完了後、**Flashモード**（SW1=L、SW2=L）に切り替えて電源を入れ直します。カメラ映像からパーム検出→ハンドランドマーク検出→モーション推論までがボード単体で動作し、判定結果がLCD／LEDに表示されます。

## 本リポジトリに含まれていないもの

| 含まれていないもの | 理由 | 入手方法 |
|---|---|---|
| NemaGFXライブラリ本体（`.a`／`.lib`） | STのライセンス条件（バイナリ形式でのみ配布可） | `tools/setup_nema_lib.ps1`／`.sh` が自動取得（上記） |
| 完成済みビルド出力（`dist/`、`Appli/Debug`・`Release`の中間ファイル） | ビルド生成物のため同梱不要 | 手順どおりビルド／書込みすれば生成されます |

## ライセンス

同梱コンポーネントごとの著作権・ライセンスは [`NOTICE.md`](NOTICE.md) にまとめています。リポジトリルートには単一のOSSライセンスファイルは置いていません（同梱コンポーネントの一部ライセンス条件による）。
