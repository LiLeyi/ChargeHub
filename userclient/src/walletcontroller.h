#ifndef CHARGEHUB_WALLETCONTROLLER_H
#define CHARGEHUB_WALLETCONTROLLER_H

#include <QJsonObject>
#include <QString>
#include <QtGlobal>

enum class WalletState {
    Idle,
    Loading,
    Ready,
    Editing,
    Confirming,
    Submitting,
    Succeeded,
    Failed,
    Uncertain,
    Querying,
};

enum class WalletResponseStatus {
    Applied,
    Duplicate,
    Ignored,
    ProtocolError,
};

struct AmountParseResult {
    bool valid = false;
    qint64 cents = 0;
    QString normalized;
};

struct WalletRequest {
    QString type;
    QJsonObject data;

    bool isValid() const { return !type.isEmpty(); }
};

class WalletController {
public:
    static AmountParseResult parseAmountCents(const QString &text);
    static QString formatCents(qint64 cents);

    WalletState state() const;
    bool hasTrustedBalance() const;
    qint64 balanceCents() const;
    QString balanceText() const;
    QString pendingRequestId() const;
    qint64 pendingAmountCents() const;
    bool isBusy() const;
    bool canSubmit() const;
    bool canQuery() const;

    void beginLoading();
    void beginEditing();
    bool beginConfirmation(qint64 amountCents);
    void cancelConfirmation();
    WalletRequest submitConfirmed();
    WalletRequest queryPending();
    WalletResponseStatus acceptWallet(const QJsonObject &data);
    WalletResponseStatus acceptRecharge(const QJsonObject &data);
    void markTimeout();
    void markFailure(const QString &errorCode);
    void startNewRecharge();

private:
    WalletState walletState = WalletState::Idle;
    bool trustedBalance = false;
    qint64 currentBalanceCents = 0;
    qint64 rechargeAmountCents = 0;
    QString rechargeRequestId;
    QString completedRequestId;
};

#endif
