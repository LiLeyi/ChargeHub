#include "reviewservice.h"

#include "database.h"

#include <algorithm>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QPair>
#include <QDateTime>

static QString nowStr()
{
    return QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
}

/** 按星级和关键词做简易情感分析，只写评价表，不改订单。 */
static QJsonObject analyzeReview(int score, const QString &text)
{
    const QStringList pos = {QString::fromUtf8("快"), QString::fromUtf8("稳"), QString::fromUtf8("方便"),
                             QString::fromUtf8("好"), QString::fromUtf8("满意"), QString::fromUtf8("充足"),
                             QString::fromUtf8("安静"), QString::fromUtf8("清楚"), QString::fromUtf8("合适"),
                             QString::fromUtf8("满")};
    const QStringList neg = {QString::fromUtf8("排队"), QString::fromUtf8("故障"), QString::fromUtf8("贵"),
                             QString::fromUtf8("慢"), QString::fromUtf8("差"), QString::fromUtf8("等"),
                             QString::fromUtf8("掉线"), QString::fromUtf8("挤"), QString::fromUtf8("坏")};
    const QStringList keys = {QString::fromUtf8("快充"), QString::fromUtf8("慢充"), QString::fromUtf8("排队"),
                              QString::fromUtf8("功率"), QString::fromUtf8("价格"), QString::fromUtf8("车位"),
                              QString::fromUtf8("屏幕"), QString::fromUtf8("稳定"), QString::fromUtf8("故障"),
                              QString::fromUtf8("过夜"), QString::fromUtf8("高峰"), QString::fromUtf8("地铁")};
    int p = 0, n = 0;
    for (const auto &w : pos) {
        if (text.contains(w))
            ++p;
    }
    for (const auto &w : neg) {
        if (text.contains(w))
            ++n;
    }
    QJsonArray kw;
    for (const auto &w : keys) {
        if (text.contains(w))
            kw.append(w);
    }
    QString sent = QString::fromUtf8("中性");
    if (score >= 4 && n <= p)
        sent = QString::fromUtf8("正面");
    if (score <= 2 || n > p)
        sent = QString::fromUtf8("负面");
    const double s = qBound(-1.0, (score - 3) / 2.0 + 0.12 * (p - n), 1.0);
    return QJsonObject{{"sentiment", sent}, {"sentimentScore", s}, {"keywords", kw}};
}

static QString dumpDoc(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

ReviewService::ReviewService(Database *db) : db_(db) {}

/** 必须有文字；写 station_review 并浅层情感写入 review_doc。 */
QJsonObject ReviewService::submit(const QVariantMap &user, const QJsonObject &data)
{
    const int sid = data.value("stationId").toInt();
    const int pileId = data.value("pileId").toInt();
    const int score = data.value("score").toInt();
    const QString comment = data.value("comment").toString().trimmed();
    if (score < 1 || score > 5)
        return QJsonObject{{"error", QString::fromUtf8("评分须为 1~5 分")}};
    if (comment.size() < 2 || comment.size() > 300)
        return QJsonObject{{"error", QString::fromUtf8("评语须为 2~300 个字，不能只打分")}};
    auto pile = db_->one("SELECT id, station_id FROM pile WHERE id=?", {pileId});
    if (pile.isEmpty())
        return QJsonObject{{"error", QString::fromUtf8("电桩不存在")}};
    const int stationId = pile.value("station_id").toInt();
    if (sid > 0 && sid != stationId)
        return QJsonObject{{"error", QString::fromUtf8("电桩与电站不匹配")}};
    const int uid = user.value("id").toInt();
    const QString t = nowStr();
    const QJsonObject nlp = analyzeReview(score, comment);
    QJsonObject doc{
        {"schema", QStringLiteral("chargehub.review.v1")},
        {"userId", uid},
        {"pileId", pileId},
        {"stationId", stationId},
        {"score", score},
        {"comment", comment},
        {"createdAt", t},
        {"updatedAt", t},
        {"nlp", nlp},
    };
    auto old = db_->one("SELECT id FROM station_review WHERE user_id=? AND pile_id=?", {uid, pileId});
    auto oldDoc = db_->one("SELECT id, doc FROM review_doc WHERE user_id=? AND pile_id=?", {uid, pileId});
    if (!oldDoc.isEmpty()) {
        auto prev = QJsonDocument::fromJson(oldDoc.value("doc").toString().toUtf8()).object();
        if (!prev.value("createdAt").toString().isEmpty())
            doc["createdAt"] = prev.value("createdAt");
    }
    if (!old.isEmpty()) {
        db_->execute("UPDATE station_review SET score=?, comment=?, created_at=? WHERE id=?",
                     {score, comment, t, old.value("id")});
        if (!oldDoc.isEmpty())
            db_->execute("UPDATE review_doc SET doc=?, created_at=? WHERE id=?",
                         {dumpDoc(doc), t, oldDoc.value("id")});
        else
            db_->execute("INSERT INTO review_doc(pile_id,user_id,doc,created_at) VALUES(?,?,?,?)",
                         {pileId, uid, dumpDoc(doc), t});
        return QJsonObject{{"updated", true}, {"nlp", nlp}};
    }
    db_->execute(
        "INSERT INTO station_review(user_id,station_id,pile_id,score,comment,created_at) VALUES(?,?,?,?,?,?)",
        {uid, stationId, pileId, score, comment, t});
    db_->execute("INSERT INTO review_doc(pile_id,user_id,doc,created_at) VALUES(?,?,?,?)",
                 {pileId, uid, dumpDoc(doc), t});
    return QJsonObject{{"updated", false}, {"nlp", nlp}};
}

/** 某桩评价列表、均分和情感摘要。 */
QJsonObject ReviewService::listForPile(const QVariantMap &user, const QJsonObject &data) const
{
    const int pileId = data.value("pileId").toInt();
    auto pile = db_->one("SELECT * FROM pile WHERE id=?", {pileId});
    if (pile.isEmpty())
        return QJsonObject{{"error", QString::fromUtf8("电桩不存在")}};
    auto station = db_->one("SELECT * FROM station WHERE id=?", {pile.value("station_id")});
    auto docs = db_->query(
        "SELECT d.id, d.doc, u.nickname FROM review_doc d JOIN user u ON u.id=d.user_id "
        "WHERE d.pile_id=? ORDER BY d.id DESC",
        {pileId});
    if (docs.isEmpty()) {
        const auto rows = db_->query(
            "SELECT r.id, r.score, r.comment, r.created_at, r.user_id, u.nickname FROM station_review r "
            "JOIN user u ON u.id=r.user_id WHERE r.pile_id=? ORDER BY r.id DESC",
            {pileId});
        for (const auto &r : rows) {
            QJsonObject d{
                {"schema", QStringLiteral("chargehub.review.v1")},
                {"userId", r.value("user_id").toInt()},
                {"pileId", pileId},
                {"score", r.value("score").toInt()},
                {"comment", r.value("comment").toString()},
                {"createdAt", r.value("created_at").toString()},
                {"nlp", analyzeReview(r.value("score").toInt(), r.value("comment").toString())},
            };
            docs.append(QVariantMap{
                {"id", r.value("id")},
                {"doc", dumpDoc(d)},
                {"nickname", r.value("nickname")},
            });
        }
    }
    QJsonArray reviews;
    double sum = 0;
    int pos = 0, neu = 0, neg = 0;
    QMap<QString, int> kwc;
    QJsonObject mine;
    const int uid = user.value("id").toInt();
    for (const auto &r : docs) {
        auto o = QJsonDocument::fromJson(r.value("doc").toString().toUtf8()).object();
        if (!o.contains("nlp"))
            o.insert("nlp", analyzeReview(o.value("score").toInt(), o.value("comment").toString()));
        const auto nlp = o.value("nlp").toObject();
        const QString sent = nlp.value("sentiment").toString();
        if (sent == QString::fromUtf8("正面"))
            ++pos;
        else if (sent == QString::fromUtf8("负面"))
            ++neg;
        else
            ++neu;
        sum += o.value("score").toInt();
        for (const auto &k : nlp.value("keywords").toArray())
            kwc[k.toString()] += 1;
        const QJsonObject item{
            {"id", r.value("id").toInt()},
            {"nickname", r.value("nickname").toString()},
            {"score", o.value("score").toInt()},
            {"comment", o.value("comment").toString()},
            {"createdAt", o.value("createdAt").toString(o.value("updatedAt").toString())},
            {"sentiment", sent},
            {"keywords", nlp.value("keywords")},
            {"mine", o.value("userId").toInt() == uid},
        };
        reviews.append(item);
        if (o.value("userId").toInt() == uid)
            mine = item;
    }
    QJsonArray topKw;
    QList<QPair<int, QString>> ranked;
    for (auto it = kwc.begin(); it != kwc.end(); ++it)
        ranked.append({it.value(), it.key()});
    std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    for (int i = 0; i < qMin(6, ranked.size()); ++i)
        topKw.append(QJsonObject{{"word", ranked[i].second}, {"n", ranked[i].first}});
    const int n = reviews.size();
    return QJsonObject{
        {"pile", QJsonObject{
             {"id", pile.value("id").toInt()},
             {"pileNo", pile.value("pile_no").toString()},
             {"type", pile.value("type").toString()},
             {"powerKw", pile.value("power_kw").toDouble()},
             {"status", pile.value("status").toString()},
             {"stationId", pile.value("station_id").toInt()},
         }},
        {"station", QJsonObject{
             {"id", station.value("id").toInt()},
             {"name", station.value("name").toString()},
             {"address", station.value("address").toString()},
             {"pricePerKwh", station.value("price_per_kwh").toDouble()},
         }},
        {"nlp", QJsonObject{
             {"count", n},
             {"avgScore", n ? sum / n : 0},
             {"positive", pos},
             {"neutral", neu},
             {"negative", neg},
             {"keywords", topKw},
         }},
        {"reviews", reviews},
        {"mine", mine},
    };
}

/** 运营侧评价情感汇总。 */
QVector<QVariantMap> ReviewService::listNlp() const
{
    struct Agg {
        int n = 0, pos = 0, neu = 0, neg = 0;
        double sum = 0;
        QString pileNo, station;
        QMap<QString, int> kw;
    };
    QHash<int, Agg> m;
    const auto docs = db_->query(
        "SELECT d.pile_id, d.doc, p.pile_no, s.name AS station FROM review_doc d "
        "JOIN pile p ON p.id=d.pile_id JOIN station s ON s.id=p.station_id");
    for (const auto &r : docs) {
        auto o = QJsonDocument::fromJson(r.value("doc").toString().toUtf8()).object();
        auto nlp = o.value("nlp").toObject();
        if (nlp.isEmpty())
            nlp = analyzeReview(o.value("score").toInt(), o.value("comment").toString());
        Agg &a = m[r.value("pile_id").toInt()];
        a.pileNo = r.value("pile_no").toString();
        a.station = r.value("station").toString();
        a.n += 1;
        a.sum += o.value("score").toInt();
        const QString sent = nlp.value("sentiment").toString();
        if (sent == QString::fromUtf8("正面"))
            ++a.pos;
        else if (sent == QString::fromUtf8("负面"))
            ++a.neg;
        else
            ++a.neu;
        for (const auto &k : nlp.value("keywords").toArray())
            a.kw[k.toString()] += 1;
    }
    QVector<QVariantMap> out;
    for (auto it = m.begin(); it != m.end(); ++it) {
        const Agg &a = it.value();
        QList<QPair<int, QString>> ranked;
        for (auto k = a.kw.begin(); k != a.kw.end(); ++k)
            ranked.append({k.value(), k.key()});
        std::sort(ranked.begin(), ranked.end(), [](const auto &x, const auto &y) { return x.first > y.first; });
        QStringList top;
        for (int i = 0; i < qMin(4, ranked.size()); ++i)
            top.append(ranked[i].second);
        out.append(QVariantMap{
            {"pile_no", a.pileNo},
            {"station", a.station},
            {"count", a.n},
            {"avg", a.n ? a.sum / a.n : 0},
            {"positive", a.pos},
            {"neutral", a.neu},
            {"negative", a.neg},
            {"keywords", top.join(QString::fromUtf8(" · "))},
        });
    }
    std::sort(out.begin(), out.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value("count").toInt() > b.value("count").toInt();
    });
    return out;
}
