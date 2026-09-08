#include "userservice.h"

#include "chargeservice.h"
#include "database.h"
#include "reservationservice.h"
#include "sessionservice.h"
#include "stationservice.h"

#include <QBuffer>
#include <QDateTime>
#include <QImage>
#include <QIODevice>
#include <QJsonArray>
#include <QtMath>

/** 头像压到 256px JPEG，便于 BLOB 入库。 */
static QByteArray jpegAvatar(const QByteArray &raw, QString *err)
{
    QImage img;
    if (!img.loadFromData(raw)) {
        *err = QString::fromUtf8("无法识别图片，请选择 jpg / png");
        return {};
    }
    if (img.width() > 256 || img.height() > 256)
        img = img.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (img.format() != QImage::Format_RGB32 && img.format() != QImage::Format_RGB888)
        img = img.convertToFormat(QImage::Format_RGB32);
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    if (!img.save(&buf, "JPEG", 85)) {
        *err = QString::fromUtf8("图片编码失败");
        return {};
    }
    if (out.size() > 200 * 1024) {
        *err = QString::fromUtf8("图片过大，请换一张更小的图");
        return {};
    }
    return out;
}

static qint64 fenOf(double yuan) { return qRound(yuan * 100.0); }
static double money(double value) { return fenOf(value) / 100.0; }
static double moneyFen(qint64 fen) { return fen / 100.0; }
static QString nowStr() { return QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"); }

UserService::UserService(Database *db, SessionService *sessions, StationService *stations,
                         ReservationService *reservations, ChargeService *charges)
    : db_(db), sessions_(sessions), stations_(stations), reservations_(reservations), charges_(charges)
{
}

/** 改昵称和/或头像（JPEG BLOB）；clearAvatar 则删 user_avatar。 */
QJsonObject UserService::updateProfile(const QVariantMap &user, const QJsonObject &data)
{
    const int uid = user.value("id").toInt();
    if (data.contains("nickname")) {
        const QString nick = data.value("nickname").toString().trimmed();
        if (nick.size() < 1 || nick.size() > 20)
            return QJsonObject{{"error", QString::fromUtf8("昵称长度须为 1~20 个字符")}};
        db_->execute("UPDATE user SET nickname=? WHERE id=?", {nick, uid});
    }
    if (data.value("clearAvatar").toBool()) {
        db_->execute("DELETE FROM user_avatar WHERE user_id=?", {uid});
        db_->execute("UPDATE user SET avatar_path=? WHERE id=?", {QStringLiteral(""), uid});
    } else if (data.contains("avatarBase64")) {
        QString b64 = data.value("avatarBase64").toString().trimmed();
        if (b64.contains(QLatin1Char(',')))
            b64 = b64.section(QLatin1Char(','), -1);
        const QByteArray raw = QByteArray::fromBase64(b64.toLatin1());
        if (raw.isEmpty())
            return QJsonObject{{"error", QString::fromUtf8("请选择有效的头像图片")}};
        if (raw.size() > 2 * 1024 * 1024)
            return QJsonObject{{"error", QString::fromUtf8("图片不能超过 2MB")}};
        QString err;
        const QByteArray jpeg = jpegAvatar(raw, &err);
        if (jpeg.isEmpty())
            return QJsonObject{{"error", err}};
        db_->execute("INSERT OR REPLACE INTO user_avatar(user_id,mime,data,updated_at) VALUES(?,?,?,?)",
                     {uid, QStringLiteral("image/jpeg"), jpeg, nowStr()});
        db_->execute("UPDATE user SET avatar_path=? WHERE id=?", {QStringLiteral("db"), uid});
    } else if (data.contains("avatarPath")) {
        db_->execute("UPDATE user SET avatar_path=? WHERE id=?", {data.value("avatarPath").toString(), uid});
    }
    if (data.contains("address")) {
        const QString addr = data.value("address").toString().trimmed();
        if (addr.size() > 80)
            return QJsonObject{{"error", QString::fromUtf8("住址过长，请控制在 80 字以内")}};
        const auto coordinates = stations_->coordinatesForAddress(addr);
        db_->execute("UPDATE user SET address=?, loc_lat=?, loc_lng=? WHERE id=?",
                     {addr, coordinates.first, coordinates.second, uid});
    }
    auto u = db_->one("SELECT * FROM user WHERE id=?", {uid});
    return QJsonObject{{"user", sessions_->publicUser(u)}};
}

/** 模拟充值，单笔 ≤ 10000 元，内部按分入账。 */
/** 事务：加余额 + 写 recharge_log。单笔不超过 10000 元。 */
QJsonObject UserService::recharge(const QVariantMap &user, const QJsonObject &data)
{
    bool ok = false;
    const double amount = data.value("amount").toVariant().toDouble(&ok);
    if (!ok || amount <= 0 || amount > 10000)
        return QJsonObject{{"error", QString::fromUtf8("单笔充值须大于 0 且不超过 10000 元")}};
    const double add = money(amount);
    const QString t = nowStr();
    int logId = 0;
    if (!db_->transaction([&] {
            const auto fresh = db_->one("SELECT balance FROM user WHERE id=?", {user.value("id")});
            const double nb = moneyFen(fenOf(fresh.value("balance").toDouble()) + fenOf(add));
            if (db_->execute("UPDATE user SET balance=? WHERE id=?", {nb, user.value("id")}) < 0)
                return false;
            logId = db_->execute("INSERT INTO recharge_log(user_id,amount,result,created_at) VALUES(?,?,?,?)",
                                 {user.value("id"), add, QString::fromUtf8("成功"), t});
            return logId > 0;
        })) {
        return QJsonObject{{"error", QString::fromUtf8("充值未写入，请重试")}};
    }
    const QString tradeNo = QString("RC%1").arg(logId, 8, 10, QChar('0'));
    auto u = db_->one("SELECT * FROM user WHERE id=?", {user.value("id")});
    return QJsonObject{{"user", sessions_->publicUser(u)}, {"tradeNo", tradeNo}, {"amount", add}};
}

/** 账号标注销、作废 token；历史订单留下，手机号不能再注册。 */
QJsonObject UserService::closeAccount(const QVariantMap &user)
{
    const int uid = user.value("id").toInt();
    auto open = charges_->openOrder(uid);
    if (!open.isEmpty())
        return QJsonObject{{"errorCode", 409},
                           {"error", QString::fromUtf8("请先结束充电并完成结算，再注销账号")}};
    reservations_->cancelActiveForUser(uid);
    const QString t = nowStr();
    db_->execute("UPDATE user SET status=?, close_reason=?, closed_at=? WHERE id=?",
                 {QString::fromUtf8("注销"), QString::fromUtf8("用户注销"), t, uid});
    db_->execute("INSERT INTO audit_log(actor,action,target,result,created_at) VALUES(?,?,?,?,?)",
                 {user.value("phone").toString(), QString::fromUtf8("用户注销"),
                  QString("uid=%1").arg(uid), QString::fromUtf8("留档禁用"), t});
    sessions_->dropUser(uid);
    return QJsonObject{{"closed", true}, {"userId", uid}};
}

/** 当前用户充值流水。 */
QJsonObject UserService::listRecharge(const QVariantMap &user) const
{
    const auto rows = db_->query(
        "SELECT * FROM recharge_log WHERE user_id=? ORDER BY id DESC LIMIT 30",
        {user.value("id")});
    QJsonArray arr;
    for (const auto &r : rows) {
        arr.append(QJsonObject{
            {"id", r.value("id").toInt()},
            {"tradeNo", QString("RC%1").arg(r.value("id").toInt(), 8, 10, QChar('0'))},
            {"amount", r.value("amount").toDouble()},
            {"result", r.value("result").toString()},
            {"createdAt", r.value("created_at").toString()},
        });
    }
    return QJsonObject{{"records", arr}, {"balance", money(user.value("balance").toDouble())}};
}
