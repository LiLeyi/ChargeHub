#ifndef CHARGEHUB_SESSIONSERVICE_H
#define CHARGEHUB_SESSIONSERVICE_H

/**
 * @file sessionservice.h
 * @brief Socket 会话：LOGIN/REGISTER、token 签发与 30 分钟滑动过期、公开用户快照。
 *
 * 【职责】用户端协议里与「我是谁」有关的唯一所有者。
 *   RequestDispatcher 每帧 authenticate(token)；TcpServer 用 userIdOfToken 绑定连接。
 *   不处理充电/预约（那些在 ChargeService / ReservationService）。
 *
 * 【原理】
 *   token 是去掉花括号和横线的 UUID 字符串，LOGIN/REGISTER 成功时 issueToken。
 *   内存三张表：token→userId、最后活跃 epoch、上次落库 epoch。
 *   进程启动 loadSessions：读 session 表，超过 30 分钟的行删掉。
 *   每次 userIdOfToken 命中则滑动 tokenAt_；距上次写库 ≥5 分钟才 UPDATE session.updated_at，
 *   避免每帧 HEARTBEAT 都打盘。过期或库中没有 → 0，authenticate 返回 401。
 *
 * 【公开用户 JSON】publicUser 给 LOGIN/REGISTER/资料/结算回包的 data.user：
 *   id, phone, nickname, avatarPath, hasAvatar, balance(元), status, createdAt,
 *   address, lat, lng, 以及可选 avatarMime/avatarBase64、closeReason/closedAt。
 *   不含 password_hash。
 *
 * 【密码】
 *   LOGIN：6～20 位即可（演示号 123456 仍能登）。
 *   REGISTER：6～20 且必须同时有大写、小写、数字。已注销手机号 409，不可再注册。
 *
 * 【协作】只被 Dispatch / RequestDispatcher / UserService（冻结时 dropUser）调用。
 * 【详见】protocol/messages.md ；表 session / user / user_avatar 见 database/schema.sql
 */

#include "transport/serviceresult.h"

#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <QVariantMap>

class Database;

/**
 * authenticate() 的返回：比 ServiceResult 多带 user 行，供路由 handler 当「当前用户」。
 * success=false 时 user 为空；code 为 401 或 403。
 */
struct AuthenticationResult {
    bool success = false;
    int code = 401;
    QString message;
    QVariantMap user;
};

/** 用户登录、持久会话和公开用户快照的唯一所有者。 */
class SessionService {
public:
    /** 打开后立刻 loadSessions。db 必须已 open。 */
    explicit SessionService(Database *db);

    /**
     * 协议 LOGIN。data: phone, password。
     * 成功 data: {user, token, isNew:false}。失败 400/401/403。
     */
    ServiceResult login(const QJsonObject &data);

    /**
     * 协议 REGISTER。强密码；成功 data 同 LOGIN 但 isNew:true。
     * 手机号已存在 409；已注销同样 409（留档不可再用）。
     */
    ServiceResult registerUser(const QJsonObject &data);

    /**
     * 校验 token 并加载未冻结、未注销的 user 行。
     * 作为副作用会滑动 token 活跃时间（因此 HEARTBEAT 能续命）。
     */
    AuthenticationResult authenticate(const QString &token) const;

    /**
     * token → user id。TcpServer bind / 推送用。
     * 空、过期、库中无记录返回 0，不抛异常。
     */
    int userIdOfToken(const QString &token) const;

    /**
     * 作废该用户全部 token（内存+session 表）。
     * 冻结账号、注销时由 UserService / AdminService 调用。
     */
    void dropUser(int userId);

    /**
     * 把 user 表一行变成可下发的 JSON（余额元、头像 Base64）。
     * 无定位字段时 lat/lng 回落到演示点（北理工一带）。
     */
    QJsonObject publicUser(const QVariantMap &user) const;

private:
    void loadSessions();
    void persistSession(const QString &token, int userId) const;
    void forgetSession(const QString &token) const;
    QString issueToken(int userId);

    Database *db_;
    mutable QMutex mutex_;
    mutable QHash<QString, int> tokenUser_;
    mutable QHash<QString, qint64> tokenAt_;
    mutable QHash<QString, qint64> tokenDbAt_;
};

#endif
