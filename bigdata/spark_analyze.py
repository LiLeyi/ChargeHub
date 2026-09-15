# -*- coding: utf-8 -*-
"""ChargeHub PySpark 作业：读扩样 CSV（HDFS 或 file://），多模型挖掘后只写 JSON。

算法：GBT / 随机森林 / 线性回归、KMeans+PCA、IsolationForest、
用户 RFM、晚高峰逻辑回归、FP-Growth、电池风险。
不改 charge_order / user.balance / pile.status。
"""
from __future__ import annotations

import json
import os
import sys
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from paths import big_dir, hdfs_uri, input_uri, out_dir  # noqa: E402


def _spark():
    from pyspark.sql import SparkSession

    builder = (
        SparkSession.builder.appName("ChargeHubBigData")
        .config("spark.sql.session.timeZone", "Asia/Shanghai")
        .config("spark.sql.shuffle.partitions", os.environ.get("CHARGEHUB_SHUFFLE", "8"))
        .config("spark.driver.memory", os.environ.get("CHARGEHUB_DRIVER_MEM", "2g"))
    )
    master = os.environ.get("SPARK_MASTER", "local[*]")
    builder = builder.master(master)
    spark = builder.getOrCreate()
    spark.sparkContext.setLogLevel("WARN")
    return spark


def _try_hdfs_ls(spark, uri: str) -> bool:
    try:
        jvm = spark._jvm
        conf = spark._jsc.hadoopConfiguration()
        path = jvm.org.apache.hadoop.fs.Path(uri)
        fs = path.getFileSystem(conf)
        return fs.exists(path)
    except Exception:
        return False


def resolve_input(spark) -> tuple[str, str]:
    """返回 (sessions_uri, engine_label)。HDFS 有数据用 HDFS，否则本地 file://。"""
    local = big_dir()
    local_s = (local / "sessions.csv").as_posix()
    hdfs_base = input_uri()
    hdfs_s = hdfs_base.rstrip("/") + "/sessions.csv"
    if hdfs_s.startswith("hdfs://") and _try_hdfs_ls(spark, hdfs_s):
        return hdfs_base.rstrip("/"), "HDFS"
    if (local / "sessions.csv").exists():
        return "file:///" + local_s.rsplit("/", 1)[0], "LOCAL"
    raise FileNotFoundError(
        "找不到 sessions.csv。请先 python3 expand_dataset.py，再可选 bash scripts/ingest_hdfs.sh"
    )


def weekday_index(name):
    from pyspark.sql.functions import when, col

    n = name
    return (
        when(n == "Mon", 1)
        .when(n == "Tues", 2)
        .when(n == "Tue", 2)
        .when(n == "Wed", 3)
        .when(n == "Thurs", 4)
        .when(n == "Thu", 4)
        .when(n == "Fri", 5)
        .when(n == "Sat", 6)
        .when(n == "Sun", 7)
        .otherwise(0)
    )


def train_models(spark, sessions):
    from pyspark.ml import Pipeline
    from pyspark.ml.evaluation import RegressionEvaluator
    from pyspark.ml.feature import StringIndexer, VectorAssembler
    from pyspark.ml.regression import GBTRegressor, LinearRegression
    from pyspark.sql.functions import col

    feat = sessions.select(
        col("kwhTotal").cast("double").alias("label"),
        col("startTime").cast("int").alias("hour"),
        weekday_index(col("weekday")).alias("wd"),
        col("chargeTimeHrs").cast("double").alias("hrs"),
        col("stationId").cast("string").alias("stationId"),
        col("platform").cast("string").alias("platform"),
        col("managerVehicle").cast("int").alias("fleet"),
        col("facilityType").cast("int").alias("facility"),
    ).na.drop()

    train, test = feat.randomSplit([0.8, 0.2], seed=20260912)
    indexers = [
        StringIndexer(inputCol="stationId", outputCol="stationIdx", handleInvalid="keep"),
        StringIndexer(inputCol="platform", outputCol="platIdx", handleInvalid="keep"),
    ]
    assemble = VectorAssembler(
        inputCols=["hour", "wd", "hrs", "stationIdx", "platIdx", "fleet", "facility"],
        outputCol="features",
        handleInvalid="skip",
    )
    from pyspark.ml.regression import RandomForestRegressor

    gbt = GBTRegressor(
        featuresCol="features",
        labelCol="label",
        maxDepth=5,
        maxIter=20,
        maxBins=128,
        seed=20260912,
    )
    rf = RandomForestRegressor(
        featuresCol="features",
        labelCol="label",
        numTrees=20,
        maxDepth=6,
        maxBins=128,
        seed=20260912,
    )
    lr = LinearRegression(featuresCol="features", labelCol="label", maxIter=40, regParam=0.08)
    stages = indexers + [assemble]
    gbt_model = Pipeline(stages=stages + [gbt]).fit(train)
    rf_model = Pipeline(stages=stages + [rf]).fit(train)
    lr_model = Pipeline(stages=stages + [lr]).fit(train)
    ev = RegressionEvaluator(labelCol="label", predictionCol="prediction")

    def scores(model, name):
        p = model.transform(test)
        return {
            "name": name,
            "mae": round(float(ev.evaluate(p, {ev.metricName: "mae"})), 4),
            "rmse": round(float(ev.evaluate(p, {ev.metricName: "rmse"})), 4),
            "r2": round(float(ev.evaluate(p, {ev.metricName: "r2"})), 4),
        }

    gbt_s, rf_s, lr_s = scores(gbt_model, "GBT"), scores(rf_model, "随机森林"), scores(lr_model, "线性回归")
    names = ["小时", "星期", "时长", "电站", "平台", "车队", "设施"]
    raw_imp = list(rf_model.stages[-1].featureImportances)
    importances = [
        {"name": names[i] if i < len(names) else f"f{i}", "value": round(float(v), 4)}
        for i, v in enumerate(raw_imp)
    ]
    importances.sort(key=lambda x: -x["value"])
    metrics = {
        "mae": gbt_s["mae"],
        "rmse": gbt_s["rmse"],
        "r2": gbt_s["r2"],
        "test_n": test.count(),
        "train_n": train.count(),
        "baseline": "LinearRegression",
        "primary": "GBTRegressor",
        "lr_mae": lr_s["mae"],
        "lr_rmse": lr_s["rmse"],
        "rf_mae": rf_s["mae"],
        "rf_rmse": rf_s["rmse"],
        "models": [gbt_s, rf_s, lr_s],
        "importances": importances,
    }
    return gbt_model, metrics


def cluster_stations(spark, sessions, stations):
    from pyspark.ml.clustering import KMeans
    from pyspark.ml.feature import PCA, StandardScaler, VectorAssembler
    from pyspark.sql.functions import avg, col, count

    feat = (
        sessions.groupBy("stationId")
        .agg(
            count("*").alias("sessions"),
            avg(col("kwhTotal").cast("double")).alias("avg_kwh"),
            avg(col("chargeTimeHrs").cast("double")).alias("avg_hrs"),
            avg(col("charging_fees").cast("double")).alias("avg_fee"),
            avg(col("startTime").cast("double")).alias("avg_hour"),
        )
        .na.fill(0)
    )
    n = feat.count()
    k = 4 if n >= 8 else max(2, min(3, n))
    assembled = VectorAssembler(
        inputCols=["sessions", "avg_kwh", "avg_hrs", "avg_fee", "avg_hour"],
        outputCol="raw",
    ).transform(feat)
    scaled = StandardScaler(inputCol="raw", outputCol="features", withMean=True, withStd=True).fit(assembled).transform(assembled)
    model = KMeans(k=k, seed=20260912, featuresCol="features", predictionCol="cluster").fit(scaled)
    labeled = model.transform(scaled)
    labeled = PCA(k=2, inputCol="features", outputCol="pca").fit(labeled).transform(labeled)
    names = stations.select(
        col("stationId").cast("string").alias("stationId"),
        col("station_name").alias("name"),
        col("address"),
        col("device_count").cast("int").alias("device_count"),
    )
    joined = labeled.join(names, "stationId", "left")
    cluster_avg = (
        joined.groupBy("cluster")
        .agg(avg("avg_hour").alias("h"), avg("sessions").alias("n"), avg("avg_kwh").alias("kwh"))
        .collect()
    )
    labels = {}
    for r in cluster_avg:
        if r["h"] is not None and r["h"] >= 17:
            labels[int(r["cluster"])] = "晚高峰型"
        elif r["n"] is not None and r["n"] < 400:
            labels[int(r["cluster"])] = "低频型"
        elif r["kwh"] is not None and r["kwh"] < 8:
            labels[int(r["cluster"])] = "慢充短时型"
        else:
            labels[int(r["cluster"])] = "均衡高负荷型"
    rows = []
    for r in joined.orderBy(col("sessions").desc()).limit(80).collect():
        cid = int(r["cluster"])
        pca = list(r["pca"]) if r["pca"] is not None else [0.0, 0.0]
        rows.append(
            {
                "stationId": r["stationId"],
                "name": r["name"] or f"站{r['stationId']}",
                "address": r["address"] or "",
                "cluster": cid,
                "label": labels.get(cid, f"簇{cid}"),
                "sessions": int(r["sessions"] or 0),
                "avg_kwh": round(float(r["avg_kwh"] or 0), 3),
                "avg_hrs": round(float(r["avg_hrs"] or 0), 3),
                "avg_fee": round(float(r["avg_fee"] or 0), 3),
                "avg_hour": round(float(r["avg_hour"] or 0), 2),
                "device_count": int(r["device_count"] or 0),
                "pca_x": round(float(pca[0]), 3),
                "pca_y": round(float(pca[1] if len(pca) > 1 else 0), 3),
            }
        )
    return rows, labels, k


def battery_risks(spark, telemetry, sessions, stations):
    from pyspark.sql.functions import abs as spark_abs, col, desc

    t = telemetry.select(
        col("esd").cast("string").alias("sessionId"),
        col("soc").cast("double").alias("soc"),
        col("`pack_voltage (V)`").cast("double").alias("pack_v"),
        spark_abs(col("`charge_current (A)`").cast("double")).alias("amps"),
        col("`max_cell_voltage (V)`").cast("double").alias("max_cell"),
        col("`min_cell_voltage (V)`").cast("double").alias("min_cell"),
        col("`max_temperature (℃)`").cast("double").alias("tmax"),
        col("`min_temperature (℃)`").cast("double").alias("tmin"),
    ).na.fill(0)
    scored = t.select(
        "*",
        (col("max_cell") - col("min_cell")).alias("v_gap"),
        (col("tmax") - col("tmin")).alias("t_gap"),
    ).withColumn(
        "score",
        (
            (col("tmax") / 70.0) * 0.28
            + (col("v_gap") / 0.08) * 0.30
            + (col("amps") / 80.0) * 0.18
            + ((100.0 - col("soc")) / 100.0) * 0.14
            + (col("t_gap") / 12.0) * 0.10
        ),
    )
    meta = sessions.select(
        col("sessionId").cast("string").alias("sessionId"),
        col("stationId").cast("string").alias("stationId"),
    )
    names = stations.select(
        col("stationId").cast("string").alias("stationId"),
        col("station_name").alias("station"),
    )
    joined = scored.join(meta, "sessionId", "left").join(names, "stationId", "left")
    rows = []
    for r in joined.orderBy(desc("score")).limit(40).collect():
        score = max(0.0, min(0.99, float(r["score"] or 0)))
        level = "高风险" if score >= 0.72 else ("需关注" if score >= 0.45 else "正常")
        reasons = []
        if float(r["tmax"] or 0) >= 45:
            reasons.append(f"电芯最高温 {r['tmax']:.0f}℃")
        if float(r["v_gap"] or 0) >= 0.04:
            reasons.append(f"单体压差 {r['v_gap']:.3f}V")
        if float(r["amps"] or 0) >= 40:
            reasons.append(f"充电电流 {r['amps']:.1f}A")
        if float(r["soc"] or 100) <= 20:
            reasons.append(f"SOC {r['soc']:.1f}%")
        rows.append(
            {
                "sessionId": r["sessionId"],
                "station": r["station"] or r["stationId"] or "-",
                "score": round(score, 3),
                "level": level,
                "soc": round(float(r["soc"] or 0), 2),
                "tmax": round(float(r["tmax"] or 0), 1),
                "v_gap": round(float(r["v_gap"] or 0), 4),
                "reason": "；".join(reasons) or "运行指标在阈值内",
            }
        )
    return rows


def hourly_and_forecast(spark, sessions, gbt_model):
    from pyspark.sql.functions import avg, col, count, sum as spark_sum

    hourly = (
        sessions.groupBy(col("startTime").cast("int").alias("hour"))
        .agg(
            spark_sum(col("kwhTotal").cast("double")).alias("hist_kwh"),
            count("*").alias("n"),
            avg(col("kwhTotal").cast("double")).alias("avg_kwh"),
        )
        .orderBy("hour")
        .collect()
    )
    by_hour = {int(r["hour"] or 0): r for r in hourly if r["hour"] is not None}

    # 用 GBT 对「典型一小时」做点预测，再按历史单量放大成负荷
    grid = []
    for h in range(24):
        grid.append((h, 1 if h < 6 else (7 if h == 0 else (h % 7) + 1), 1.6, "0", "android", 0, 3))
    from pyspark.sql.types import DoubleType, IntegerType, StringType, StructField, StructType

    schema = StructType(
        [
            StructField("hour", IntegerType()),
            StructField("wd", IntegerType()),
            StructField("hrs", DoubleType()),
            StructField("stationId", StringType()),
            StructField("platform", StringType()),
            StructField("fleet", IntegerType()),
            StructField("facility", IntegerType()),
        ]
    )
    # 用真实站的高频站做代表点，避免 indexer 全是 unseen
    top_station = (
        sessions.groupBy("stationId").count().orderBy(col("count").desc()).limit(1).collect()
    )
    sid = str(top_station[0]["stationId"]) if top_station else "0"
    grid_rows = [(h, (h % 7) + 1, 1.6, sid, "android", 0, 3) for h in range(24)]
    gdf = spark.createDataFrame(grid_rows, schema)
    pred = {int(r["hour"]): float(r["prediction"]) for r in gbt_model.transform(gdf).collect()}

    curve = []
    hist_curve = []
    for h in range(24):
        row = by_hour.get(h)
        hist = float(row["hist_kwh"]) if row else 0.0
        n = float(row["n"]) if row else 0.0
        unit = pred.get(h, float(row["avg_kwh"]) if row else 6.0)
        # 把全量样本折成「典型一天」：按出现天数近似
        typical = hist / max(90.0, n / max(1.0, (n / 24.0))) if n else unit * 8
        if row:
            typical = hist / 90.0
        pred_kwh = max(0.0, 0.55 * typical + 0.45 * unit * max(8.0, n / 90.0))
        curve.append(round(pred_kwh, 3))
        hist_curve.append(round(hist / 90.0, 3))

    now_h = datetime.now().hour
    hourly_out = []
    for h in range(24):
        hourly_out.append(
            {
                "hour": h,
                "pred_kwh": curve[h],
                "hist_kwh": hist_curve[h],
                "name": "全网扩样",
            }
        )

    def win_sum(length: int) -> float:
        return sum(curve[(now_h + i) % 24] for i in range(length))

    peak_h = max(range(24), key=lambda i: curve[i])
    peak = f"{peak_h:02d}:00-{(peak_h + 1) % 24:02d}:00"
    forecasts = []
    for hz in (1, 6, 24):
        pred_v = win_sum(hz)
        forecasts.append(
            {
                "name": "全网（Spark）",
                "horizon_hours": hz,
                "pred_kwh": round(pred_v, 2),
                "pred_idle": max(0, 18 - int(pred_v / 40)),
                "peak_hour": peak,
            }
        )
    return hourly_out, forecasts, curve, peak, now_h


def peak_classifier(spark, sessions):
    from pyspark.ml import Pipeline
    from pyspark.ml.classification import LogisticRegression
    from pyspark.ml.evaluation import BinaryClassificationEvaluator, MulticlassClassificationEvaluator
    from pyspark.ml.feature import StringIndexer, VectorAssembler
    from pyspark.sql.functions import col, when

    feat = sessions.select(
        when(col("startTime").cast("int").between(17, 21), 1).otherwise(0).alias("label"),
        col("chargeTimeHrs").cast("double").alias("hrs"),
        col("kwhTotal").cast("double").alias("kwh"),
        col("charging_fees").cast("double").alias("fee"),
        weekday_index(col("weekday")).alias("wd"),
        col("platform").cast("string").alias("platform"),
        col("facilityType").cast("int").alias("facility"),
    ).na.drop()
    train, test = feat.randomSplit([0.8, 0.2], seed=20260912)
    pipe = Pipeline(
        stages=[
            StringIndexer(inputCol="platform", outputCol="platIdx", handleInvalid="keep"),
            VectorAssembler(inputCols=["hrs", "kwh", "fee", "wd", "platIdx", "facility"], outputCol="features"),
            LogisticRegression(featuresCol="features", labelCol="label", maxIter=30),
        ]
    ).fit(train)
    pred = pipe.transform(test)
    auc = BinaryClassificationEvaluator(labelCol="label").evaluate(pred)
    acc = MulticlassClassificationEvaluator(labelCol="label", metricName="accuracy").evaluate(pred)
    peak_n = feat.filter(col("label") == 1).count()
    return {
        "auc": round(float(auc), 4),
        "accuracy": round(float(acc), 4),
        "peak_ratio": round(peak_n / max(1, feat.count()), 4),
        "peak_n": int(peak_n),
    }


def isolation_anomalies(spark, sessions):
    from pyspark.ml.feature import VectorAssembler
    from pyspark.sql.functions import col, count

    feat = sessions.select(
        col("sessionId").cast("string").alias("sessionId"),
        col("stationId").cast("string").alias("stationId"),
        col("startTime").cast("int").alias("hour"),
        col("kwhTotal").cast("double").alias("kwh"),
        col("chargeTimeHrs").cast("double").alias("hrs"),
        col("charging_fees").cast("double").alias("fee"),
    ).na.drop()
    assembled = VectorAssembler(inputCols=["hour", "kwh", "hrs", "fee"], outputCol="features").transform(feat)
    sample = assembled.sample(False, 0.25, 20260912)
    try:
        from pyspark.ml.anomaly import IsolationForest

        model = IsolationForest(contamination=0.03, numEstimators=40, maxFeatures=1.0, seed=20260912).fit(sample)
        scored = model.transform(sample)
        flagged = scored.filter(col("prediction") == 1)
    except Exception:
        from pyspark.sql.functions import expr

        stats = sample.selectExpr(
            "percentile_approx(kwh, 0.99) as k99",
            "percentile_approx(hrs, 0.99) as h99",
        ).first()
        flagged = sample.filter((col("kwh") > float(stats["k99"])) | (col("hrs") > float(stats["h99"])))
    rows = []
    for r in flagged.orderBy(col("kwh").desc()).limit(24).collect():
        rows.append(
            {
                "sessionId": r["sessionId"],
                "hour": int(r["hour"] or 0),
                "kwh": round(float(r["kwh"] or 0), 2),
                "hrs": round(float(r["hrs"] or 0), 2),
                "fee": round(float(r["fee"] or 0), 2),
            }
        )
    by_hour = (
        flagged.groupBy("hour").agg(count("*").alias("n")).orderBy("hour").collect()
    )
    hourly = [{"hour": int(r["hour"] or 0), "n": int(r["n"])} for r in by_hour]
    return {"count": flagged.count(), "items": rows, "hourly": hourly}


def user_rfm(spark, sessions):
    from pyspark.ml.clustering import KMeans
    from pyspark.ml.feature import StandardScaler, VectorAssembler
    from pyspark.sql.functions import avg, col, count, datediff, lit, max as spark_max, min as spark_min, sum as spark_sum, to_timestamp

    u = (
        sessions.select(
            col("userId").cast("string").alias("userId"),
            to_timestamp(col("created")).alias("ts"),
            col("charging_fees").cast("double").alias("fee"),
            col("kwhTotal").cast("double").alias("kwh"),
        )
        .na.drop()
        .groupBy("userId")
        .agg(
            count("*").alias("freq"),
            spark_sum("fee").alias("monetary"),
            spark_max("ts").alias("last_ts"),
            spark_min("ts").alias("first_ts"),
            spark_sum("kwh").alias("kwh"),
        )
    )
    end = u.agg(spark_max("last_ts").alias("mx")).first()["mx"]
    feat = u.withColumn("recency", datediff(lit(end), col("last_ts")).cast("double")).na.fill(0)
    assembled = VectorAssembler(inputCols=["recency", "freq", "monetary"], outputCol="raw").transform(feat)
    scaled = StandardScaler(inputCol="raw", outputCol="features", withMean=True, withStd=True).fit(assembled).transform(assembled)
    k = 3 if feat.count() >= 9 else 2
    labeled = KMeans(k=k, seed=20260912, featuresCol="features").fit(scaled).transform(scaled)
    stats = (
        labeled.groupBy("prediction")
        .agg(avg("freq").alias("f"), avg("monetary").alias("m"), avg("recency").alias("r"), count("*").alias("n"))
        .collect()
    )
    tags = {}
    if stats:
        tags[int(max(stats, key=lambda r: (r["m"] or 0) * (r["f"] or 0))["prediction"])] = "高价值"
        tags[int(max(stats, key=lambda r: r["r"] or 0)["prediction"])] = "沉默"
    for r in stats:
        cid = int(r["prediction"])
        tags.setdefault(cid, "常规")
    mix = []
    for r in stats:
        mix.append(
            {
                "name": tags.get(int(r["prediction"]), "常规"),
                "value": int(r["n"]),
                "avg_fee": round(float(r["m"] or 0), 1),
                "avg_freq": round(float(r["f"] or 0), 1),
            }
        )
    mix.sort(key=lambda x: -x["value"])
    return mix


def assoc_rules(spark, sessions):
    from pyspark.ml.fpm import FPGrowth
    from pyspark.sql.functions import array, col, lit, when

    items = sessions.select(
        array(
            col("weekday").cast("string"),
            when(col("startTime").cast("int").between(17, 21), lit("晚高峰"))
            .when(col("startTime").cast("int").between(7, 9), lit("早高峰"))
            .otherwise(lit("平峰")),
            col("platform").cast("string"),
        ).alias("items")
    ).na.drop()
    model = FPGrowth(itemsCol="items", minSupport=0.04, minConfidence=0.35).fit(items)
    rules = []
    for r in model.associationRules.orderBy(col("confidence").desc()).limit(8).collect():
        rules.append(
            {
                "ante": " · ".join(list(r["antecedent"])),
                "cons": " · ".join(list(r["consequent"])),
                "confidence": round(float(r["confidence"]), 3),
                "support": round(float(r["support"]), 3),
            }
        )
    return rules


def weekday_hour(spark, sessions):
    from pyspark.sql.functions import col, sum as spark_sum

    rows = (
        sessions.groupBy(col("weekday"), col("startTime").cast("int").alias("hour"))
        .agg(spark_sum(col("kwhTotal").cast("double")).alias("kwh"))
        .collect()
    )
    order = ["Mon", "Tues", "Tue", "Wed", "Thurs", "Thu", "Fri", "Sat", "Sun"]
    canon = {"Tue": "Tues", "Thu": "Thurs"}
    out = []
    for r in rows:
        wd = canon.get(r["weekday"], r["weekday"] or "")
        if r["hour"] is None:
            continue
        out.append({"weekday": wd, "hour": int(r["hour"]), "kwh": round(float(r["kwh"] or 0), 2)})
    return out


def platform_mix(spark, sessions):
    from pyspark.sql.functions import col, count

    rows = sessions.groupBy(col("platform")).agg(count("*").alias("n")).collect()
    return [{"name": (r["platform"] or "unknown"), "value": int(r["n"])} for r in rows]


def station_rank(spark, sessions, stations):
    from pyspark.sql.functions import col, count, sum as spark_sum

    names = stations.select(
        col("stationId").cast("string").alias("stationId"),
        col("station_name").alias("name"),
    )
    rows = (
        sessions.groupBy("stationId")
        .agg(count("*").alias("n"), spark_sum(col("kwhTotal").cast("double")).alias("kwh"))
        .join(names, "stationId", "left")
        .orderBy(col("kwh").desc())
        .limit(12)
        .collect()
    )
    return [
        {
            "name": r["name"] or str(r["stationId"]),
            "kwh": round(float(r["kwh"] or 0), 1),
            "sessions": int(r["n"]),
        }
        for r in rows
    ]


def build_alerts(metrics, clusters, battery, curve, kpis, anomalies=None):
    alerts = []
    now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    peak = max(curve) if curve else 0
    if peak >= 80:
        alerts.append({"level": "严重", "title": f"高峰 {peak:.0f} kWh", "detail": "", "created_at": now})
    high_bat = [b for b in battery if b["level"] == "高风险"]
    if high_bat:
        alerts.append({"level": "严重", "title": f"电池 {len(high_bat)}", "detail": "", "created_at": now})
    n_ano = (anomalies or {}).get("count") or 0
    if n_ano:
        alerts.append({"level": "一般", "title": f"异常 {n_ano}", "detail": "", "created_at": now})
    rush = [c for c in clusters if c["label"] == "晚高峰型"]
    if rush:
        alerts.append({"level": "一般", "title": f"晚高峰站 {len(rush)}", "detail": "", "created_at": now})
    return alerts


def write_json(path: Path, obj) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, ensure_ascii=False, indent=2), encoding="utf-8")


def main() -> None:
    spark = _spark()
    base, engine = resolve_input(spark)
    print("INPUT", base, engine)
    sessions = (
        spark.read.option("header", True)
        .option("encoding", "UTF-8")
        .csv(base + "/sessions.csv")
    )
    stations = (
        spark.read.option("header", True)
        .option("encoding", "UTF-8")
        .csv(base + "/stations.csv")
    )
    telemetry = (
        spark.read.option("header", True)
        .option("encoding", "UTF-8")
        .csv(base + "/telemetry.csv")
    )

    gbt_model, metrics = train_models(spark, sessions)
    clusters, cluster_labels, k = cluster_stations(spark, sessions, stations)
    battery = battery_risks(spark, telemetry, sessions, stations)
    hourly, forecasts, curve, peak, now_h = hourly_and_forecast(spark, sessions, gbt_model)
    heat = weekday_hour(spark, sessions)
    platforms = platform_mix(spark, sessions)
    top = station_rank(spark, sessions, stations)
    peak_clf = peak_classifier(spark, sessions)
    anomalies = isolation_anomalies(spark, sessions)
    rfm = user_rfm(spark, sessions)
    rules = assoc_rules(spark, sessions)

    from pyspark.sql.functions import col, count, sum as spark_sum

    kwh_total = sessions.agg(spark_sum(col("kwhTotal").cast("double"))).first()[0] or 0.0
    fee_total = sessions.agg(spark_sum(col("charging_fees").cast("double"))).first()[0] or 0.0
    n_sess = sessions.count()
    n_sta = stations.count()
    kpis = {
        "sessions": int(n_sess),
        "stations": int(n_sta),
        "kwh_total": round(float(kwh_total), 2),
        "fees_total": round(float(fee_total), 2),
        "clusters": int(k),
        "engine": engine,
        "model": metrics["primary"],
        "anomalies": int(anomalies.get("count") or 0),
        "peak_hour": int(max(range(24), key=lambda i: curve[i]) if curve else 0),
        "peak_auc": peak_clf.get("auc"),
    }

    now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    report = {
        "model_version": "spark-ensemble-v2",
        "mae": metrics["mae"],
        "rmse": metrics["rmse"],
        "r2": metrics["r2"],
        "lr_mae": metrics["lr_mae"],
        "lr_rmse": metrics["lr_rmse"],
        "sample_n": int(n_sess),
        "train_n": metrics["train_n"],
        "test_n": metrics["test_n"],
        "weather": f"大数据引擎 {engine}",
        "created_at": now,
        "engine": engine,
        "primary": metrics["primary"],
        "baseline": metrics["baseline"],
        "rf_mae": metrics.get("rf_mae"),
        "rf_rmse": metrics.get("rf_rmse"),
        "peak_auc": peak_clf.get("auc"),
        "peak_acc": peak_clf.get("accuracy"),
    }
    alerts = build_alerts(metrics, clusters, battery, curve, kpis, anomalies)
    plan = []
    for i, c in enumerate(sorted(clusters, key=lambda x: x["sessions"], reverse=True)[:8], start=1):
        plan.append(
            {
                "station": c["name"],
                "recommend": c["avg_kwh"],
                "priority": i,
                "reason": f"{c['label']} · 会话 {c['sessions']} · 建议按簇调度值班",
                "created_at": now,
            }
        )

    dest = out_dir()
    dest.mkdir(parents=True, exist_ok=True)
    bundle = {
        "report": report,
        "kpis": kpis,
        "hourly": hourly,
        "forecasts": forecasts,
        "clusters": clusters,
        "battery": battery,
        "alerts": alerts,
        "plan": plan,
        "heatmap": heat,
        "platform": platforms,
        "top_stations": top,
        "cluster_labels": cluster_labels,
        "models": metrics.get("models") or [],
        "importances": metrics.get("importances") or [],
        "anomalies": anomalies,
        "rfm": rfm,
        "rules": rules,
        "peak_clf": peak_clf,
        "updated": now,
    }
    write_json(dest / "spark_report.json", bundle)
    write_json(dest / "report.json", report)
    write_json(dest / "hourly.json", hourly)
    write_json(dest / "clusters.json", clusters)
    write_json(dest / "battery.json", battery)
    print("SPARK_OK")
    print("out", dest / "spark_report.json")
    print("sessions", n_sess, "mae", metrics["mae"], "rmse", metrics["rmse"], "engine", engine)
    spark.stop()


if __name__ == "__main__":
    main()
