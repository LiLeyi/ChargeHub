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

    /** @param sessions 会话鉴权依赖，不接管所有权。 */
    explicit RequestDispatcher(SessionService *sessions);

    /** @param type 协议类型。@param handler 无需登录的处理函数；覆盖同名旧路由。 */
    void addPublicRoute(const QString &type, const Handler &handler);
    /**
     * @param type 协议类型。
     * @param mutating 是否为需做 8 秒幂等缓存的写操作。
     * @param handler 接收认证用户和 data 的业务处理函数。
     */
    void addAuthenticatedRoute(const QString &type, bool mutating, const Handler &handler);
    /**
     * 路由算法：查类型 → 查幂等缓存 → 鉴权 → 调 Handler → 封装/缓存响应。
     * @param request 含 type/seq/token/data 的协议请求。
     * @return 含 type/seq/code/message/data 的标准响应。
     */
    QJsonObject handle(const QJsonObject &request);

private:
    struct Route {
        bool authenticated = true;
        bool mutating = false;
        Handler handler;
    };

    /** @return 将 ServiceResult 转成协议响应信封。 */
    static QJsonObject response(const QString &type, int seq, const ServiceResult &result);
    /** @return 由 token、type、seq 拼成的幂等缓存键。 */
    QString idempotencyKey(const QJsonObject &request, const QString &type, int seq) const;
    /** @param key 幂等键。@param response 成功响应；写入缓存并清理过期项。 */
    void storeIdempotent(const QString &key, const QJsonObject &response);

    SessionService *sessions_;
    QHash<QString, Route> routes_;
    QMutex idempotencyMutex_;
    QHash<QString, QPair<qint64, QJsonObject>> idempotencyCache_;
};

#endif
