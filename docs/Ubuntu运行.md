# 在 Ubuntu 22.04 上运行 ChargeHub

完整说明（含联调、网络基础、模块接口、数据库）见 **[README.md](../README.md)**。

```bash
sudo apt update
sudo apt install -y build-essential qtbase5-dev qt5-qmake libqt5sql5-sqlite python3-flask
bash /mnt/hgfs/ChargeHub/scripts/rebuild.sh    # 第一次或改代码后
bash /mnt/hgfs/ChargeHub/scripts/start.sh      # 只打开、不编译
```

本机用户端填 `127.0.0.1:8888`。连组里服务器填管理端窗口底栏的 `局域网IP:8888`，且其他人不要再开管理端。
