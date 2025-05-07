import socket
import struct
import logging
from enum import IntEnum
import time

import numpy as np

from .scanner import WulpusScanner


# Grab the logger you use in this file (e.g. “WiFi” in your __init__)
wifi_logger = logging.getLogger("WiFi")
wifi_logger.setLevel(logging.DEBUG)
# Prevent messages from bubbling up to the root logger
wifi_logger.propagate = False

# Create and attach a FileHandler just for this logger
file_handler = logging.FileHandler("wulpus.log")
file_handler.setLevel(logging.DEBUG)
formatter = logging.Formatter(
    "%(asctime)s\t%(name)s\t%(funcName)s\t%(levelname)s\t%(message)s",
    "%Y-%m-%d %H:%M:%S",
)
file_handler.setFormatter(formatter)
wifi_logger.addHandler(file_handler)


class WulpusCommand(IntEnum):
    """
    Wulpus command codes.
    """

    SET_CONFIG = 0x57
    GET_DATA = 0x58
    PING = 0x59
    PONG = 0x5A
    RESET = 0x5B
    CLOSE = 0x5C
    START_RX = 0x5D
    STOP_RX = 0x5E

    def __str__(self):
        return f"{self.__class__.__name__}.{self.name}"

    def __repr__(self):
        return str(self)


class WulpusWiFi:
    def __init__(
        self, service_name: str = "wulpus", service_type: str = "tcp", port: int = 2121
    ):
        """
        Constructor.

        Arguments
        ---------
        servie_name : str
            The name of the service to look for.
        service_type : str
            The type of the service to look for ("tcp" / "udp").
        port : int
            The port to connect to.
        """
        self.log = wifi_logger

        self.service_name = service_name
        self.service_type = service_type
        self.port = port
        self.log.info(
            f"Initializing WulpusWiFi with service name: {service_name}.{service_type}:{port}"
        )

        self.scanner = WulpusScanner(service_name, service_type)

        self.device = None
        self.sock = None

        self.backlog = b""

        self.acq_length = 400

        self.log.info("WulpusWiFi initialized")

    def get_available(self):
        """
        Get a list of available devices.
        """
        self.log.info("Getting available devices")

        result = self.scanner.find()

        self.log.info(f"Found {len(result)} devices")
        for device in result:
            self.log.debug(f"Found device: {device}")

        self.log.debug("Done getting available devices")

        return result

    def open(self, device: str = None):
        """
        Open the device connection.
        """
        self.log.info("Opening device connection")

        if self.sock is not None:
            self.log.warning("Device already open")
            return True

        self.device = None
        if device is None:
            self.log.info("No device specified, using scanner")
            if self.scanner.devices:
                self.log.debug("Found device, opening...")
                self.device = self.scanner.devices[0]
            else:
                self.log.error("No devices scanned")
                return False
        else:
            self.device = device

        # Open socket
        self.log.info(f"Opening {self.device}")
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(5)

        try:
            self.sock.connect((device.ip, device.port))
        except Exception as e:
            self.log.error(f"Error connecting to {device}: {e}")
            self.sock = None
            return False

        self.log.info("Opened device connection")

        return True

    def flush(self):
        """
        Flush the device connection.
        """
        self.log.debug("Flushing device connection")

        if self.sock is None:
            self.log.warning("Device not open")
            return

        curr_timeout = self.sock.gettimeout()
        self.sock.settimeout(0.1)

        while True:
            try:
                self.sock.recv(1024)
            except socket.timeout:
                self.log.debug("No more data to receive")
                break

        self.sock.settimeout(curr_timeout)

        self.backlog = b""

        self.log.debug("Flushed device connection")

    def close(self):
        """
        Close the device connection.
        """
        self.log.info("Closing device connection")

        if self.sock is None:
            self.log.warning("Device not open")
            return True

        # Flush the device connection
        self.flush()

        # Send close command
        try:
            self.send_command(WulpusCommand.CLOSE)
        except Exception as e:
            self.log.error(f"Error sending close command: {e}")
            return False

        # Close socket
        self.log.debug("Closing socket")
        self.sock.close()
        self.sock = None
        self.device = None

        self.log.info("Closed device connection")
        return True

    def _encode_command(self, commmand: WulpusCommand, data: bytes = None):
        """
        Encode a command to send to the device.
        """
        if data is None:
            data = b""

        # Create header
        header = (
            b"wulpus" + commmand.to_bytes(1, "little") + len(data).to_bytes(2, "little")
        )

        # Create package
        package = header + data

        return package

    def _decode_command(self, header_bytes: bytes):
        """
        Decode a header received from the device.
        """
        # First 6 bytes: "wulpus" (str)
        # 1 byte: command (WulpusCommand)
        # 2 bytes: length (u16)
        if len(header_bytes) != 9:
            self.log.error(
                f"Invalid header length. Expected 9 bytes, got {len(header_bytes)}"
            )
            raise ValueError(
                f"Invalid header length. Expected 9 bytes, got {len(header_bytes)}"
            )

        # Unpack header
        struct_format = "<6sBH"
        header = struct.unpack(struct_format, header_bytes)
        header = {
            "magic": header[0].decode("utf-8"),
            "command": WulpusCommand(header[1]),
            "length": header[2],
        }
        self.log.debug(f"Decoded header: {header}")

        # Check magic
        if header["magic"] != "wulpus":
            self.log.error(
                f"Invalid header magic. Expected 'wulpus', got {header['magic']}"
            )
            raise ValueError("Invalid header magic. Expected 'wulpus'.")

        return header

    def send_command(
        self, command: WulpusCommand, data: bytes = None, receive: bool = True
    ):
        """
        Send a command to the device.
        """
        self.log.info(f"Sending command: {command}")

        if self.sock is None:
            self.log.error("Device not open")
            raise ValueError("Device not open.")

        # Encode command
        package = self._encode_command(command, data)

        # Send package
        self.log.debug(f"Sending package: {package}")
        self.sock.sendall(package)

        # Check if we need to receive a response
        if not receive:
            self.log.debug("Not receiving response")
            return None, None

        # Receive response
        header, data = self.receive_command()

        # Check command
        if header["command"] != command:
            self.log.error(
                f"Invalid command. Expected {command}, got {header['command']}"
            )
            raise ValueError(
                f"Invalid command. Expected {command}, got {header['command']}"
            )

        return header, data

    def send_config(self, conf_bytes_pack: bytes):
        """
        Send a configuration package to the device.
        """
        self.log.info(f"Sending configuration package of length {len(conf_bytes_pack)}")
        # TODO: Add better receiving
        self.log.debug(f"Configuration package ({len(conf_bytes_pack)} bytes):")
        for byte in conf_bytes_pack:
            self.log.debug(f"  0x{byte:02X}")
        self.flush()
        self.send_command(WulpusCommand.SET_CONFIG, conf_bytes_pack)
        self.log.info("Sent configuration package")

    def receive_command(self, strict_length: bool = True):
        """
        Receive a command from the device.
        """
        self.log.info("Receiving command")

        if self.sock is None:
            self.log.error("Device not open")
            raise ValueError("Device not open.")

        # Receive header
        self.log.debug("Receiving header")
        header = self.sock.recv(9)
        header = self._decode_command(header)
        self.log.debug(f"Received header: {header}")

        # Get data of length header["length"]
        self.log.debug(f"Receiving data of length {header['length']}")
        data = self.sock.recv(header["length"])
        if len(data) != header["length"] and strict_length:
            self.log.error(
                f"Invalid received data length. Expected {header['length']}, got {len(data)}"
            )
            raise ValueError(
                f"Invalid received data length. Expected {header['length']}, got {len(data)}"
            )
        self.log.debug(f"Received data of length {len(data)}")

        # Return header and data
        self.log.debug("Done receiving command")
        return header, data

    def _get_rf_data_and_info__(self, bytes_arr: bytes):
        # First byte: Garbage (u8)
        # First 1 byte: tx_rx_id (u8)
        # 2 bytes: acq_nr (u16)
        # Rest: rf_arr (i2)
        if len(bytes_arr) < 3:
            self.log.warning(
                f"Invalid data length. Expected at least 3 bytes, got {len(bytes_arr)}"
            )
            return None

        struct_format = "<BBH"
        data = struct.unpack(struct_format, bytes_arr[:4])
        data = {
            "tx_rx_id": data[1],
            "acq_nr": data[2],
            "rf_arr": np.frombuffer(bytes_arr[4:], dtype="<i2"),
        }
        self.log.debug(
            f"Decoded RF data: TRX ID: {data['tx_rx_id']}, ACQ NR: {data['acq_nr']}, DATA: {len(data['rf_arr'])}",
        )

        if len(data["rf_arr"]) != self.acq_length:
            self.log.warning(
                f"Invalid data length. Expected {self.acq_length}, got {len(data['rf_arr'])}"
            )
            return None

        return data["rf_arr"], data["acq_nr"], data["tx_rx_id"]

    def receive_data(self):
        """
        Receive a data package from the device.
        """
        self.log.info("Receiving data package")

        if len(self.backlog) > 0:
            self.log.debug("Backlog has data")
            # Check if there is a complete package in the backlog
            header = self._decode_command(self.backlog[:9])
            self.log.debug(f"Backlog header: {header}")
            if len(self.backlog) >= header["length"] + 9:
                self.log.debug("Backlog has complete package")
                # Get data of length header["length"]
                data = self.backlog[9 : header["length"] + 9]
                self.backlog = self.backlog[header["length"] + 9 :]

                return self._get_rf_data_and_info__(data)
            else:
                self.log.warning(
                    "Backlog does not have complete package, discarding it"
                )
                # No complete package in backlog, receive new data
                self.backlog = b""

        start_time = time.time()

        while time.time() - start_time < 5:
            try:
                header, data = self.receive_command(strict_length=False)
            except socket.timeout:
                self.log.warning("Timeout while receiving data")
                return None

            if len(data) != header["length"]:
                self.log.warning(
                    f"Pushing incomplete data to backlog. Expected {header['length']}, got {len(data)}"
                )
                data += self.sock.recv(header["length"] - len(data))

            # Check command
            if header["command"] != WulpusCommand.GET_DATA:
                self.log.warning(
                    f"Invalid command. Expected {WulpusCommand.GET_DATA}, got {header['command']}"
                )
                return None

            break

        # Check if there is magic in the data
        if b"wulpus" in data:
            # Remove magic from data
            self.backlog += b"wulpus" + data.split(b"wulpus")[1]
            data = data.split(b"wulpus")[0]

        # Unpack data
        self.log.debug("Decoding data")
        return self._get_rf_data_and_info__(data)

    def ping(self):
        """
        Ping the device.
        """
        self.log.info("Pinging device")

        header, data = self.send_command(WulpusCommand.PING)

        # Check command
        if header["command"] != WulpusCommand.PING:
            self.log.error(
                f"Invalid command. Expected {WulpusCommand.PING}, got {header['command']}"
            )
            raise ValueError(
                f"Invalid command. Expected {WulpusCommand.PING}, got {header['command']}"
            )

        self.log.info(f"Ping response: {data}")

        self.log.debug("Done pinging device")
        return header, data

    def toggle_rx(self, state: bool):
        """
        Toggle RX state.
        """
        self.log.info(f"Toggling RX state to {state}")

        self.flush()
        try:
            if state:
                self.send_command(WulpusCommand.START_RX)
            else:
                self.send_command(WulpusCommand.STOP_RX)
        except Exception as e:
            self.log.error(f"Error toggling RX state: {e}")
            return False

        self.log.debug("Done toggling RX state")
        return True
