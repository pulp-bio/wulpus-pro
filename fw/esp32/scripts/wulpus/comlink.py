import socket
import struct
from enum import IntEnum

from .scanner import WulpusScanner, WulpusNetworkDevice

class WulpusCommand(IntEnum):
    """
    Wulpus command codes.
    """
    SET_CONFIG = 0x57
    GET_DATA = 0x58
    PING = 0x59
    RESET = 0x5A

class WulpusESP():
    def __init__(self, service_name: str = "wulpus", service_type: str = "tcp", port:int = 2121):
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
        self.service_name = service_name
        self.service_type = service_type
        self.port = port

        self.scanner = WulpusScanner(service_name, service_type)

        self.device = None
        self.sock = None


    def get_available(self):
        """
        Get a list of available devices.
        """

        return self.scanner.find()


    def open(self, device: WulpusNetworkDevice = None):
        """
        Open the device connection.
        """
        if self.sock is not None:
            return

        if device is None:
            if self.scanner.devices:
                device = self.scanner.devices[0]
            else:
                raise ValueError("No device found.")

        self.device = device

        # Open socket
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(5)
        self.sock.connect((device.ip, device.port))

    def close(self):
        """
        Close the device connection.
        """
        if self.sock is None:
            return

        self.sock.close()
        self.sock = None
        self.device = None


    def _encode_command(self, commmand: WulpusCommand, data: bytes = None):
        """
        Encode a command to send to the device.
        """
        if data is None:
            data = b""

        # Create header
        header = b"wulpus" + commmand.to_bytes(1, 'little') + len(data).to_bytes(2, 'little')

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
            raise ValueError("Invalid header length. Expected 9 bytes.")

        # Unpack header
        struct_format = "<6sB2H"
        header = struct.unpack(struct_format, header_bytes)
        header = {
            "magic": header[0].decode("utf-8"),
            "command": WulpusCommand(header[1]),
            "length": header[2],
        }

        # Check magic
        if header["magic"] != "wulpus":
            raise ValueError("Invalid header magic. Expected 'wulpus'.")

        return header
    
    def send_command(self, command: WulpusCommand, data: bytes = None):
        """
        Send a command to the device.
        """
        if self.sock is None:
            raise ValueError("Device not open.")

        # Encode command
        package = self._encode_command(command, data)

        # Send package
        self.sock.sendall(package)

        # Receive response
        header, data = self.receive_command()

        # Check command
        if header["command"] != command:
            raise ValueError(f"Invalid command. Expected {command}, got {header['command']}")

        return header, data

    def send_config(self, conf_bytes_pack: bytes):
        """
        Send a configuration package to the device.
        """
        _, data = self.send_command(WulpusCommand.SET_CONFIG, conf_bytes_pack)

        if data != conf_bytes_pack:
            raise ValueError("Invalid configuration response. Expected same data.")
    
    def receive_command(self):
        """
        Receive a command from the device.
        """
        if self.sock is None:
            raise ValueError("Device not open.")

        # Receive header
        header = self.sock.recv(9)
        header = self._decode_command(header)

        # Get data of length header["length"]
        data = self.sock.recv(header["length"])
        if len(data) != header["length"]:
            raise ValueError(f"Invalid received data length. Expected {header['length']}, got {len(data)}")

        # Return header and data
        return header, data
    

    def _get_rf_data_and_info__(self, bytes_arr: bytes):
        # First 1 byte: tx_rx_id (u8)
        # 2 bytes: acq_nr (u16)
        # Rest: rf_arr (i2)
        if len(bytes_arr) < 3:
            raise ValueError("Invalid data length. Expected at least 3 bytes.")

        struct_format = "<B2H"
        data = struct.unpack(struct_format, bytes_arr[:3])
        data = {
            "tx_rx_id": header[0],
            "acq_nr": header[1],
            "rf_arr": bytes_arr[3:],
        }

        return data["rf_arr"], data["acq_nr"], data["tx_rx_id"]

    def receive_data(self):
        """
        Receive a data package from the device.
        """
        header, data = self.receive_command()

        # Check command
        if header["command"] != WulpusCommand.GET_DATA:
            raise ValueError(f"Invalid command. Expected {WulpusCommand.GET_DATA}, got {header['command']}")

        # Unpack data
        return self._get_rf_data_and_info__(data)
