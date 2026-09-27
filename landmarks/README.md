# x-cube-n6-ai-hand-landmarks Application

Computer Vision application demonstrating the deployment of several object detection models execution in series on the STM32N6570-DK board. The chosen use case is single-hand landmark detection. It consists of two models that execute sequentially:

1. A palm detection (pd) model is executed in the first stage to detect the hand’s palm.
2. A hand landmark (hl) detection model is executed in the second stage to identify the landmarks of the hand detected during the first stage. After the execution of the first model, a resize operation takes place to provide the expected input to the second model.
   GPU2D is used to also perform a rotation to orient hand vertically.

This top README gives an overview of the app. Additional documentation is available in the [Doc](./Doc/) folder.

![Screenshot of application running](_htmresc/screenshot.png)

---

## Doc Folder Content

- [Application Overview](Doc/Application-Overview.md)
- [Boot Overview](Doc/Boot-Overview.md)
- [Camera Build Options](Doc/Build-Options.md)

---

## Features Demonstrated

- Multi-threaded application flow (FreeRTOS)
- NPU-accelerated quantized AI model inference
- GPU2D usage to perform resize and rotation
- Execute multiple models in series
- Dual DCMIPP pipelines
- DCMIPP cropping, decimation, and downscaling
- DCMIPP ISP usage
- LTDC dual-layer implementation
- Development mode
- Boot from external flash

---

## Hardware Support

Supported development platforms:

- [STM32N6570-DK](https://www.st.com/en/evaluation-tools/stm32n6570-dk.html) Discovery Board
  - Connect to the onboard ST-LINK debug adapter (CN6) using a __USB-C to USB-C cable__ for sufficient power.
  - OTP fuses are configured for xSPI IOs to achieve maximum speed (200MHz) on xSPI interfaces.

![Board](_htmresc/STM32N6570-DK.png)
STM32N6570-DK board with MB1854B IMX335.

Supported camera modules:

- Provided IMX335 camera module
- [STEVAL-55G1MBI](https://www.st.com/en/evaluation-tools/steval-55g1mbi.html)
- [STEVAL-66GYMAI](https://www.st.com/en/evaluation-tools/steval-66gymai.html)
- [STEVAL-1943-MC1](https://www.st.com/en/evaluation-tools/steval-1943-mc1.html)

---

## Tools Version

- IAR Embedded Workbench for Arm (**EWARM 9.40.1**) + N6 patch ([**EWARMv9_STM32N6xx_V1.0.0**](STM32Cube_FW_N6/Utilities/PC_Software/EWARMv9_STM32N6xx_V1.0.0.zip))
- [STM32CubeIDE](https://www.st.com/content/st_com/en/products/development-tools/software-development-tools/stm32-software-development-tools/stm32-ides/stm32cubeide.html) (__v1.17.0__)
- [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html) (__v2.18.0__)
- [STEdgeAI](https://www.st.com/en/development-tools/stedgeai-core.html) (__v3.0.0__)

---

## Boot Modes

The STM32N6 series does not have internal flash memory. To retain firmware after a reboot, program it into the external flash. Alternatively, you can load firmware directly into SRAM (development mode), but note that the program will be lost if the board is powered off in this mode.

Development Mode: used for loading firmware into RAM during a debug session or for programming firmware into external flash.

Boot from Flash: used to boot firmware from external flash.

|                  | STM32N6570-DK                                                                |
| -------------    | -------------                                                                |
| Boot from flash  | ![STM32N6570-DK Boot from flash](_htmresc/STM32N6570-DK_Boot_from_flash.png) |
| Development mode | ![STM32N6570-DK Development mode](_htmresc/STM32N6570-DK_Dev_mode.png)       |

---

## Console parameters

You can see application messages by attaching a console application to the ST-Link console output. Use the following console parameters:
- Baud rate of 115200 bps.
- No parity.
- One stop bit.

---

## Quickstart using Prebuilt Binaries

The prebuilt binaries are an assembly of several binaries:
  - FSBL (First Stage Boot Loader, loading the application from flash to RAM)
  - The application
  - The weights of the neural network models

To program the board's external flash, follow these steps:

1. Set the board to [development mode](#boot-modes).
2. Program `Binary/x-cube-n6-ai-hand-landmarks-dk.hex`.
3. Set the board to [boot from flash mode](#boot-modes).
4. Power cycle the board.

---

### How to Program Hex Files Using STM32CubeProgrammer UI

See [How to program hex files STM32CubeProgrammer](Doc/Program-Hex-Files-STM32CubeProgrammer.md).

---

### How to Program Hex Files Using Command Line

Ensure the STM32CubeProgrammer `bin` folder is in your PATH.

```bash
export DKEL="<STM32CubeProgrammer_N6 Install Folder>/bin/ExternalLoader/MX66UW1G45G_STM32N6570-DK.stldr"

# Application Firmware
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -el $DKEL -hardRst -w Binary/x-cube-n6-ai-hand-landmarks-dk.hex
```

---

## Quickstart using Source Code

Before building and running the application, you must program `Model/palm_detector_data.hex` and `Model/hand_landmark_data.hex` (models weights and biases). This only needs to be done once unless you change the AI model. See [Quickstart using prebuilt binaries](#quickstart-using-prebuilt-binaries) for details.

For more information about boot modes, see [Boot Overview](Doc/Boot-Overview.md).

---

### Application Build and Run - Dev Mode

Set your board to [development mode](#development-mode).

#### STM32CubeIDE

Double-click `STM32CubeIDE/STM32N6570-DK/.project` to open the project in STM32CubeIDE. Build and run the project.

#### IAR EWARM

Double-click `EWARM/STM32N6570-DK/x-cube-n6-ai-hand-landmarks-dk.eww` to open the project in IAR. Build and run the project.
#### Makefile

Run the following commands (ensure required tools are in your PATH):

1. Build the project:
    ```bash
    make -j8
    ```
2. Start a GDB server connected to the STM32 target:
    ```bash
    ST-LINK_gdbserver -p 61234 -l 1 -d -s -cp <path-to-stm32cubeprogramer-bin-dir> -m 1 -g
    ```
3. In a separate terminal, launch a GDB session to load the firmware:
    ```bash
    $ arm-none-eabi-gdb build/Project.elf
    (gdb) target remote :61234
    (gdb) monitor reset
    (gdb) load
    (gdb) continue
    ```

---

### Application Build and Run - Boot from Flash

Set your board to [development mode](#development-mode).

#### Build the Application

##### STM32CubeIDE

Double-click `STM32CubeIDE/STM32N6570-DK/.project` to open the project in STM32CubeIDE. Build the project.

##### IAR EWARM

Double-click `EWARM/STM32N6570-DK/x-cube-n6-ai-hand-landmarks-dk.eww` to open the project in IAR. Build the project.

##### Makefile

Ensure all required tools are in your PATH, then build the project:

```bash
make -j8
```

#### Program the Firmware in the External Flash

After building the application, you must add a signature to the binary file:

```bash
STM32_SigningTool_CLI -bin build/Project.bin -nk -t ssbl -hv 2.3 -o build/Project_sign.bin
```

Program the signed binary at address `0x70100000`, as well as the FSBL and network parameters.

```bash
export DKEL="<STM32CubeProgrammer_N6 Install Folder>/bin/ExternalLoader/MX66UW1G45G_STM32N6570-DK.stldr"

# First Stage Boot Loader
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -el $DKEL -hardRst -w FSBL/ai_fsbl.hex

# Adjust build path as needed
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -el $DKEL -hardRst -w build/Project_sign.bin 0x70100000

# Network parameters and biases
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -el $DKEL -hardRst -w Model/palm_detector_data.hex
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -el $DKEL -hardRst -w Model/hand_landmark_data.hex
```

__Note__: Only the application binary needs to be programmed if `fsbl` and networks data have already been programmed.

Set your board to [boot from flash](#boot-from-flash) mode and power cycle to boot from external flash.

---

## How to update my project with a new version of ST Edge AI

The neural network models files (`palm_detector.c`, `stai_palm_detector.c/h`, etc.) included in this project were generated using [STEdgeAI](https://www.st.com/en/development-tools/stedgeai-core.html) version 3.0.0.

Using a different version of STEdgeAI to generate these models files may result in the following compile-time error:  
`Possible mismatch in ll_aton library used`.

If you encounter this error, please follow the STEdgeAI instructions on [How to update my project with a new version of ST Edge AI Core](https://stedgeai-dc.st.com/assets/embedded-docs/stneuralart_faqs_update_version.html) to update your project.
