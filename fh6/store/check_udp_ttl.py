#!/usr/bin/env python3
"""Verify IPv4 TTL on two loopback packets emitted by the isolated FH6 Wine probe."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import socket
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'rosetta/fg-chain'))
from run_native_ui_probe import sha, store_test_runtime


def receive(sock):
    seen = {}
    for _ in range(2):
        data, ancillary, _, _ = sock.recvmsg(32, socket.CMSG_SPACE(1))
        seen[data] = [value[0] for level, kind, value in ancillary
                      if (level, kind) == (socket.IPPROTO_IP, socket.IP_RECVTTL) and len(value) == 1]
    return seen


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('prefix', type=Path, help='existing isolated Store renderer test prefix in /private/tmp')
    parser.add_argument('probe', type=Path, help='locally built x64 fh6/arm64/network/probe.c executable')
    args = parser.parse_args()
    prefix = args.prefix.resolve(strict=True)
    if not prefix.is_relative_to(Path('/private/tmp')):
        parser.error('Use a separate prefix under /private/tmp')
    wine, env, _ = store_test_runtime(prefix)
    live = Path.home() / 'Library/Application Support/MACxus/external-highball/fh6-prior-fixes/system32/ws2_32.dll'
    if sha(prefix / 'drive_c/windows/system32/ws2_32.dll') != sha(live):
        parser.error('The isolated prefix must contain the installed Store ws2_32 build')
    env.update(WINEDEBUG='-all', WINEDLLOVERRIDES='winemenubuilder.exe=d;ws2_32=n,b')
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock, ThreadPoolExecutor(max_workers=1) as pool:
        sock.bind(('127.0.0.1', 0))
        sock.settimeout(15)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_RECVTTL, 1)
        packets = pool.submit(receive, sock)
        subprocess.run([wine, args.probe.resolve(strict=True), '--ttl-wire', str(sock.getsockname()[1])],
                       env=env, check=True, timeout=20)
        if packets.result() != {b'base': [37], b'check': [255]}:
            raise RuntimeError('IPv4 loopback did not receive the expected TTL change from 37 to 255')
    print('PASS Darwin received IPv4 TTL 37 then 255 through the installed Store ws2_32 logic')


if __name__ == '__main__':
    main()
