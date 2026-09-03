# ChargeHub 交付验收记录

本文件只记录可复现的验证，不包含真实数据库、服务器地址、密钥、token或完整手机号。

## 自动化验证

在Ubuntu 24.04源码目录执行：

```bash
python3 -m unittest discover -s tests -p 'test*.py' -v
npm --prefix dashboard test

mkdir -p /tmp/chargehubbuild
cd /tmp/chargehubbuild
qmake /path/to/ChargeHub/ChargeHub.pro
make -j2
QT_QPA_PLATFORM=offscreen ./tests/wallettest -txt
```

30分钟稳定性检查使用临时数据库，不触碰联调库：

```bash
cd /path/to/ChargeHub
python3 tests/soaktest.py --duration-seconds 1800 --interval-seconds 5
```

通过条件：退出码为0，`failures`、`slowResponses`均为0，内存增长不超过50 MiB。

## 本分支验收结果

验收日期：2026-09-04。验证对象：`cbh/webdashboardcoremetrics`。

| 验收项 | 环境与结果 |
| --- | --- |
| Ubuntu完整构建 | Ubuntu 24.04.4、Qt 5.15.13、GCC 13.2.0；总工程`qmake`与`make -j2`通过 |
| Python回归 | `python3 -m unittest discover -s tests -p 'test*.py' -v`全部通过 |
| Web回归 | `npm --prefix dashboard test`：20项全部通过 |
| Qt钱包回归 | `QT_QPA_PLATFORM=offscreen ./tests/wallettest -txt`：20项全部通过 |
| 大屏稳定性 | 30分钟、每5秒一次真实路由检查通过；无请求失败、慢响应或超限内存增长 |
| 浏览器检查 | 1920×1080、1366×768、760×900均无横向溢出，四个图表可见且控制台无错误 |
| 降级与演示 | API不可用时不展示伪造指标；`?demo=1`明确标记“演示数据” |

上述结果来自隔离临时数据库；未读取或修改小组联调库。人工联调项需要在管理端、用户端和分析端合并到同一版本后由小组共同勾选。

## 人工联调清单

- [ ] 只启动一份管理端，用户端连接其底栏显示的地址；
- [ ] 登录后余额来自`QUERY_WALLET`，断开服务器时不显示假0；
- [ ] 分别充值0.01元和10000.00元，余额及充值流水一致；
- [ ] 提交后快速点击按钮和按回车，不产生重复流水；
- [ ] 模拟超时后查询原请求，再由用户主动选择是否新建充值；
- [ ] 冻结用户、存储失败、协议缺字段时余额不变化；
- [ ] 浏览器检查1920×1080、1366×768和窄窗口，无横向溢出；
- [ ] 暂停Flask后保留最后可信快照并显示“数据已过期”；
- [ ] 移除预测表时只降级负荷预测区域；
- [ ] `?demo=1`显示“演示数据”，所有图表可离线展示。

联调完成后，由执行者在本文件勾选对应项目并创建独立Git commit。
