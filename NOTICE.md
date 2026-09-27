# NOTICE

本リポジトリは STMicroelectronics 公式パッケージ（`x-cube-n6-ai-hand-landmarks`）由来のコード・ライブラリと、TRON Forum 提供の μT-Kernel 3.0（`mtk3bsp2_stm32n657/Appli/mtk3_bsp2`）を含みます。ディレクトリ・コンポーネントごとの著作権・ライセンスは下表のとおりです。各ディレクトリ配下の `LICENSE.md` / `LICENSE.txt` にも同内容が記載されています。

**本リポジトリのルートには、単一のOSSライセンス（MIT等）は置いていません。** 一部の同梱コンポーネントのライセンス条件（下表 SLA0044 等）が、リポジトリ全体を単一のOSSライセンスで覆うことを許容していないためです。

| Component | Copyright | License |
|:---------|:----------|:-------|
| CMSIS | Arm Limited | Apache-2.0 |
| STM32N6xx CMSIS | ARM Limited, STMicroelectronics | Apache-2.0 |
| STM32N6xx HAL/LL Drivers | STMicroelectronics | BSD-3-Clause |
| STM32N6570-DK BSP Drivers | STMicroelectronics | BSD-3-Clause |
| BSP Component aps256xx | STMicroelectronics | BSD-3-Clause |
| BSP Component Common | STMicroelectronics | BSD-3-Clause |
| BSP Component mx66uw1g45g | STMicroelectronics | BSD-3-Clause |
| BSP Component rk050hr18 | STMicroelectronics | BSD-3-Clause |
| Fonts Utility | STMicroelectronics | BSD-3-Clause |
| lcd Utility | STMicroelectronics | BSD-3-Clause |
| FreeRTOS kernel | Amazon.com, Inc. or its affiliates | MIT |
| Nema.GFX | Think Silicon S.A. | SOFTWARE LICENSE AGREEMENT FOR THINK SILICON USER DRIVER SOFTWARE |
| Gcc | STMicroelectronics | SLA0044 |
| Inc | STMicroelectronics | SLA0044 |
| Src | STMicroelectronics | SLA0044 |
| EWARM/Src | STMicroelectronics | SLA0044 |
| Lib/ipl | STMicroelectronics | SLA0044 |
| Lib/screenl | STMicroelectronics | SLA0044 |
| Lib/screenl/uvcl | STMicroelectronics | SLA0044 |
| Lib/Camera_Middleware | STMicroelectronics | SLA0044 |
| Lib/Camera_Middleware/sensors | STMicroelectronics | BSD-3-Clause |
| Lib/Camera_Middleware/ISP_Library | STMicroelectronics | SLA0044 |
| Lib/lib_vision_models_pp/lib_vision_models_pp | STMicroelectronics | SLA0044 |
| Lib/AI_Runtime | STMicroelectronics | SLA0044 |
| Binary | STMicroelectronics | SLA0044 |
| Model | Google | Apache-2.0 |
| mtk3bsp2_stm32n657/Appli/mtk3_bsp2（μT-Kernel 3.0） | TRON Forum | T-License 2.1 |

**Nema.GFX について:** ヘッダ・テンプレートのみ本リポジトリに同梱しています。コンパイル済みのライブラリ本体（`.a`／`.lib`）は、上記ライセンスが配布を「完成したファームウェアに組み込んだバイナリ形式」に限っているため同梱していません。`tools/setup_nema_lib.ps1` / `.sh` でSTMicroelectronics公式リポジトリから取得してください（README参照）。

**Azure RTOS USBX（Microsoft Software License for Azure RTOS）** は元パッケージに含まれていましたが、本ファームウェアのビルドで使用していないため本リポジトリには含めていません。
