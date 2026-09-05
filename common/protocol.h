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
    /**
     * 把一张 JSON 编成「4 字节大端长度 + UTF-8 正文」，发给对端。
     * 发送端（Client::request、TcpServer 回包/推送）只调用这个静态函数。
     */
    static QByteArray pack(const QJsonObject &obj);

    /**
     * 把 TCP 刚读到的字节追加进缓冲，并立刻 tryDecode。
     * 可能一次 append 拆出 0 个、1 个或多个完整包。
     */
    void append(const QByteArray &chunk);

    /** 缓冲里是否已经有拆好、还没取走的 JSON。 */
    bool hasPacket() const { return !ready_.isEmpty(); }

    /**
     * 取出队列最前面一张完整 JSON。
     * 没有包时返回空对象；调用方应先看 hasPacket()。
     */
    QJsonObject nextPacket();

private:
    QByteArray buf_;           ///< 还没凑齐一帧的原始字节
    QList<QJsonObject> ready_; ///< 已经拆好、等待 nextPacket 取走

    /**
     * 循环：缓冲 ≥4 字节就读长度；0 或 >1MB 则清空（防坏帧）；
     * 正文够了就 fromJson，是对象才入队。半包则停，等下次 append。
     */
    void tryDecode();
};

#endif
