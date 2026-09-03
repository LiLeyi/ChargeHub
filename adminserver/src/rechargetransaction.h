#ifndef CHARGEHUB_RECHARGETRANSACTION_H
#define CHARGEHUB_RECHARGETRANSACTION_H

#include <QtGlobal>
#include <QString>

enum class RechargeStatus {
    Succeeded,
    NotFound,
    InvalidAmount,
    InvalidRequestId,
    UserNotFound,
    UserFrozen,
    StorageError,
};

struct RechargeResult {
    RechargeStatus status = RechargeStatus::StorageError;
    QString requestId;
    QString tradeNo;
    qint64 amountCents = 0;
    qint64 balanceCents = 0;
    bool replayed = false;
};

class RechargeTransaction {
public:
    explicit RechargeTransaction(const QString &databasePath);

    RechargeResult applyRecharge(int userId, qint64 amountCents, const QString &requestId) const;
    RechargeResult queryRecharge(int userId, const QString &requestId) const;

    static bool isValidRequestId(const QString &requestId);

private:
    QString databasePath;
};

#endif
