/** @file analyticsservice.cpp
 * @brief 只负责分析读模型与重算任务；不会修改用户余额、订单状态或电桩运行状态。
 */
#include "analyticsservice.h"
#include "database.h"
#include <algorithm>
#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QMap>
#include <QPair>
#include <QtMath>
static qint64 fenOf(double yuan) { return qRound(yuan * 100.0); }
static double money(double value) { return fenOf(value) / 100.0; }
static QString nowStr() { return QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"); }
AnalyticsService::AnalyticsService(Database *db) : db_(db) {}

QJsonObject AnalyticsService::salesSummary() const
{
    const QString today = QDate::currentDate().toString("yyyy-MM-dd");
    const QString month = QDate::currentDate().toString("yyyy-MM");
    auto sumWhere = [this](const QString &w, const QVariantList &a) {
        auto r = db_->one("SELECT IFNULL(SUM(amount),0) AS s FROM charge_order WHERE status='已完成' AND " + w, a);
        return money(r.value("s").toDouble());
    };
    QJsonArray trend;
    for (int i = 29; i >= 0; --i) {
        const QString d = QDate::currentDate().addDays(-i).toString("yyyy-MM-dd");
        trend.append(QJsonObject{{"date", d}, {"amount", sumWhere("substr(start_time,1,10)=?", {d})}});
    }
    return QJsonObject{
        {"today", sumWhere("substr(start_time,1,10)=?", {today})},
        {"month", sumWhere("substr(start_time,1,7)=?", {month})},
        {"total", sumWhere("1=1", {})},
        {"trend", trend},
    };
}

QJsonObject AnalyticsService::pileStatusStats() const
{
    const auto rows = db_->query("SELECT status, COUNT(*) AS n FROM pile GROUP BY status");
    QMap<QString, int> counts{{QString::fromUtf8("在用"), 0}, {QString::fromUtf8("闲置"), 0}, {QString::fromUtf8("故障"), 0}};
    int total = 0;
    for (const auto &r : rows) {
        counts[r.value("status").toString()] = r.value("n").toInt();
        total += r.value("n").toInt();
    }
    if (total == 0)
        total = 1;
    QJsonArray items;
    const QStringList order = {QString::fromUtf8("在用"), QString::fromUtf8("闲置"), QString::fromUtf8("故障")};
    double acc = 0;
    for (int i = 0; i < order.size(); ++i) {
        const int n = counts.value(order[i]);
        double pct = qRound(n * 1000.0 / total) / 10.0;
        if (i == order.size() - 1)
            pct = qRound((100.0 - acc) * 10) / 10.0;
        else
            acc += pct;
        items.append(QJsonObject{{"status", order[i]}, {"count", n}, {"percent", pct}});
    }
    return QJsonObject{{"total", total}, {"items", items}};
}

QJsonObject AnalyticsService::cockpit() const
{
    QJsonObject s = salesSummary();
    QJsonObject st = pileStatusStats();
    QJsonArray idleRank;
    for (const auto &station : db_->query("SELECT * FROM station")) {
        const auto piles = db_->query("SELECT status FROM pile WHERE station_id=?", {station.value("id")});
        int idle = 0;
        for (const auto &p : piles)
            if (p.value("status").toString() == QString::fromUtf8("闲置"))
                ++idle;
        idleRank.append(QJsonObject{
            {"name", station.value("name").toString()},
            {"idle", idle},
            {"total", piles.size()},
        });
    }
    s.insert("piles", st.value("items"));
    s.insert("idleRank", idleRank);
    return s;
}

QVector<QVariantMap> AnalyticsService::listForecasts() const
{
    return db_->query(
        "SELECT f.*, s.name FROM load_forecast f JOIN station s ON s.id=f.station_id "
        "ORDER BY f.station_id, f.horizon_hours");
}

QVector<QVariantMap> AnalyticsService::listHourlyLoad() const
{
    return db_->query(
        "SELECT h.hour, h.pred_kwh, s.name FROM hourly_load h "
        "JOIN station s ON s.id=h.station_id ORDER BY s.id, h.hour");
}

QVector<QVariantMap> AnalyticsService::listFaultRisks() const
{
    return db_->query("SELECT * FROM fault_risk ORDER BY score DESC");
}

QVector<QVariantMap> AnalyticsService::listAlerts() const
{
    return db_->query("SELECT * FROM analysis_alert ORDER BY id DESC LIMIT 20");
}

QVector<QVariantMap> AnalyticsService::listDispatchPlan() const
{
    return db_->query("SELECT * FROM dispatch_plan ORDER BY priority");
}

QVariantMap AnalyticsService::latestReport() const
{
    return db_->one("SELECT * FROM analysis_report ORDER BY id DESC LIMIT 1");
}

int AnalyticsService::refreshForecast()
{
    const QString t = nowStr();
    const int nowH = QDateTime::currentDateTime().time().hour();
    const int wd = QDateTime::currentDateTime().date().dayOfWeek();
    const bool weekend = wd >= 6;
    const QStringList weathers = {QString::fromUtf8("晴"), QString::fromUtf8("多云"), QString::fromUtf8("小雨")};
    const QString weather = weathers[QDate::currentDate().dayOfYear() % 3];
    const double wFactor = weather == QString::fromUtf8("小雨") ? 0.90 : (weekend ? 0.88 : 1.08);

    db_->execute("DELETE FROM hourly_load");
    db_->execute("DELETE FROM load_forecast");
    db_->execute("DELETE FROM fault_risk");
    db_->execute("DELETE FROM analysis_alert");
    db_->execute("DELETE FROM dispatch_plan");
    db_->execute("DELETE FROM analysis_report");

    const auto hist = db_->query(
        "SELECT p.station_id AS sid, CAST(substr(o.start_time,12,2) AS INTEGER) AS hh, "
        "AVG(o.energy_kwh) AS a, COUNT(*) AS n FROM charge_order o "
        "JOIN pile p ON p.id=o.pile_id WHERE o.status='已完成' GROUP BY sid, hh");
    QHash<int, QVector<double>> hourMean;
    int sampleN = 0;
    for (const auto &r : hist) {
        const int sid = r.value("sid").toInt();
        if (!hourMean.contains(sid))
            hourMean.insert(sid, QVector<double>(24, 0));
        hourMean[sid][qBound(0, r.value("hh").toInt(), 23)] = r.value("a").toDouble();
        sampleN += r.value("n").toInt();
    }

    const auto daily = db_->query(
        "SELECT p.station_id AS sid, substr(o.start_time,1,10) AS d, SUM(o.energy_kwh) AS s "
        "FROM charge_order o JOIN pile p ON p.id=o.pile_id WHERE o.status='已完成' "
        "GROUP BY sid, d ORDER BY d");
    QHash<int, QVector<double>> series;
    for (const auto &r : daily)
        series[r.value("sid").toInt()].append(r.value("s").toDouble());
    double mae = 0, rmse = 0;
    int evalN = 0;
    for (auto it = series.begin(); it != series.end(); ++it) {
        auto v = it.value();
        if (v.size() < 5)
            continue;
        const int cut = qMax(2, int(v.size() * 0.8));
        double mean = 0;
        for (int i = 0; i < cut; ++i)
            mean += v[i];
        mean /= cut;
        for (int i = cut; i < v.size(); ++i) {
            const double e = v[i] - mean;
            mae += qAbs(e);
            rmse += e * e;
            ++evalN;
        }
    }
    if (evalN) {
        mae /= evalN;
        rmse = qSqrt(rmse / evalN);
    }

    int n = 0;
    QVector<QPair<QString, double>> loads;
    for (const auto &s : db_->query("SELECT id, name FROM station")) {
        const int sid = s.value("id").toInt();
        const QString name = s.value("name").toString();
        const auto piles = db_->query("SELECT * FROM pile WHERE station_id=?", {sid});
        int idle = 0;
        double cap = 0;
        for (const auto &p : piles) {
            cap += p.value("power_kw").toDouble();
            if (p.value("status").toString() == QString::fromUtf8("闲置"))
                ++idle;
        }
        if (cap < 1)
            cap = 60;
        QVector<double> curve(24);
        for (int h = 0; h < 24; ++h) {
            double base = hourMean.value(sid).value(h, 0);
            if (base <= 0)
                base = idle * 2.4 * (h >= 17 && h <= 21 ? 1.4 : (h >= 7 && h <= 9 ? 1.15 : 0.7));
            if (h >= 17 && h <= 21)
                base *= 1.12;
            curve[h] = qMax(0.0, base * wFactor);
            db_->execute("INSERT INTO hourly_load(station_id,hour,pred_kwh,created_at) VALUES(?,?,?,?)",
                         {sid, h, qRound(curve[h] * 100) / 100.0, t});
        }
        auto sumH = [&](int from, int len) {
            double x = 0;
            for (int i = 0; i < len; ++i)
                x += curve[(from + i) % 24];
            return x;
        };
        int peakH = 0;
        double peakV = -1;
        for (int h = 0; h < 24; ++h) {
            if (curve[h] > peakV) {
                peakV = curve[h];
                peakH = h;
            }
        }
        int peakStart = peakH, peakEnd = peakH;
        const double peakCut = qMax(peakV * 0.8, cap * 0.55);
        while (peakStart > 0 && curve[peakStart - 1] >= peakCut)
            --peakStart;
        while (peakEnd < 23 && curve[peakEnd + 1] >= peakCut)
            ++peakEnd;
        const QString peakWin = QString("%1:00-%2:00")
                                    .arg(peakStart, 2, 10, QChar('0'))
                                    .arg(peakEnd, 2, 10, QChar('0'));
        const int hs[] = {1, 6, 24};
        for (int h : hs) {
            const double pred = sumH(nowH, h);
            const int predIdle = qBound(0, int(qRound(piles.size() - pred / qMax(7.0, cap / qMax(1, piles.size())))), piles.size());
            db_->execute(
                "INSERT INTO load_forecast(station_id,horizon_hours,pred_kwh,pred_idle,peak_hour,created_at) VALUES(?,?,?,?,?,?)",
                {sid, h, qRound(pred * 100) / 100.0, predIdle, peakWin, t});
            ++n;
        }
        loads.append({name, sumH(nowH, 6)});
        const double rate = peakV / cap;
        if (rate >= 0.8)
            db_->execute("INSERT INTO analysis_alert(level,title,detail,created_at) VALUES(?,?,?,?)",
                         {QString::fromUtf8("严重"), QString::fromUtf8("高峰过载风险 · ") + name,
                          QString::fromUtf8("预测高峰负荷率 %1%，建议引导分流或增开慢充").arg(qRound(rate * 1000) / 10.0), t});
        else if (rate >= 0.55)
            db_->execute("INSERT INTO analysis_alert(level,title,detail,created_at) VALUES(?,?,?,?)",
                         {QString::fromUtf8("一般"), QString::fromUtf8("晚高峰需关注 · ") + name,
                          QString::fromUtf8("预测高峰约 %1 kWh，空闲桩约 %2").arg(peakV, 0, 'f', 1).arg(idle), t});
    }

    std::sort(loads.begin(), loads.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
    int pri = 1;
    for (const auto &L : loads) {
        db_->execute("INSERT INTO dispatch_plan(station,recommend,priority,reason,created_at) VALUES(?,?,?,?,?)",
                     {L.first, qRound(L.second * 10) / 10.0, pri,
                      pri == 1 ? QString::fromUtf8("未来6小时负荷最高，优先保障运维值班")
                               : QString::fromUtf8("按负荷从高到低分配巡检与引导资源"),
                      t});
        ++pri;
    }

    for (const auto &p : db_->query(
             "SELECT p.*, s.name AS station_name FROM pile p JOIN station s ON s.id=p.station_id")) {
        double score = 0.08;
        QString reason = QString::fromUtf8("运行平稳");
        if (p.value("status").toString() == QString::fromUtf8("故障")) {
            score = 0.93;
            reason = QString::fromUtf8("当前故障，需现场检修");
        } else {
            if (p.value("total_charge_count").toInt() > 40) {
                score += 0.22;
                reason = QString::fromUtf8("累计次数偏高，建议保养");
            }
            if (p.value("total_charge_minutes").toInt() > 1800) {
                score += 0.18;
                reason = QString::fromUtf8("累计时长偏高，关注接触器温升");
            }
            if (p.value("status").toString() == QString::fromUtf8("在用"))
                score += 0.08;
        }
        score = qBound(0.0, score, 0.99);
        const QString level = score >= 0.8 ? QString::fromUtf8("高风险")
                                           : (score >= 0.5 ? QString::fromUtf8("需关注") : QString::fromUtf8("正常"));
        db_->execute("INSERT INTO fault_risk(pile_no,station,score,level,reason,created_at) VALUES(?,?,?,?,?,?)",
                     {p.value("pile_no"), p.value("station_name"), qRound(score * 100) / 100.0, level, reason, t});
        if (score >= 0.8)
            db_->execute("INSERT INTO analysis_alert(level,title,detail,created_at) VALUES(?,?,?,?)",
                         {QString::fromUtf8("严重"), QString::fromUtf8("设备故障预警 ") + p.value("pile_no").toString(),
                          reason, t});
    }

    if (sampleN < 20)
        db_->execute("INSERT INTO analysis_alert(level,title,detail,created_at) VALUES(?,?,?,?)",
                     {QString::fromUtf8("提示"), QString::fromUtf8("样本偏少"),
                      QString::fromUtf8("历史完成订单不足，预测置信度有限，已回退到小时均值+时段因子"), t});
    db_->execute("INSERT INTO analysis_alert(level,title,detail,created_at) VALUES(?,?,?,?)",
                 {QString::fromUtf8("提示"), QString::fromUtf8("天气因子已融合"),
                  QString::fromUtf8("当前模拟天气：") + weather + QString::fromUtf8("，负荷系数 ") + QString::number(wFactor, 'f', 2), t});

    db_->execute(
        "INSERT INTO analysis_report(model_version,mae,rmse,sample_n,weather,created_at) VALUES(?,?,?,?,?,?)",
        {QStringLiteral("hour-mean-v1.2"), qRound(mae * 1000) / 1000.0, qRound(rmse * 1000) / 1000.0, sampleN, weather, t});
    return n;
}


