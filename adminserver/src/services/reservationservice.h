#ifndef CHARGEHUB_RESERVATIONSERVICE_H
#define CHARGEHUB_RESERVATIONSERVICE_H

#include <QJsonObject>
#include <QVariantMap>

class Database;

/** 预约生命周期及有效预约查询。 */
class ReservationService {
public:
    explicit ReservationService(Database *db);

    void expire() const;
    QVariantMap activeForPile(int pileId) const;
    bool pileIsIdle(const QVariantMap &pile) const;
    QJsonObject list(const QVariantMap &user) const;
    QJsonObject reserve(const QVariantMap &user, const QJsonObject &data);
    QJsonObject cancel(const QVariantMap &user);
    bool fulfill(int reservationId);
    void cancelActiveForUser(int userId);

private:
    Database *db_;
};

#endif
