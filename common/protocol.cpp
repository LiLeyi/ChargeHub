/**
 * @file protocol.cpp
 * @brief 组包 / 拆包，粘包时按长度切帧
 */
#include "protocol.h"

#include <QJsonDocument>
#include <QtEndian>

QByteArray Protocol::pack(const QJsonObject &obj)
{
    const QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    QByteArray out;
    out.resize(4);
    qToBigEndian<quint32>(static_cast<quint32>(body.size()), out.data());
    out.append(body);
    return out;
}

void Protocol::append(const QByteArray &chunk)
{
    buf_.append(chunk);
    tryDecode();
}

QJsonObject Protocol::nextPacket()
{
    if (ready_.isEmpty())
        return QJsonObject();
    return ready_.takeFirst();
}

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
