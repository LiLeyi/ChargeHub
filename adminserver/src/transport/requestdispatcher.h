#ifndef CHARGEHUB_REQUESTDISPATCHER_H
#define CHARGEHUB_REQUESTDISPATCHER_H

/**
 * @file requestdispatcher.h
 * @brief Socket JSON 的路由、鉴权、幂等和响应封装。TcpServer 不直接调领域服务。
 *
 * 【职责】
 *   把一帧请求变成统一响应信封 {type,seq,code,message,data}。
 *   公开路由（LOGIN/REGISTER）不查 token；其余先 SessionService::authenticate。
 *   写操作（mutating）在 8 秒内对同一 token|type|seq 直接返回上次成功响应，避免连点开两单、扣两次款。
 *
 * 【原理】
 *   Dispatch::registerRoutes 在构造时 addPublicRoute / addAuthenticatedRoute。
 *   handle()：
 *     1. type 不在表里：仍先鉴权（兼容旧客户端乱发 type），失败 401/403，成功则 400「未知请求类型」。
 *     2. mutating 且 token 非空：缓存命中且未满 8 秒 → 原样返回缓存 JSON（含当时的 seq/code/data）。
 *     3. 需登录：authenticate；失败不调 handler。
 *     4. handler(user行, data对象) → ServiceResult → response()。
 *     5. mutating 且本次 success：写入幂等缓存（最多约 200 条，溢出时删掉超过 8 秒的）。
 *
 * 【幂等范围】当前 mutating 的 type：RECHARGE、START_CHARGE、STOP_CHARGE、SETTLE_ORDER、
 *   RESERVE_PILE、CANCEL_RESERVE。QUERY_* / HEARTBEAT / REVIEW 等不缓存。
 *   键不包含 data 内容：同一 seq 重发即视为同一操作（客户端连点通常 seq 也相同，因为 UI 没等到回包）。
 *
 * 【金额】本层不改数字；失败时 data 强制 {}，避免半成功字段泄漏。
 *
 * 【协作】只被 Dispatch::handle 调用。SessionService 负责 token TTL 30 分钟。
 * 【详见】protocol/messages.md
 */

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
    /**
     * 领域处理函数。
     * @param user  已认证时为 user 表一行（QVariantMap）；公开路由则为空 map。
     * @param data  请求信封里的 data 对象，缺省为 {}。
     */
    using Handler = std::function<ServiceResult(const QVariantMap &, const QJsonObject &)>;

    /**
     * @param sessions  鉴权与 token 滑动过期的唯一入口；必须非空且比本对象活得更久（Dispatch 同生）。
     */
    explicit RequestDispatcher(SessionService *sessions);

    /**
     * 注册不需要 token 的 type（LOGIN、REGISTER）。
     * authenticated=false，mutating=false：登录本身不走 8 秒幂等（每次都会发新 token）。
     */
    void addPublicRoute(const QString &type, const Handler &handler);

    /**
     * 注册必须登录的 type。
     * @param mutating  true 则启用 token|type|seq 的 8 秒成功响应缓存。
     */
    void addAuthenticatedRoute(const QString &type, bool mutating, const Handler &handler);

    /**
     * TcpServer 每收到一帧就调用。线程模型：Qt 主线程同步执行。
     * @param request  已拆好的对象，缺字段时按空串/0 处理。
     * @return         始终带 type/seq/code/message/data，可直接 Protocol::pack。
     */
    QJsonObject handle(const QJsonObject &request);

private:
    struct Route {
        bool authenticated = true; ///< false 才允许空 token
        bool mutating = false;     ///< true 才查/写幂等缓存
        Handler handler;
    };

    /**
     * 把 ServiceResult 收成线协议信封。
     * success 时 code 强制 0（忽略 result.code）；失败时 data 强制 {}。
     */
    static QJsonObject response(const QString &type, int seq, const ServiceResult &result);

    /** 幂等键：token + '|' + type + '|' + seq。token 空时仍可算出键，但 handle 不会查缓存。 */
    QString idempotencyKey(const QJsonObject &request, const QString &type, int seq) const;

    /** 写入缓存；超过 200 条时删除早于 8 秒的条目。 */
    void storeIdempotent(const QString &key, const QJsonObject &response);

    SessionService *sessions_;
    QHash<QString, Route> routes_;
    QMutex idempotencyMutex_;
    QHash<QString, QPair<qint64, QJsonObject>> idempotencyCache_; ///< key → {写入时 epoch 秒, 完整响应}
};

#endif
