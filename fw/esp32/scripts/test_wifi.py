# /// script
# dependencies = [
#   "zeroconf",
# ]
# ///
import time

from wulpus import WulpusWiFi, WulpusCommand

esp = WulpusWiFi()

print(esp.get_available())

esp.open()
print(esp.device)

try:
    print(esp.send_command(WulpusCommand.PING))
    print(esp.receive_command())

    print(esp.send_command(WulpusCommand.SET_CONFIG, b"0123456789abcdef"))

    # with esp:
    #     print(esp.send_command(WulpusCommand.RESET, receive=False))

    # 
    print("Waiting for data...")
    time.sleep(1)
    while True:
        try:
            print(len(esp.receive_data()[0]))
        except Exception as e:
            print(f"Error receiving data: {e}")
            break

    # print("Resetting device...")
    # print(esp.send_command(WulpusCommand.RESET))
except Exception as e:
    print(f"Error: {e}")
finally:
    esp.close()
    print("Closed connection.")