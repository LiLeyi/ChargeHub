#ifndef CHARGEHUB_SERVICERESULT_H
#define CHARGEHUB_SERVICERESULT_H

#include <QJsonObject>
#include <QString>

/** 与传输协议无关的业务调用结果。 */
struct ServiceResult {
    bool success = true; ///< 业务是否成功；决定响应 code 是否归零。
    int code = 0;        ///< 失败状态码；成功时为 0。
    QString message = QStringLiteral("ok"); ///< 面向调用方的结果说明。
    QJsonObject data;    ///< 成功时的结构化业务数据。

    /** @param data 成功数据。@param message 可选提示。@return code=0 的结果对象。 */
    static ServiceResult ok(const QJsonObject &data = {},
                            const QString &message = QStringLiteral("ok"))
    {
        return {true, 0, message, data};
    }

    /** @param code 业务错误码。@param message 错误说明。@return data 为空的失败结果。 */
    static ServiceResult fail(int code, const QString &message)
    {
        return {false, code, message, {}};
    }
};

#endif
