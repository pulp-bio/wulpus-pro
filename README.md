# WULPUS PRO v0.1.0
## A base platform for Wearable Ultra Low Power Ultrasound

# Introduction

This repository contains the work in progress on the WULPUS PRO ultrasound platform, a successor of the [WULPUS Project](https://github.com/pulp-bio/wulpus)

WULPUS PRO is a compact base platform for an ultra low power ultrasound sensing, featuring the following specifications:
- **16 channels** (time-multiplexed)
- **30 V** unipolar excitations with programmable frequency (100 kHz - 10 MHz)
- **Indirect and Direct Bias** (-30 V up to 30 V) for CMUT transducers
- **30.8 dB** Programable-Gain Amplifier + **80 dB** external VGA
- **Programmable** linear Time Gain Compesation Profiles
- **Optional** Envelope Extractor, runtime configurable
- **8 Msps** Analog-to-Digital converter (12 bit resolution)
- **SPI (8 MHz)** host MCU interface
- **Compactness and light weight** (40 x 20 mm footprint)
- **Ultra-Low Power** consumption of **<25 mW**

# WULPUS PRO system diagram

Work in progress.

# Structure of the repository

This repository has the following folders:

- `fw`, containing the firmware source code, namely:
    - source code for the nRF52832 MCU of the nRF52 DK, located at `fw/nrf52/ble_peripheral/US_probe_nRF52_firmware`
    - source code for the nRF52840 USB dongle, located at `fw/nrf52/peripheral/US_probe_dongle_firmware`
    - source code for the MSP430 ultrasound MCU, located at `fw/msp430`
- `sw`, containing the Python code for the WULPUS Pro Graphical User Interface
 
- `hw`, containing the CAD source files for 
    - WULPUS PRO Acquisition PCB (located at `hw/wulpus_pro_acq_pcb_dev_board`)
    - WULPUS adapter PCB for polyCMUT transducer (located at `hw/wulpus_polycmut_adapter`)

- `docs`, containing the project documentation (Work in Progress)

# Documentation 

WULPUS PRO build up on the top of the original WULPUS platfrom. Therefore, please refer to the original [WULPUS User Manual](docs/wulpus_user_manual.pdf) for assembly instructions, example measurements and GUI overview.

# How to reproduce?

To build and run your own instance of the **WULPUS PRO** platform, follow the steps below:

### 1. Manufacture and Assemble the Hardware

Produce the **WULPUS PRO acquisition development board** using the design files in:

- `hw/wulpus_pro_acq_pcb_dev_board`
- (Optionally) `hw/wulpus_polycmut_adapter`

Assemble the PCB using the provided schematics and BOMs.

### 2. Interconnect with nRF52 DK

Use the following pin mapping to connect the WULPUS PRO acquisition board to the **nRF52 DK**:

| **Signal**         | **nRF52 DK Pin** | **WULPUS PRO Connector Pin** |
|--------------------|------------------|-------------------------------|
| `SPI_SS`           | P0.04            | X3.4                          |
| `SPI_CLK`          | P0.29            | X3.3                          |
| `SPI_MISO`         | P0.30            | X3.1                          |
| `SPI_MOSI`         | P0.31            | X3.2                          |
| `Data_ready`       | P0.24            | X4.2                          |
| `BLE_conn_ready`   | P0.25            | X4.3                          |

### 3. Flash the MSP430 Firmware

Navigate to `fw/msp430/` and follow instructions to:

- Set up Code Composer Studio
- Compile the firmware
- Flash the MSP430 MCU

### 4. Flash the nRF52 DK and USB Dongle

Go to `fw/nrf52/` to:

- Set up Segger Studio and nRF Connect SDK
- Compile and flash:
  - nRF52832 DK (`ble_peripheral`)
  - nRF52840 USB dongle (`peripheral`)

### 5. Set Up the Python GUI

1. [Install Miniconda](https://docs.conda.io/en/latest/miniconda.html)
2. Go to the `sw` folder (contains `requirements.yml`)
3. Open terminal (Anaconda Prompt on Windows), run:
   ```bash
   conda env create -f requirements.yml
   conda activate wulpus_env
   jupyter notebook
   ```

## How to use?

To start a measurement with **WULPUS PRO**, follow these steps:

1. **Plug in the USB dongle** and **power the nRF52 DK** via USB.
2. **Check the dongle connection** (green LED should light up).  
   If not, press the **reset button on the nRF52 DK**, then try again.
3. **Only after confirming** dongle connectivity, **power the WULPUS PRO board** (via connector or debug pin headers).
4. Open a **Conda terminal**, activate the environment:
   ```bash
   conda activate wulpus_env
   ```

5. Then navigate to the `sw/` folder and open `wulpus_pro_gui.ipynb`. Follow the notebook instructions to begin acquisition.


# Authors

The WULPUS Pro system was developed at the [Integrated Systems Laboratory (IIS)](https://iis.ee.ethz.ch/) at ETH Zurich by:
- [Sergei Vostrikov](https://scholar.google.com/citations?user=a0KNUooAAAAJ&hl=en) (PCB design, Firmware, Software, Open-Sourcing)
- [Federico Villani](https://scholar.google.com/citations?user=5LgLMCEAAAAJ&hl=en) (PCB design, Component selection)
- [Cédric Hirschi](https://www.linkedin.com/in/c%C3%A9dric-cyril-hirschi-09624021b/) (Firmware, Software)
- [Andrea Cossettini](https://scholar.google.com/citations?user=d8O91jIAAAAJ&hl=en) (Supervision, Project administration)
- [Luca Benini](https://scholar.google.com/citations?hl=en&user=8riq3sYAAAAJ) (Supervision, Project administration)

Thanks to all the people who contributed to the WULPUS Pro platform:
- [Sebastian Frey](https://scholar.google.com/citations?user=7jhiqz4AAAAJ&hl=en) (Design review)

# License
The following files are released under Apache License 2.0 (`Apache-2.0`) (see `sw/LICENSE`):

- `sw/`

The following files are released under Solderpad v0.51 (`SHL-0.51`) (see `hw/LICENSE`):

- `hw/`

The `fw/msp430/` and `fw/nrf52/` directories contain third-party sources that come with their own
licenses. See the respective folders and source files for the licenses used.

## Limitation of Liability
In no event and under no legal theory, whether in tort (including negligence), contract, or otherwise, unless required by applicable law (such as deliberate and grossly negligent acts) or agreed to in writing, shall any Contributor be liable to You for damages, including any direct, indirect, special, incidental, or consequential damages of any character arising as a result of this License or out of the use or inability to use the Work (including but not limited to damages for loss of goodwill, work stoppage, computer failure or malfunction, or any and all other commercial damages or losses), even if such Contributor has been advised of the possibility of such damages.
