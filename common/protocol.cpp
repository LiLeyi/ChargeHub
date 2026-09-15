/**
 * @file protocol.cpp
 * @brief 长度前缀帧的编解码实现。发送只 pack；接收 append → tryDecode → nextPacket。
 *
 * 与 Python 对端的对应关系（烟测 / 多用户测试必须保持一致）：
 *   struct.pack(">I", len(body)) + body.encode("utf-8")
 *   struct.unpack(">I", hdr) 后再 recv 满 len 字节 json.loads
 *
 * 不在这里做 gzip、加密、type 分发。那些分别在 TLS（本作业不用）、业务层 RequestDispatcher。
 */
#include "protocol.h"

#include <QJsonDocument>
#include <QtEndian>

/**
 * @brief 大端 4 字节长度 + Compact UTF-8 JSON。
 * @see Protocol::pack 头文件说明。
 */
QByteArray Protocol::pack(const QJsonObject &obj)
{
    const QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    QByteArray out;
    out.resize(4);
    qToBigEndian<quint32>(static_cast<quint32>(body.size()), out.data());
    out.append(body);
    return out;
}

/**
 * @brief 接到缓冲末尾再拆。半包留在 buf_，完整包进 ready_。
 */
void Protocol::append(const QByteArray &chunk)
{
    buf_.append(chunk);
    tryDecode();
}

/**
 * @brief 取出已拆好的下一张 JSON；队列空则返回空对象。
 */
QJsonObject Protocol::nextPacket()
{
    if (ready_.isEmpty())
        return QJsonObject();
    return ready_.takeFirst();
}

/**
 * @brief 按大端长度切帧；非法长度清空缓冲，防止坏帧把拆包拖死。
 *
 * 1MiB 上限对应作业里最大的头像 Base64 等 data 字段，正常业务 JSON 远小于此。
 * 清空后本连接仍可继续收后续帧（对端若真在发超大包，后续也会再被清）。
 */
void Protocol::tryDecode()
{
    while (buf_.size() >= 4) {
        const quint32 len = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar *>(buf_.constData()));
        if (len == 0 || len > 1024 * 1024) {
            buf_.clear();
            return;
        }
        if (buf_.size() < static_cast<int>(4 + len))
            return;
        const QByteArray body = buf_.mid(4, static_cast<int>(len));
        buf_.remove(0, static_cast<int>(4 + len));
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject())
            ready_.append(doc.object());
    }
}
