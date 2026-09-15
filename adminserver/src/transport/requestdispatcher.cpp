/**
 * @file requestdispatcher.cpp
 * @brief 路由表查找、token 鉴权、8 秒写操作幂等、统一信封。
 *
 * 常量 kIdempotencySeconds=8 与 protocol/messages.md 一致。
 * 缓存不持久化：进程退出即丢失，管理端重启后连点保护从头开始（token 本身仍可从 session 表恢复）。
 */
#include "requestdispatcher.h"

#include "services/sessionservice.h"

#include <QDateTime>
#include <QMutexLocker>

namespace {
constexpr qint64 kIdempotencySeconds = 8;
constexpr int kMaxCachedResponses = 200;
}

RequestDispatcher::RequestDispatcher(SessionService *sessions) : sessions_(sessions) {}

/** @brief 公开路由：不鉴权、不幂等。后注册同名 type 会覆盖前者。 */
void RequestDispatcher::addPublicRoute(const QString &type, const Handler &handler)
{
    routes_.insert(type, {false, false, handler});
}

/** @brief 登录后路由。mutating=true 的 type 见头文件列表。 */
void RequestDispatcher::addAuthenticatedRoute(const QString &type, bool mutating,
                                               const Handler &handler)
{
    routes_.insert(type, {true, mutating, handler});
}

/**
 * @brief 组装线协议响应。失败时不回传 result.data，避免调用方把半成品当成功展示。
 */
QJsonObject RequestDispatcher::response(const QString &type, int seq,
                                        const ServiceResult &result)
{
    return QJsonObject{{"type", type},
                       {"seq", seq},
                       {"code", result.success ? 0 : result.code},
                       {"message", result.message},
                       {"data", result.success ? result.data : QJsonObject()}};
}

QString RequestDispatcher::idempotencyKey(const QJsonObject &request, const QString &type,
                                           int seq) const
{
    return request.value("token").toString() + QLatin1Char('|') + type + QLatin1Char('|')
        + QString::number(seq);
}

void RequestDispatcher::storeIdempotent(const QString &key, const QJsonObject &value)
{
    QMutexLocker locker(&idempotencyMutex_);
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    idempotencyCache_.insert(key, {now, value});
    if (idempotencyCache_.size() <= kMaxCachedResponses)
        return;
    const qint64 cutoff = now - kIdempotencySeconds;
    for (auto it = idempotencyCache_.begin(); it != idempotencyCache_.end();) {
        if (it.value().first < cutoff)
            it = idempotencyCache_.erase(it);
        else
            ++it;
    }
}

/**
 * @brief 一帧请求的完整处理。顺序：查表 →（写操作）幂等命中 → 鉴权 → handler → 缓存成功写。
 *
 * 未知 type 也先鉴权：未登录的乱 type 得到 401 而不是 400，避免被用来探测路由表。
 */
QJsonObject RequestDispatcher::handle(const QJsonObject &request)
{
    const QString type = request.value("type").toString();
    const int seq = request.value("seq").toInt();
    const auto route = routes_.constFind(type);

    // 旧协议中除 LOGIN/REGISTER 外均先鉴权，未知类型也保持这一行为。
    if (route == routes_.cend()) {
        const auto auth = sessions_->authenticate(request.value("token").toString());
        if (!auth.success)
            return response(type, seq, ServiceResult::fail(auth.code, auth.message));
        return response(type, seq, ServiceResult::fail(400, QString::fromUtf8("未知请求类型")));
    }

    const QString key = idempotencyKey(request, type, seq);
    if (route->mutating && !request.value("token").toString().isEmpty()) {
        QMutexLocker locker(&idempotencyMutex_);
        const auto cached = idempotencyCache_.constFind(key);
        if (cached != idempotencyCache_.cend()
            && QDateTime::currentSecsSinceEpoch() - cached.value().first < kIdempotencySeconds)
            return cached.value().second;
    }

    QVariantMap user;
    if (route->authenticated) {
        const auto auth = sessions_->authenticate(request.value("token").toString());
        if (!auth.success)
            return response(type, seq, ServiceResult::fail(auth.code, auth.message));
        user = auth.user;
    }

    const ServiceResult result = route->handler(user, request.value("data").toObject());
    const QJsonObject envelope = response(type, seq, result);
    if (route->mutating && result.success && !request.value("token").toString().isEmpty())
        storeIdempotent(key, envelope);
    return envelope;
}
