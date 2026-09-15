#ifndef CHARGEHUB_PROTOCOL_H
#define CHARGEHUB_PROTOCOL_H

/**
 * @file protocol.h
 * @brief 用户端 ↔ 管理端的唯一 TCP 报文编解码（运营 GUI、大屏 HTTP、腾讯 HTTP 都不走这里）。
 *
 * 【职责】
 *   只负责「JSON 对象 ↔ 长度前缀字节」。不解释 type、不查 token、不算费、不写库。
 *   发送端只调静态 pack()；接收端每条 TCP 连接一个 Protocol 实例，反复 append()+nextPacket()。
 *
 * 【原理 / 线格式】
 *   一帧 = 4 字节大端无符号长度（quint32 / 网络字节序）+ 恰好那么长的 UTF-8 JSON。
 *   JSON 必须是对象（`{...}`），数组或裸值会被丢弃。
 *   Compact 编码（无多余空白），与 `tests/testmultiuser.py`、`scripts/smokelocal.py` 的 `>I` + utf-8 一致。
 *
 *   粘包：TCP 可能一次 readyRead 送来半包、整包、或多包。append() 把字节接到 buf_ 末尾，
 *   tryDecode() 循环切包：先读 4 字节长度，正文够了才 fromJson 入队，半包留在 buf_。
 *   坏帧：长度为 0 或 >1MB（1024*1024）时清空整个缓冲并停止本轮解码，避免永远等一个假长度。
 *   非法 JSON / 非对象：该帧丢掉，缓冲继续处理后面的字节（不把对端卡死）。
 *
 * 【请求信封】（用户端 Client::request 写出）
 *   {
 *     "type":  "LOGIN" 等，见 protocol/messages.md,
 *     "seq":   正整数，本连接内 Client 自增，从 1 起；PUSH_CHARGE 的 seq 固定 0,
 *     "role":  恒为 "user"（本作业没有管理员走这根线）,
 *     "token": 登录前空串；登录后带 LOGIN/REGISTER 返回的 token,
 *     "data":  业务对象，无字段时为 {}
 *   }
 *
 * 【响应信封】（TcpServer 把 Dispatch::handle 的返回 pack 回去）
 *   {
 *     "type":    与请求相同（推送则为 "PUSH_CHARGE"）,
 *     "seq":     与请求相同（推送为 0）,
 *     "code":    0 成功；400 参数；401 未登录/密码错；403 冻结或注销；404 找不到；409 冲突,
 *     "message": 给人看的中文,
 *     "data":    成功时的业务对象；失败时为空对象 {}（错误原因只在 message/code）
 *   }
 *
 * 【金额】接口里始终是「元」、两位小数。分的取整只发生在服务端写库之前，本类不碰数字。
 *
 * 【协作】
 *   - 用户端：`userclient/src/client.cpp` 发送 pack，接收 append/nextPacket，再 emit responded。
 *   - 管理端：`adminserver/src/tcpserver.cpp` 每连接一个 Protocol*；回包与 PUSH_CHARGE 都 pack。
 *   - 业务：`Dispatch::handle` / `RequestDispatcher` 只看见已经拆好的 QJsonObject。
 *   - 管理端 MainWindow、Flask 大屏、TencentApi 都不使用本类。
 *
 * 【详见】protocol/messages.md 、docs/模块与协作说明.md 、docs/接口约定.md（HTTP 不是本协议）
 */

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QtEndian>

class Protocol {
public:
    /**
     * 把一张 JSON 对象编成可直接 write 到 QTcpSocket 的一帧。
     *
     * 步骤：QJsonDocument::Compact → UTF-8 字节 → 在前面加 4 字节大端长度。
     * 空对象也会编码成 `{"..."}` 的合法 JSON，长度为正文字节数，不是字符数。
     *
     * 调用方：
     *   - Client::request：用户请求
     *   - TcpServer::incomingConnection：同步回包
     *   - TcpServer::pushChargeTicks：PUSH_CHARGE 推送
     *
     * @param obj  已填好的信封（请求或响应）。本函数不校验字段是否齐全。
     * @return     至少 4 字节；随后是 JSON。不要再自己加换行或长度。
     */
    static QByteArray pack(const QJsonObject &obj);

    /**
     * 把 TCP 刚 readAll() 到的字节追加进本连接的缓冲，并立刻尝试拆出完整包。
     *
     * 一次调用可能使 hasPacket() 从 false 变为 true 多次（多包）或仍为 false（半包）。
     * chunk 为空时是空操作。不要跨连接共用同一个 Protocol 实例。
     *
     * @param chunk  本段套接字读到的原始字节，可以是任意长度。
     */
    void append(const QByteArray &chunk);

    /**
     * 缓冲里是否已经有拆好、还没被 nextPacket() 取走的 JSON 对象。
     * 接收循环标准写法：append(readAll()); while (hasPacket()) 处理 nextPacket()。
     */
    bool hasPacket() const { return !ready_.isEmpty(); }

    /**
     * 取出队列最前面一张完整 JSON 对象（FIFO）。
     *
     
     * 没有包时返回空 QJsonObject（isEmpty()==true）。调用方应先看 hasPacket()，
     * 否则会把「队列空」和「对端发来 {}」搞混（对端发 {} 极少见，业务上都有 type）。
     *
     * @return  一帧对应的对象；取出后从 ready_ 删除。
     */
    QJsonObject nextPacket();

private:
    QByteArray buf_;           ///< 还没凑齐一帧的原始字节（可能含半个长度或半个 JSON）
    QList<QJsonObject> ready_; ///< 已经拆好、等待 nextPacket 取走的对象队列

    /**
     * 在 buf_ 上循环切帧，直到不够一帧或遇到非法长度。
     *
     * 规则：
     *   1. 不足 4 字节：停止，等下次 append。
     *   2. 长度 len==0 或 len>1MiB：buf_.clear() 并 return（整段连接的残留都丢掉）。
     *   3. 已有 4+len 字节：切开正文，fromJson；是对象则入 ready_，否则丢弃该帧。
     *   4. 切掉已消费字节后继续 while，处理粘在后面的下一帧。
     */
    void tryDecode();
};

#endif
