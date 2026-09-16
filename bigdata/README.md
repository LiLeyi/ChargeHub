# ChargeHub 大数据与智能分析（Hadoop + PySpark）

作业里的运营大屏和「智能分析」现在接 **Hadoop HDFS + PySpark**。  
原始表在 `dataset/04.数据集最终版/`，扩样后的生产级 CSV 在 `dataset/big/`（约 22 万会话 + 8 万条电池遥测）。

| 步骤 | 做什么 | 谁跑 |
|------|--------|------|
| 1 | 装 OpenJDK 11、Hadoop 3.3.6、Spark 3.5.x | WSL `setup_hadoop_spark.sh` |
| 2 | 扩样 CSV | `python3 expand_dataset.py` |
| 3 | （可选）`start-dfs.sh` + 上传 HDFS | `start_hdfs.sh` / `ingest_hdfs.sh` |
| 4 | GBT 预测 + KMeans 聚类 + 电池风险 | `spark-submit spark_analyze.py` |
| 5 | 只写分析表 | `apply_results.py` |
| 6 | 大屏读 JSON + SQLite | `dashboard/app.py` |

HDFS 没起来时 Spark 自动改读 `file://dataset/big`，作业照样出图。  
**不改** `charge_order` / `user.balance` / `pile.status`。

## PyCharm

1. 打开文件夹 `ChargeHub`（或只打开 `ChargeHub/bigdata`）。
2. 解释器选 **WSL Ubuntu-22.04** 的 `/usr/bin/python3`（不要用 Windows 的 python）。
3. Run Configuration 环境变量（或先在 WSL 终端 `source ~/opt/chargehub-spark.env`）：

```
JAVA_HOME=/usr/lib/jvm/java-11-openjdk-amd64
HADOOP_HOME=/home/bit/opt/hadoop
SPARK_HOME=/home/bit/opt/spark
HADOOP_CONF_DIR=/home/bit/opt/hadoop/etc/hadoop
PYSPARK_PYTHON=python3
```

4. 工作目录：`ChargeHub/bigdata`。
5. 先跑 `expand_dataset.py`，再跑 `spark_analyze.py`（或 WSL 里 `spark-submit`）。

脚本自带 Spark 发行包里的 `pyspark`，一般不用 `pip install`。没有 `SPARK_HOME` 时才 `pip3 install -r requirements.txt`。

## 算法（答辩可讲）

- **ETL**：真实会话 + 按站/时段/电量分布加噪声扩样，写入 HDFS 或本地。
- **GBT / 随机森林 / 线性回归**：预测单次电量，对照 MAE/RMSE，并输出特征贡献。
- **KMeans + PCA**：电站画像（会话、电量、时长、费用、时段）。
- **IsolationForest**：会话异常（电量/时长/费用/小时）。
- **用户 RFM**：按近度、频次、金额分成高价值 / 常规 / 沉默。
- **逻辑回归**：是否晚高峰会话（AUC）。
- **FP-Growth**：星期 × 峰谷 × 平台 的频繁项。
- **24h 负荷**：历史分时电量折成典型日，再与 GBT 单次电量 × 到站频次合成。
- **KMeans（k=4）**：电站向量 = 会话数、均电量、均时长、均费用、均开始小时 → 晚高峰型 / 均衡高负荷 / 慢充短时 / 低频。
- **电池风险**：遥测温差、单体压差、电流、低 SOC 加权，输出高风险会话。

## 常用命令（WSL）

```bash
# 只做一次
bash /mnt/d/大三小学期/计算机软件实训/ChargeHub/bigdata/scripts/setup_hadoop_spark.sh
source ~/opt/chargehub-spark.env
bash /mnt/d/大三小学期/计算机软件实训/ChargeHub/bigdata/scripts/start_hdfs.sh

# 每次出新图
bash /mnt/d/大三小学期/计算机软件实训/ChargeHub/bigdata/scripts/run_pipeline.sh
```

输出：`bigdata/output/spark_report.json`。大屏 `GET /api/bigdata` 读它。
