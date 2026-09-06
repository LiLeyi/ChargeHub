# 怎么打开 ChargeHub

WSLg 若出现 `[WARN:COPY MODE]`，是 Windows 共享内存坏了，不是 ChargeHub 的问题。启动脚本会先挂上 `/mnt/shared_memory` 再开窗口。

只保留两个文件，分别双击即可。

| 双击这个 | 打开什么 | 登录 |
|----------|----------|------|
| `ChargeHub/scripts/打开运营后台.bat` | 管理端 | `admin` / `123456` |
| `ChargeHub/scripts/打开用户端.bat` | 用户端 | `13800138000` / `123456`，服务器 `127.0.0.1:8888`，先连接再登录 |

大屏：管理端登录后，左边 **运营决策大屏** → **打开 Web 大屏**。

改了代码：在 Ubuntu 里执行 `bash scripts/rebuild.sh`，再双击上面两个 bat。
