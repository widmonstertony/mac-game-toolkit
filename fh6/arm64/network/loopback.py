#!/usr/bin/env python3
"""Local WebSocket/HTTP peer for probe.c; no external service or credentials."""
import argparse
import base64
import hashlib
from pathlib import Path
import socketserver
import struct
import time


class Peer(socketserver.StreamRequestHandler):
    def handle(self):
        self.request.settimeout(6)
        while True:
            line = self.rfile.readline()
            if not line:
                return
            fields = line.split()
            if len(fields) != 3:
                # A reused WebSocket cannot serve another HTTP request.
                return
            headers = {}
            while (line := self.rfile.readline()) not in (b"\r\n", b"\n", b""):
                key, _, value = line.partition(b":")
                headers[key.lower()] = value.strip()
            if fields[1] != b"/ws":
                if fields[1] == b"/slow":
                    time.sleep(0.4)
                self.wfile.write(b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: keep-alive\r\n\r\nok")
                self.wfile.flush()
                continue
            key = headers[b"sec-websocket-key"]
            accept = base64.b64encode(hashlib.sha1(key + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
            self.wfile.write(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + b"\r\n\r\n")
            self.wfile.flush()
            self.websocket()
            return

    def websocket(self):
        while True:
            header = self.rfile.read(2)
            if len(header) != 2:
                return
            first, second = header
            size = second & 127
            if size == 126:
                size = struct.unpack("!H", self.rfile.read(2))[0]
            elif size == 127:
                return
            mask = self.rfile.read(4) if second & 128 else b""
            data = self.rfile.read(size)
            if len(data) != size:
                return
            if mask:
                data = bytes(x ^ mask[i % 4] for i, x in enumerate(data))
            opcode = first & 15
            if opcode == 8:
                self.wfile.write(b"\x88\x02\x03\xe8")
                self.wfile.flush()
                return
            if opcode != 1:
                return
            time.sleep(0.2)
            self.wfile.write(bytes([0x81, len(data)]) + data)
            self.wfile.flush()


class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--port-file", type=Path, required=True)
    args = parser.parse_args()
    with Server(("127.0.0.1", 0), Peer) as server:
        args.port_file.write_text(str(server.server_address[1]))
        server.serve_forever()
