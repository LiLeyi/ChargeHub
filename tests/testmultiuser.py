# -*- coding: utf-8 -*-
"""多用户连管理端 TCP :8888 的冒烟测试。

帧格式与 common/protocol.cpp 完全一致：struct ">I" 大端长度 + UTF-8 JSON。
只发 LOGIN，验证演示号能同时登。不测充电，以免改库。
管理端必须已 listen；本脚本是客户端，不要再开第二份 adminserver。
"""
from __future__ import annotations

import json
import socket
import struct
import sys

HOST, PORT = "127.0.0.1", 8888


def packMessage(obj: dict) -> bytes:
    """与用户端 Protocol::pack 相同：4 字节大端长度 + Compact JSON。"""
    body = json.dumps(obj, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    return struct.pack(">I", len(body)) + body


def readMessage(sock: socket.socket) -> dict:
    """先收 4 字节长度，再收满正文并 json.loads。半包循环 recv。"""
    hdr = sock.recv(4)
    n = struct.unpack(">I", hdr)[0]
    buf = b""
    while len(buf) < n:
        buf += sock.recv(n - len(buf))
    return json.loads(buf.decode("utf-8"))


def login(phone: str, password: str = "123456") -> dict:
    """发一帧 LOGIN 后立刻关连接。token 会留在服务端 session，本测试不续用。"""
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
