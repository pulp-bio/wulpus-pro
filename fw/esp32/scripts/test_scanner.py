# /// script
# dependencies = [
#   "zeroconf",
# ]
# ///

from wulpus import WulpusScanner, WulpusNetworkDevice

print(WulpusScanner().find())