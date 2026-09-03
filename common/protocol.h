#ifndef CHARGEHUB_PROTOCOL_H
#define CHARGEHUB_PROTOCOL_H

/**
 * @file protocol.h
 * @brief 用户端 ↔ 管理端 的唯一通信格式（管理端 GUI 不走这里）。
 *
 * 传输：TCP。默认端口 8888。
 * 帧：先 4 字节大端无符号长度，再等长的 UTF-8 JSON。
 * JSON 请求：{ type, seq, role:"user", token, data }
 * JSON 响应：{ type, seq, code, message, data }  code==0 成功。
 * 用户端禁止直连 SQLite；所有业务由管理端 Dispatch 写库后回包。
 */

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QtEndian>

class Protocol {
public:
    /** 把 JSON 编成「长度 + 正文」，发给对端。 */
    static QByteArray pack(const QJsonObject &obj);

    /** 把 TCP 读到的字节追加进缓冲，内部按长度切完整包。 */
    void append(const QByteArray &chunk);
    bool hasPacket() const { return !ready_.isEmpty(); }
    QJsonObject nextPacket();

private:
    QByteArray buf_;
    QList<QJsonObject> ready_;
    void tryDecode();
};

#endif
