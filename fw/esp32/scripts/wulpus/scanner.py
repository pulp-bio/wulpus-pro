from time import sleep
from dataclasses import dataclass

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
        print(info)
        self.found.append(info)
    
    def update_service(self, zeroconf, type, name):
        pass

@dataclass
class WulpusNetworkDevice:
    name: str
    server: str
    ip: str
    port: int

class WulpusScanner:
    def __init__(self, service_name: str = "wulpus", service_type: str = "tcp"):
        self.service_name = service_name
        self.service_type = service_type
        self.service = f"_{service_name}._{service_type}.local."
        self.found = []
        self.devices = []
    
    def find(self, timeout=5):
        zeroconf = Zeroconf()
        listener = _Listener(self.service)
        browser = ServiceBrowser(zeroconf, self.service, listener)

        while not listener.found and timeout > 0:
            sleep(0.1)
            timeout -= 0.1
        zeroconf.close()
        
        if not listener.found:
            print(f"Could not find service '{self.service}'")
            return []

        self.found = listener.found

        self.devices = []
        print(f"Found {len(self.found)} devices")
        for service in self.found:
            name = service.name
            server = service.server.replace(".local.", ".local")
            ip = bytes_to_ip(service.addresses[0])
            port = service.port

            print(f"  {name} at {server} ({ip}:{port})")
            self.devices.append(WulpusNetworkDevice(name=name, server=server, ip=ip, port=port))

        return self.devices