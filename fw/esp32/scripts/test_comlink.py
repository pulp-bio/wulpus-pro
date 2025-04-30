# /// script
# dependencies = [
#   "zeroconf",
# ]
# ///

from wulpus import WulpusESP

esp = WulpusESP()

print(esp.get_available())

esp.open()
print(esp.device)

esp.send_config()