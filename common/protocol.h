#ifndef CHARGEHUB_PROTOCOL_H
#define CHARGEHUB_PROTOCOL_H

/**
 * @file protocol.h
 * @brief 用户端与管理端的唯一报文格式（运营 GUI 不走这里）。
 *
 * 【职责】组包 / 拆包。不解释 type，不查 token。
 * 【原理】帧 = 4 字节大端长度 + UTF-8 JSON。append() 把粘在一起的字节按长度切开。
 *         长度非法或超过 1MB 清空缓冲，避免永远拼不齐。
 * 【协作】Client::request 调用 pack；TcpServer 每连接一个实例调用 append/nextPacket。
 * 【报文】请求 {type,seq,role:"user",token,data}；响应多 code/message，0 成功。
 * 【详见】docs/模块与协作说明.md 与 protocol/messages.md
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
