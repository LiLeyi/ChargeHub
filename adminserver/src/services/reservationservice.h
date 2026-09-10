#ifndef CHARGEHUB_RESERVATIONSERVICE_H
#define CHARGEHUB_RESERVATIONSERVICE_H

#include <QJsonObject>
#include <QVariantMap>

class Database;

/** 预约生命周期及有效预约查询。 */
class ReservationService {
public:
    /** @param db 数据库依赖，不接管所有权。 */
    explicit ReservationService(Database *db);

    /** 将已过 expires_at 的有效预约批量标记为过期。 */
    void expire() const;
    /** @param pileId 电桩主键。@return 该桩当前有效预约；不存在则为空 Map。 */
    QVariantMap activeForPile(int pileId) const;
    /** @param pile 电桩数据库行。@return 状态为空闲且无有效预约时返回 true。 */
    bool pileIsIdle(const QVariantMap &pile) const;
    /** @param user 当前用户。@return 用户尚未到期的预约及电站/电桩信息。 */
    QJsonObject list(const QVariantMap &user) const;
    /** @param user 当前用户。@param data 含 pileId。@return 15 分钟预约创建结果。 */
    QJsonObject reserve(const QVariantMap &user, const QJsonObject &data);
    /** @param user 当前用户。@return 取消其当前有效预约的结果。 */
    QJsonObject cancel(const QVariantMap &user);
    /** @param reservationId 预约主键。@return 有效预约成功改为已履约时返回 true。 */
    bool fulfill(int reservationId);
    /** @param userId 用户主键；批量取消其全部有效预约。 */
    void cancelActiveForUser(int userId);

private:
    Database *db_;
};

#endif
