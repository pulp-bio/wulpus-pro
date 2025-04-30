# Receive data from a device which sends data via UDP

import socket
import json
import time

# Send message to first device in devices.json via TCP
with open('devices.json') as json_file:
    devices = json.load(json_file)['devices']

# Create a TCP/IP socket
print(f'Sending message to {devices[0]["ip"]}:{devices[0]["port"]}')
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect((devices[0]['ip'], devices[0]['port']))

# Create a UDP socket
sock_udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock_udp.settimeout(1)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, int(20*1e8))
sock_udp.bind(("", devices[0]['port']))

# Send message to start UDP stream
sock.sendall('start udp 1000 10000 0'.encode())
sock.close()
print('Sent message to start UDP stream')


total_bytes = 0
start_time = time.time()

while True:
    try:
        data, addr = sock_udp.recvfrom(16384)
    except socket.timeout:
        break

    end_time = time.time()

    total_bytes += len(data)
    # print('Received {} bytes from {}'.format(len(data), addr))

time_taken = end_time - start_time

print('Total bytes received: {}'.format(total_bytes))
print('Time taken: {} seconds'.format(time_taken))

print('Throughput: {} mbps'.format((total_bytes * 8) / (time_taken * 1000000)))
