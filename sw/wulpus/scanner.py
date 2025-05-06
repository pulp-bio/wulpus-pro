from time import sleep
from dataclasses import dataclass
import threading

from zeroconf import ServiceBrowser, ServiceListener, Zeroconf

listener = None
message = None


def bytes_to_ip(ip_bytes):
    return ".".join(map(str, ip_bytes))


class _Listener(ServiceListener):
    def __init__(self, service):
        self.service = service
        self.found = []

    def remove_service(self, zeroconf, type, name):
        self.found.remove(name)

    def add_service(self, zeroconf, type, name):
        info = zeroconf.get_service_info(type, name)
        # print(info)
        self.found.append(info)

    def update_service(self, zeroconf, type, name):
        pass


@dataclass
class WulpusNetworkDevice:
    name: str
    server: str
    ip: str
    port: int

    @property
    def description(self):
        return str(self.ip) + ":" + str(self.port)


class WulpusScanner:
    def __init__(self, service_name: str = "wulpus", service_type: str = "tcp"):
        self.service_name = service_name
        self.service_type = service_type
        self.service = f"_{service_name}._{service_type}.local."
        self.found = []
        self.devices = []

    def browse(self, timeout):
        zeroconf = Zeroconf()
        self.listener = _Listener(self.service)
        browser = ServiceBrowser(zeroconf, self.service, self.listener)

        while not self.listener.found and timeout > 0:
            sleep(0.1)
            timeout -= 0.1
        zeroconf.close()

    def find(self, timeout=5):
        thread = threading.Thread(target=self.browse, args=(timeout,))
        thread.start()
        thread.join(timeout)
        thread.join()
        if thread.is_alive():
            print("Timeout reached, stopping the scan")
            thread.join()

        if not self.listener.found:
            print(f"Could not find service '{self.service}'")
            return []

        self.found = self.listener.found

        self.devices = []
        print(f"Found {len(self.found)} devices")
        for service in self.found:
            name = service.name
            server = service.server.replace(".local.", ".local")
            ip = bytes_to_ip(service.addresses[0])
            port = service.port

            print(f"  {name} at {server} ({ip}:{port})")
            self.devices.append(
                WulpusNetworkDevice(name=name, server=server, ip=ip, port=port)
            )

        return self.devices
