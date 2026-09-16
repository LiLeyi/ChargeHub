# -*- coding: utf-8 -*-
from pathlib import Path
import os
import subprocess

os.environ["HOME"] = "/home/bit"
os.environ["USER"] = "bit"
src = Path("/mnt/d/大三小学期/计算机软件实训/ChargeHub/scripts/wslbuildlocal.sh")
dst = Path("/tmp/wslbuildlocal.sh")
dst.write_bytes(src.read_bytes().replace(b"\r\n", b"\n").replace(b"\r", b"\n"))
dst.chmod(0o755)
raise SystemExit(subprocess.call(["/bin/bash", str(dst)]))
