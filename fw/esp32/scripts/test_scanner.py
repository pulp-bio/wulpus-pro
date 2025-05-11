# /// script
# dependencies = [
#   "zeroconf",
# ]
# ///

from wulpus import WulpusScanner, WulpusNetworkDevice

import sys
import zeroconf

# Print python version
print(sys.version)

print(zeroconf.__version__)

print(WulpusScanner().find())