#ifndef CHARGEHUB_SERVICERESULT_H
#define CHARGEHUB_SERVICERESULT_H

/**
 * @file serviceresult.h
 * @brief 领域服务返回值：与 TCP 信封、HTTP JSON 都解耦。
 *
 * 【职责】SessionService / ChargeService / StationService 等只返回本结构，
 *   由 RequestDispatcher::response 收成 {type,seq,code,message,data}。
 *   管理端 GUI 走 Dispatch 的 QString/QJsonObject 包装，不强制用本结构。
 *
 * 【字段】
 *   success  业务是否做成。Dispatcher 只看这个决定 code 写 0 还是写 code 字段。
 *   code     失败时的协议码：400/401/403/404/409；成功时通常为 0，会被 Dispatcher 忽略。
 *   message  直接展示给用户端的中文。
 *   data     成功时进入响应 data；失败时 Dispatcher 会丢掉，不要靠它传错误细节。
 *
 * 【约定】金额在 data 里仍是元。不要在这里做分/元换算。
 */

#include <QJsonObject>
#include <QString>

/** 与传输协议无关的业务调用结果。 */
struct ServiceResult {
    bool success = true;
    int code = 0;
    QString message = QStringLiteral("ok");
    QJsonObject data;

    /**
     * 成功结果。code 视为 0。
     * @param data     进入响应 data 的对象，可为空。
     * @param message  默认 "ok"；开充/结算等会改成中文提示。
     */
    static ServiceResult ok(const QJsonObject &data = {},
                            const QString &message = QStringLiteral("ok"))
    {
        return {true, 0, message, data};
    }

    /**
     * 失败结果。data 为空对象。
     * @param code     必须是协议里出现过的数字，见 protocol/messages.md。
     * @param message  非空中文，用户端弹窗会原样显示。
     */
    static ServiceResult fail(int code, const QString &message)
    {
        return {false, code, message, {}};
    }
};

#endif
