#ifndef CHARGEHUB_SERVICERESULT_H
#define CHARGEHUB_SERVICERESULT_H

#include <QJsonObject>
#include <QString>

/** 与传输协议无关的业务调用结果。 */
struct ServiceResult {
    bool success = true;
    int code = 0;
    QString message = QStringLiteral("ok");
    QJsonObject data;

    static ServiceResult ok(const QJsonObject &data = {},
                            const QString &message = QStringLiteral("ok"))
    {
        return {true, 0, message, data};
    }

    static ServiceResult fail(int code, const QString &message)
    {
        return {false, code, message, {}};
    }
};

#endif
