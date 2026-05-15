#!/usr/bin/env python3
import argparse
import os
import socket
import struct
import sys
import time


OP_RRQ = 1
OP_WRQ = 2
OP_DATA = 3
OP_ACK = 4
OP_ERROR = 5

BLOCK_SIZE = 512


class TFTPError(Exception):
    pass


def build_wrq(remote_name: str) -> bytes:
    return (
        struct.pack("!H", OP_WRQ)
        + remote_name.encode("ascii")
        + b"\0"
        + b"octet"
        + b"\0"
    )


def build_data(block: int, payload: bytes) -> bytes:
    return struct.pack("!HH", OP_DATA, block & 0xFFFF) + payload


def parse_packet(packet: bytes):
    if len(packet) < 4:
        raise TFTPError("short packet received")

    opcode = struct.unpack("!H", packet[:2])[0]

    if opcode == OP_ACK:
        block = struct.unpack("!H", packet[2:4])[0]
        return ("ack", block, None)

    if opcode == OP_ERROR:
        code = struct.unpack("!H", packet[2:4])[0]
        message = packet[4:].split(b"\0", 1)[0].decode("ascii", errors="replace")
        return ("error", code, message)

    return ("other", opcode, None)


def emit_progress(percent: int, message: str, zenity: bool):
    percent = max(0, min(100, percent))

    if zenity:
        print(percent, flush=True)
        print(f"# {message}", flush=True)
    else:
        print(f"{percent:3d}%  {message}", flush=True)


def send_wrq(sock, host: str, port: int, remote_name: str, timeout: float, retries: int):
    wrq = build_wrq(remote_name)
    server_addr = (host, port)

    for attempt in range(retries + 1):
        sock.sendto(wrq, server_addr)

        try:
            packet, addr = sock.recvfrom(516)
        except socket.timeout:
            continue

        kind, value, message = parse_packet(packet)

        if kind == "error":
            raise TFTPError(f"TFTP error {value}: {message}")

        if kind == "ack" and value == 0:
            return addr

        raise TFTPError(f"unexpected response to WRQ: {kind} {value}")

    raise TFTPError("timeout waiting for WRQ ACK")


def wait_ack(sock, expected_block: int, server_addr, last_packet: bytes, retries: int):
    for attempt in range(retries + 1):
        try:
            packet, addr = sock.recvfrom(516)
        except socket.timeout:
            sock.sendto(last_packet, server_addr)
            continue

        if addr != server_addr:
            continue

        kind, value, message = parse_packet(packet)

        if kind == "error":
            raise TFTPError(f"TFTP error {value}: {message}")

        if kind == "ack":
            if value == (expected_block & 0xFFFF):
                return

            # Duplicate ACK: resend current packet.
            sock.sendto(last_packet, server_addr)
            continue

        raise TFTPError(f"unexpected packet while waiting for ACK: {kind} {value}")

    raise TFTPError(f"timeout waiting for ACK block {expected_block}")


def upload_file(
    host: str,
    port: int,
    local_path: str,
    remote_name: str,
    timeout: float,
    retries: int,
    zenity: bool,
):
    total_size = os.path.getsize(local_path)
    sent_bytes = 0
    last_percent = -1

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(timeout)

        emit_progress(0, "Opening TFTP transfer...", zenity)
        server_addr = send_wrq(sock, host, port, remote_name, timeout, retries)

        block = 1

        with open(local_path, "rb") as f:
            while True:
                payload = f.read(BLOCK_SIZE)
                packet = build_data(block, payload)

                sock.sendto(packet, server_addr)
                wait_ack(sock, block, server_addr, packet, retries)

                sent_bytes += len(payload)

                if total_size > 0:
                    percent = int(sent_bytes * 100 / total_size)
                else:
                    percent = 100

                if zenity and percent >= 100:
                    percent = 99

                if percent != last_percent:
                    emit_progress(
                        percent,
                        f"Uploaded {sent_bytes}/{total_size} bytes",
                        zenity,
                    )
                    last_percent = percent

                block += 1

                if len(payload) < BLOCK_SIZE:
                    break

        if zenity:
            try:
                print(100, flush=True)
            except BrokenPipeError:
                pass
        else:
            emit_progress(100, "Transfer complete.", zenity)


def main():
    parser = argparse.ArgumentParser(
        description="MiniJV880 TFTP PUT uploader with real progress."
    )
    parser.add_argument("host", help="MiniJV880 IP address")
    parser.add_argument("local_file", help="local file to upload")
    parser.add_argument("remote_name", help="remote TFTP filename")
    parser.add_argument("--port", type=int, default=69, help="TFTP server port")
    parser.add_argument("--timeout", type=float, default=5.0, help="ACK timeout in seconds")
    parser.add_argument("--retries", type=int, default=5, help="retry count per packet")
    parser.add_argument(
        "--zenity",
        action="store_true",
        help="emit zenity progress format on stdout",
    )

    args = parser.parse_args()

    if not os.path.isfile(args.local_file):
        print(f"ERROR: local file not found: {args.local_file}", file=sys.stderr)
        return 1

    try:
        upload_file(
            host=args.host,
            port=args.port,
            local_path=args.local_file,
            remote_name=args.remote_name,
            timeout=args.timeout,
            retries=args.retries,
            zenity=args.zenity,
        )
        return 0

    except TFTPError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    except OSError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main())
