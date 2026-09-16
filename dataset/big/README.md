# 扩样后的模拟生产数据集

由 `bigdata/expand_dataset.py` 根据 `dataset/04.数据集最终版/` 生成，给 Hadoop / PySpark 用。

| 文件 | 默认规模 | 内容 |
|------|----------|------|
| `sessions.csv` | 22 万行 | 充电会话（含 3395 条真实记录 + 加噪声复制） |
| `stations.csv` | 105 行 | 电站维表 |
| `telemetry.csv` | 8 万行 | 电池包遥测 |

重新生成：

```bash
cd ChargeHub/bigdata
python3 expand_dataset.py
```
