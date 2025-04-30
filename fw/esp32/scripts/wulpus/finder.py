from time import sleep
from zeroconf import ServiceBrowser, ServiceListener, Zeroconf

listener = None
message = None

def bytes_to_ip(ip_bytes):
    return ".".join(map(str, ip_bytes))

class WulpusServiceFinder:

    class Listener(ServiceListener):
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
    
    class Device:
        def __init__(self, name, server, ip, port):
            self.name = name
            self.server = server
            self.ip = ip
            self.port = port

        # def __repr__(self):
        #     return f"Service '{self.name}' at {self.ip}:{self.port}"
        

    def __init__(self, service):
        self.service = service
        self.found = []
    
    def find(self, timeout=5):
        zeroconf = Zeroconf()
        listener = WulpusServiceFinder.Listener(self.service)
        browser = ServiceBrowser(zeroconf, self.service, listener)

        try:
            sleep(timeout)
        finally:
            zeroconf.close()

        self.found = listener.found
        return self.found
    
    def get_devices(self):
        devices = []
        for service in self.found:
            name = service.name
            server = service.server.replace(".local.", ".local")
            ip = bytes_to_ip(service.addresses[0])
            port = service.port
            devices.append(WulpusServiceFinder.Device(name, server, ip, port))
        return devices