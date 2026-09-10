#ifndef CHARGEHUB_REVIEWSERVICE_H
#define CHARGEHUB_REVIEWSERVICE_H
#include <QJsonObject>
#include <QVariantMap>
#include <QVector>
class Database;
/** 评价写入、桩评价详情和运营端情感聚合；不修改订单。 */
class ReviewService {
public:
    /** @param db 数据库依赖，不接管所有权。 */
    explicit ReviewService(Database *db);
    /** @param user 当前用户。@param data 含 stationId/pileId/score/comment。@return 写入结果。 */
    QJsonObject submit(const QVariantMap &user, const QJsonObject &data);
    /** @param user 当前用户。@param data 含 pileId。@return 桩信息、评价列表及情感汇总。 */
    QJsonObject listForPile(const QVariantMap &user, const QJsonObject &data) const;
    /** @return 运营端 NLP 汇总行；算法按关键词规则聚合正面/中性/负面。 */
    QVector<QVariantMap> listNlp() const;
private:
    Database *db_;
};
#endif
