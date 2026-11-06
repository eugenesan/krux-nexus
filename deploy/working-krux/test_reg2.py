import socket
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
path = "/data/krux/camd.sock"
addr = bytes([1, 0] + list(path.encode()) + [0] * (108 - len(path)))
sock.connect(addr)
sock.settimeout(5)
sock.sendall(b"W" + bytes([0x02, 0x02, 0x09]))
resp = sock.recv(2)
print("response:", resp)
sock.close()
print("done")
