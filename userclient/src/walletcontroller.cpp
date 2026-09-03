#include "walletcontroller.h"

#include <QtMath>
#include <QUuid>

namespace {

bool readIntegerCents(const QJsonObject &data, const QString &name, qint64 *value)
{
    const QJsonValue field = data.value(name);
    if (!field.isDouble())
        return false;
    const double number = field.toDouble(-1);
    if (!qIsFinite(number) || number < 0 || number > 9007199254740991.0
        || number != qFloor(number)) {
        return false;
    }
    *value = qint64(number);
    return true;
}

}

AmountParseResult WalletController::parseAmountCents(const QString &text)
{
    const QString value = text.trimmed();
    if (value.isEmpty())
        return {};

    const int decimalPoint = value.indexOf(QLatin1Char('.'));
    if (decimalPoint != value.lastIndexOf(QLatin1Char('.')))
        return {};
    const QString yuanText = decimalPoint < 0 ? value : value.left(decimalPoint);
    const QString fractionText = decimalPoint < 0 ? QString() : value.mid(decimalPoint + 1);
    if (yuanText.isEmpty() || fractionText.size() > 2 || (decimalPoint >= 0 && fractionText.isEmpty()))
        return {};
    if (yuanText.size() > 1 && yuanText.startsWith(QLatin1Char('0')))
        return {};

    qint64 yuan = 0;
    for (const QChar character : yuanText) {
        if (character < QLatin1Char('0') || character > QLatin1Char('9'))
            return {};
        yuan = yuan * 10 + character.digitValue();
        if (yuan > 10000)
            return {};
    }
    qint64 fraction = 0;
    for (const QChar character : fractionText) {
        if (character < QLatin1Char('0') || character > QLatin1Char('9'))
            return {};
        fraction = fraction * 10 + character.digitValue();
    }
    if (fractionText.size() == 1)
        fraction *= 10;
    const qint64 cents = yuan * 100 + fraction;
    if (cents < 1 || cents > 1000000)
        return {};
    return AmountParseResult{true, cents,
                             QStringLiteral("%1.%2")
                                 .arg(cents / 100)
                                 .arg(cents % 100, 2, 10, QLatin1Char('0'))};
}

QString WalletController::formatCents(qint64 cents)
{
    if (cents < 0)
        return QStringLiteral("--");
    return QStringLiteral("¥%1.%2")
        .arg(cents / 100)
        .arg(cents % 100, 2, 10, QLatin1Char('0'));
}

WalletState WalletController::state() const { return walletState; }
bool WalletController::hasTrustedBalance() const { return trustedBalance; }
qint64 WalletController::balanceCents() const { return currentBalanceCents; }
QString WalletController::balanceText() const
{
    return trustedBalance ? formatCents(currentBalanceCents) : QStringLiteral("--");
}
QString WalletController::pendingRequestId() const { return rechargeRequestId; }
qint64 WalletController::pendingAmountCents() const { return rechargeAmountCents; }
bool WalletController::isBusy() const
{
    return walletState == WalletState::Loading || walletState == WalletState::Submitting
        || walletState == WalletState::Querying;
}
bool WalletController::canSubmit() const
{
    return trustedBalance
        && (walletState == WalletState::Ready || walletState == WalletState::Editing
            || walletState == WalletState::Failed || walletState == WalletState::Succeeded);
}
bool WalletController::canQuery() const { return walletState == WalletState::Uncertain; }

void WalletController::beginLoading()
{
    if (!isBusy())
        walletState = WalletState::Loading;
}

void WalletController::beginEditing()
{
    if (isBusy() || walletState == WalletState::Uncertain)
        return;
    rechargeAmountCents = 0;
    rechargeRequestId.clear();
    walletState = WalletState::Editing;
}

bool WalletController::beginConfirmation(qint64 amountCents)
{
    if (!canSubmit() || amountCents < 1 || amountCents > 1000000)
        return false;
    rechargeAmountCents = amountCents;
    rechargeRequestId.clear();
    walletState = WalletState::Confirming;
    return true;
}

void WalletController::cancelConfirmation()
{
    if (walletState == WalletState::Confirming)
        walletState = WalletState::Editing;
}

WalletRequest WalletController::submitConfirmed()
{
    if (walletState != WalletState::Confirming || rechargeAmountCents < 1)
        return {};
    rechargeRequestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    walletState = WalletState::Submitting;
    return WalletRequest{
        QStringLiteral("RECHARGE"),
        QJsonObject{{"amountCents", QJsonValue::fromVariant(rechargeAmountCents)},
                    {"requestId", rechargeRequestId}},
    };
}

WalletRequest WalletController::queryPending()
{
    if (!canQuery() || rechargeRequestId.isEmpty())
        return {};
    walletState = WalletState::Querying;
    return WalletRequest{QStringLiteral("QUERY_RECHARGE"),
                         QJsonObject{{"requestId", rechargeRequestId}}};
}

WalletResponseStatus WalletController::acceptWallet(const QJsonObject &data)
{
    if (walletState == WalletState::Confirming || walletState == WalletState::Submitting
        || walletState == WalletState::Querying || walletState == WalletState::Uncertain) {
        return WalletResponseStatus::Ignored;
    }
    qint64 serverBalance = 0;
    if (!readIntegerCents(data, QStringLiteral("balanceCents"), &serverBalance)) {
        walletState = WalletState::Failed;
        return WalletResponseStatus::ProtocolError;
    }
    currentBalanceCents = serverBalance;
    trustedBalance = true;
    walletState = WalletState::Ready;
    return WalletResponseStatus::Applied;
}

WalletResponseStatus WalletController::acceptRecharge(const QJsonObject &data)
{
    const QString requestId = data.value("requestId").toString();
    if (!completedRequestId.isEmpty() && requestId == completedRequestId)
        return WalletResponseStatus::Duplicate;
    if ((walletState != WalletState::Submitting && walletState != WalletState::Querying
         && walletState != WalletState::Uncertain)
        || requestId.isEmpty() || requestId != rechargeRequestId) {
        return WalletResponseStatus::Ignored;
    }
    qint64 serverAmount = 0;
    qint64 serverBalance = 0;
    if (data.value("status").toString() != QStringLiteral("succeeded")
        || data.value("tradeNo").toString().isEmpty()
        || !readIntegerCents(data, QStringLiteral("amountCents"), &serverAmount)
        || !readIntegerCents(data, QStringLiteral("balanceCents"), &serverBalance)
        || serverAmount != rechargeAmountCents) {
        walletState = WalletState::Failed;
        return WalletResponseStatus::ProtocolError;
    }
    currentBalanceCents = serverBalance;
    trustedBalance = true;
    completedRequestId = requestId;
    walletState = WalletState::Succeeded;
    return WalletResponseStatus::Applied;
}

void WalletController::markTimeout()
{
    if (walletState == WalletState::Submitting || walletState == WalletState::Querying)
        walletState = WalletState::Uncertain;
    else if (walletState == WalletState::Loading)
        walletState = WalletState::Failed;
}

void WalletController::markFailure(const QString &errorCode)
{
    if ((errorCode == QStringLiteral("NETWORK_ERROR") || errorCode == QStringLiteral("TIMEOUT")
         || errorCode == QStringLiteral("RECHARGE_NOT_FOUND"))
        && !rechargeRequestId.isEmpty()) {
        walletState = WalletState::Uncertain;
    } else {
        walletState = WalletState::Failed;
    }
}

void WalletController::startNewRecharge()
{
    if (isBusy())
        return;
    rechargeAmountCents = 0;
    rechargeRequestId.clear();
    walletState = trustedBalance ? WalletState::Editing : WalletState::Idle;
}
