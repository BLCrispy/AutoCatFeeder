# Automated IoT Cat Feeder

An automated, network-connected cat feeder that dispenses precise food portions through an http webserver hosted on core0 of the RP2040 on a RPI Pico W which talks with a homebridge server using the HTTP advanced accessory plugin to integrate the device with homekit. This allows programming the food portions, automating feeding times, as well as manual dispensing food through 2 "devices" in homekit. This Device utilizes a load cell sensor to detect food weight as it dispenses food onto the plate to determine when the device has met the programmed portion while also upkeeping a minimum dispense time to ensure an adequate amount of food is dispensed in case of sensor issues and is displayed on the SSD1315 display as well as the time/date, wifi connectivity, current portion size, dispensing state, and last time dispensed. Buzzer is programmed through DMA and PIO to allow for a jingle to play when dispensing action is triggered.

---

## 📋 Table of Contents
- [Overview](#-overview)
- [External Submodules](#-external-submodules)
- [Hardware Requirements](#-hardware-requirements)
- [Clone Repository](#-clone-repository)
- [Configuration](#-configuration)
- [Usage](#-usage)

---

## Overview

I created this project as a cheaper DIY alternative to normal automated cat feeders that I could personalize to my exact needs. I found a 3D Model for a Automated Cat Feeder project someone made and posted in MakerWorld which I have linked at the bottom. This model looked to be able to mostly fit my idea for what electronics I wanted to fit this device with which allowed me to do minimal changes to electronics department of the model in fusion. The code for this project include 3 git submodules, 2 of which are made by others which help serve as APIs for interfacing with the SSD1315 display and HX711 Module, 1 is made by me simply so I could use the code in other projects if I liked which allows for playing jingles on a passive buzzer without main core intervention through the use of a DMA channel to load the jingle into and the PIO to be able to actually play the jingle to the buzzer. I have designed this such that the main core (core 0) runs an http webserver that uses cgi and ssi handlers to talk with a homebridge server using the HTTP advanced accesory plugin by staromeste to allow for homekit to control and automate this the feeder completely remotely. Homekit is able to then control the portion size for the feeder and trigger a dispensing action as well as automate feeding through its automation functionality. I then have designated core 1 to handle all other logic due to timing of different devices that I worried would interfere with the webservers connectivity. Core 0 handles the http webserver and sending connection status to core 1. Core 1 handles all the logic for displaying the SSD1315 screen with all necessary data such as the time/date, wifi connectivity status, dispensing status, last time dispensed, current weight when dispensing, and current portion size. The RP2040 only has 2 ARM Cortex-M0+ cores so after completing all this logic the last thing I had to add was the buzzer which I wanted it to be able to run WHILE the food was dispensing while still allowing core 1 to poll and pull weight data from the HX711 without interruption during the dispensing action. To circumvent this I utilized the RP2040's PIO cores and a DMA channel which allowed me to push 32-bit large note size jingles to the DMA channel which then after configuring and point the DMA to the desired jingle handles pushing the jingle into the FIFO to the PIO note by note then allow the PIO to play jingles to the buzzer all without needing a main core to handle the playing the jingle itself and taking up the core which could instead by reading the values taken from the HX711. 

---

## 📦 External Submodules

This repository contains Git submodules. Ensure you clone with the `--recursive` flag to pull all external modules.

| Submodule Path | Source Repository | Purpose |
| :--- | :--- | :--- |
| `extern/displaylib_1bit_PICO` | `https://github.com/gavinlyonsrepo/displaylib_1bit_PICO` | Send Display to SSD1315 |
| `extern/hx711-pico-c` | `https://github.com/endail/hx711-pico-c` | Weight detection for the food bowl |
| `extern/buzzer-pio` | `https://github.com/BLCrispy/buzzer-pio` | Play jingles on passive buzzer w/o cpu |

---

## 🔌 Hardware Requirements

### Component List
*   **Microcontroller:** Raspberry Pi Pico W 
*   **Motor:** JG7 12V high RPM Motor
*   **Relay:** SONGLE SRD-05VDC-SL-C
*   **Weight Sensor:** HX711 Load Cell (5kg max) for weight calibration
*   **Display:** SSD1315
*   **Buck Converter:** MP1585EN DC-DC Buck Converter
*   **Power Adapter:** 12V 2A Barrel Plug Power Adapter
*   **Power Plug:** Female Barrel Jack Plug 2A rated
*   **Wires:** Breadboard Jumper Wires
*   **Soldering Kit:** Any Soldering Kit
*   **Capacitors:** Electrolytic and Ceramic Capacitor Kit
*   **Bolts:** M3 and M4 Bolts
*   **Resistor:** 100 Ohm Resistor
*   **Homekit:** Homekit Stuff
*   **Computer:** Any PC capable of running Homebridge server 24/7

### Pico Pin Configuration
```text
[HX711 Scale]  --> DT: GPIO 15  | SCK: GPIO 14 | VCC: 3.3VOUT
[Relay]  --> IN: GPIO 2 | VCC: 3.3V M1584EN OUT+  
[SSD1315]   --> SDA: GPIO 0 | SDL: GPIO 1 | VCC: 3.3VOUT
[Buzzer]   --> IN: GPIO 3 | VCC: 3.3VOUT
```

---

## Clone Repository
To fetch the project along with all the external GitHub submodules, run:
```bash
git clone --recursive https://github.com
cd feedbot
```
*Note: If you already cloned it normally, pull the modules using:*
```bash
git submodule update --init --recursive
```

---

## 🔧 Configuration

If you open this repository in VSCode it should be instantly able to be compiled. You should then be able to plug a pico w in bootloader mode to your PC and move the .uf2 file into the pico. To make this work for homekit you should setup homekit and then a homebridge server to work with homekit (I personally already had a homebridge server setup from a roommate), you must install the HTTP advanced accesory plugin to homebridge by staromeste. You can then add your auto cat feeder JSON config to its json config. It should look something like below as that is what I personally use:

**Example `config.json`:**
```json
{
  "feeding_schedule": [
    {"time": "07:00", "portion_grams": 45},
    {"time": "18:00", "portion_grams": 45}
  ],
  "calibration_factor": -405.0,
  "stream_port": 8081
}
```
---
## Acknowledgments & Resources
* [Gemini Initial Research](https://share.gemini.google/2UwQ6rR2SKlL) - Helped with research to learn how http webservers work with the C/C++ PicoSDK
* [Gemini HX711 Research](https://share.gemini.google/BvaolGDBAYSy) - Helped with research to learn how to use HX711 repo I chose to interface with my HX711
* [Gemini Homebridge Research](https://share.gemini.google/R2ld22wtgJma) - Helped with research to learn how to get the http webserver on the pico and the homebridge to interact as intended to interact with the device from homekit
* [Gemini Buzzer PIO Research](https://share.gemini.google/od04ThYlS434) - Helped with research to learn how to program in the PIO to run jingles on the buzzer
* [Claude PIO Debugging](https://claude.ai/share/4263f048-05f5-437d-8808-cc097477c6cb) - Helped smooth out kinks in PIO code
* [Claude modularizing PIO/DMA Buzzer Repo](https://claude.ai/share/9cf11839-2d30-4572-893b-c6379d75bea8) - Helped restructure my PIO/DMA code to run jingles on a passive buzzer to allow it to be used as a module on any project I may want to use it for after this project
* [Raspberry Pi Pico PIO - 8 Little Processors You Can Program](https://www.youtube.com/watch?v=QlKtEA5XKc4) - PIO Research
* [Raspberry Pi Pico - PIO explained](https://www.youtube.com/watch?v=3_fxE2XXgX8) - PIO Research
* [Raspberry Pi Pico PIO - Ep. 1 - Overview with Pull, Out, and Parallel Port](https://www.youtube.com/watch?v=YafifJLNr6I&list=PLiRALtgGsxmZs_LXGkh09Zr2NUmk_mtEI&index=2) - PIO Research
* [Raspberry Pi Pico PIO - Ep. 2 - Side Set, Wait, and Handshaking](https://www.youtube.com/watch?v=BAP_n7gxg6M&list=PLiRALtgGsxmZs_LXGkh09Zr2NUmk_mtEI&index=3) - PIO Research
* [Raspberry Pi Pico PIO - Ep. 8 - Introduction to DMA](https://www.youtube.com/watch?v=OenPIsmKeDI&list=PLiRALtgGsxmZs_LXGkh09Zr2NUmk_mtEI&index=9) - DMA Research
* [pico-sdk github repo](https://github.com/raspberrypi/pico-sdk) - referenced to learn more about picoSDK
* [PicoSDK PDF Documentation](https://pip-assets.raspberrypi.com/categories/609-microcontroller-boards/documents/RP-009085-KB-2-raspberry-pi-pico-c-sdk.pdf) - Chapter 3 was incredibly insightful for learning more about how PIO works and has super helpful examples
* [PicoSDK Documentation](https://www.raspberrypi.com/documentation/pico-sdk/index_doxygen.html#raspberry-pi-pico-sdk) - Great reference for APIs and libraries included in the PicoSDK
* [Burrito Feedo 3D Model](https://makerworld.com/en/models/892368-smart-automatic-pet-feeder#profileId-849156) - 3D Model I used as base for the project as well as gained some inspiration for functionalities I gave mine
* [Burrito Feedo 3D Model Video](https://www.youtube.com/watch?v=R-5Gb3uwxLU) - Displays some of the functionality of their automated cat feeder
---

## 📜 License
Distributed under the MIT License. See `LICENSE` for details.

