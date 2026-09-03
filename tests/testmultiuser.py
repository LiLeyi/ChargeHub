# -*- coding: utf-8 -*-
"""多用户连管理端 :8888 的冒烟测试。"""
from __future__ import annotations

import json
import socket
import struct
import sys

HOST, PORT = "127.0.0.1", 8888


def packMessage(obj: dict) -> bytes:
    body = json.dumps(obj, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    return struct.pack(">I", len(body)) + body


def readMessage(sock: socket.socket) -> dict:
    hdr = sock.recv(4)
    n = struct.unpack(">I", hdr)[0]
    buf = b""
    while len(buf) < n:
        buf += sock.recv(n - len(buf))
    return json.loads(buf.decode("utf-8"))


def login(phone: str, password: str = "123456") -> dict:
    sock = socket.create_connection((HOST, PORT), 5)
    sock.sendall(
        packMessage(
            {
                "type": "LOGIN",
                "seq": 1,
                "role": "user",
                "token": "",
                "data": {"phone": phone, "password": password},
            }
        )
    )
    resp = readMessage(sock)
    sock.close()
    return resp


def main() -> int:
    phones = ["13800138000", "13912345678", "18611112222"]
    ok = 0
    for phone in phones:
        resp = login(phone)
        print(phone, resp.get("code"), resp.get("message"))
        if resp.get("code") == 0:
            ok += 1
    print("ok", ok, "/", len(phones))
    return 0 if ok == len(phones) else 1


if __name__ == "__main__":
    sys.exit(main())
