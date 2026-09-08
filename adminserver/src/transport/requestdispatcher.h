#ifndef CHARGEHUB_REQUESTDISPATCHER_H
#define CHARGEHUB_REQUESTDISPATCHER_H

#include "serviceresult.h"

#include <functional>

#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QPair>
#include <QVariantMap>

class SessionService;

/** JSON 请求的路由、鉴权、幂等和响应封装层。 */
class RequestDispatcher {
public:
    using Handler = std::function<ServiceResult(const QVariantMap &, const QJsonObject &)>;

    explicit RequestDispatcher(SessionService *sessions);

    void addPublicRoute(const QString &type, const Handler &handler);
    void addAuthenticatedRoute(const QString &type, bool mutating, const Handler &handler);
    QJsonObject handle(const QJsonObject &request);

private:
    struct Route {
        bool authenticated = true;
        bool mutating = false;
        Handler handler;
    };

    static QJsonObject response(const QString &type, int seq, const ServiceResult &result);
    QString idempotencyKey(const QJsonObject &request, const QString &type, int seq) const;
    void storeIdempotent(const QString &key, const QJsonObject &response);

    SessionService *sessions_;
    QHash<QString, Route> routes_;
    QMutex idempotencyMutex_;
    QHash<QString, QPair<qint64, QJsonObject>> idempotencyCache_;
};

#endif
