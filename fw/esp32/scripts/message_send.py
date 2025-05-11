import socket
import json
import sys
import time

if __name__ == "__main__":

    if len(sys.argv) > 1:
        message = sys.argv[1]
    else:
        print("Usage: python message_send.py <message>")
        sys.exit(1)
    
    with open("devices.json", "r") as f:
        data = json.loads(f.read())

    for device in data["devices"]:
        print(f"Sending message to {device['ip']}:{device['port']}")

        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        
        try:
            s.connect((device["ip"], device["port"]))
            s.send(message.encode())
            time_start = time.time()
            time_end = time_start

            # Receive answers, until no more data is received for 1s
            answer = b""
            while True:
                data = s.recv(1024)
                if data:
                    answer += data
                    time_end = time.time()
                else:
                    if time.time() - time_end > 1:
                        break

            s.close()

            answer_length = len(answer)
            time_delta = time_end - time_start
            # data rate in megabits per second
            data_rate = answer_length * 8 / time_delta / 1024 / 1024

            print(f"Received answer of length {answer_length} in {time_delta:.6f}s ({data_rate:.2f} mbps)")
            # print first 100 bytes of answer
            print(answer[:100].decode())
        except ConnectionRefusedError:
            print(f"Connection to {device['ip']}:{device['port']} refused")
            s.close()
