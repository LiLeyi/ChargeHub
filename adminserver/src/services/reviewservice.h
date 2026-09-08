#ifndef CHARGEHUB_REVIEWSERVICE_H
#define CHARGEHUB_REVIEWSERVICE_H
#include <QJsonObject>
#include <QVariantMap>
#include <QVector>
class Database;
/** 评价写入、桩评价详情和运营端情感聚合；不修改订单。 */
class ReviewService {
public:
    explicit ReviewService(Database *db);
    QJsonObject submit(const QVariantMap &user, const QJsonObject &data);
    QJsonObject listForPile(const QVariantMap &user, const QJsonObject &data) const;
    QVector<QVariantMap> listNlp() const;
private:
    Database *db_;
};
#endif

