# WULPUS PRO ESP32 Firmware

This firmware enables WULPUS PRO to connect to Wi-Fi via a ESP32-C6, which enables higher throughputs and thus framerates.

The firmware includes the following features:
- **mDNS**: Multicast DNS for local network discovery
- **Full integration with the Python API**: The firmware also comes with an extension to the Python API, which allows you to control the WULPUS PRO over Wi-Fi
- **Power Management**: The firmware includes power management, which allows the ESP32 to enter light sleep when not in use. This results in ~1.5mA current draw
- **Easy expandability**: The firmware is designed to be easily extensible, allowing you to add new features and functionality as needed

## Getting Started

This firmware is written with [ESP-IDF](https://github.com/espressif/esp-idf). We recommend using at the official [VS Code extension](https://github.com/espressif/vscode-esp-idf-extension/tree/master).

The firmware is tested on the [ESP32-C6-DEVKITM-1](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitm-1/user_guide.html) board, but should work on any ESP32-C6 board. The firmware is designed to be easily portable to other ESP32 boards as well.

### Connection to WULPUS PRO

Use the following pin mapping to connect the WULPUS PRO acquisition board to the **ESP32-C6-DEVKITM-1**:

| **Signal**         | **ESP32-C6 Pin** | **WULPUS PRO Connector Pin**  |
|--------------------|------------------|-------------------------------|
| `SPI_SS`           | 18               | X3.4                          |
| `SPI_CLK`          | 6                | X3.3                          |
| `SPI_MISO`         | 7                | X3.1                          |
| `SPI_MOSI`         | 2                | X3.2                          |
| `Data_ready`       | 1                | X4.2                          |
| `BLE_conn_ready`   | 0                | X4.3                          |

These pins can be changed however you like by using the [SDK Configurator](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/configureproject.html).

### Usage in the Python API

The Python API remains largely unchanged, the only thing you need to do in order to use the Wi-Fi connection is to pass the `WulpusWiFi` communication link to the `WulpusGuiSingleCh` class instead of the `WulpusDongle` class. This will look like this:

```python
from wulpus.wifi import WulpusWiFi

# Create a wifi object
wifi = WulpusWiFi()

# Setup the GUI (uss_conf is already setup and configured)
gui = WulpusGuiSingleCh(dongle, uss_conf)

display(gui)
```

## TODO

This firmware is still a work in progress. The following features are planned for future releases (among others):

- [ ] Add LED status codes
- [ ] Return error codes on the socket on failures

## Licensing

ESP-IDF is licensed under the Apache License 2.0. See the [LICENSE](https://github.com/espressif/esp-idf/blob/master/LICENSE) in the ESP-IDF repository for more information.

ESP-IDF uses multiple third-party components, which are licensed under various other open-source licenses. One example is [FreeRTOS](https://github.com/FreeRTOS). Please make sure all of the licenses are compatible with your project.
