#ifndef CHARGEHUB_DATABASE_H
#define CHARGEHUB_DATABASE_H

/**
 * @file database.h
 * @brief SQLite 封装：全进程唯一写库入口。
 *
 * 库文件默认：可执行文件旁 data/chargehub.db。
 * 仅 adminserver 允许打开。userclient / dashboard / ml 不得当第二写者：
 *   - 用户端：只发 TCP；
 *   - 大屏：只读同一份文件；
 *   - 预测脚本：只写分析表，且须在管理端已启动、能接受 WAL 读者时运行。
 * 表结构以 database/schema.sql 为准，C++ 启动时 CREATE IF NOT EXISTS 并做列迁移。
 */

#include <QMutex>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QVector>

class Database {
public:
    explicit Database(const QString &path);
    ~Database();
    bool open();
    QVector<QVariantMap> query(const QString &sql, const QVariantList &args = {});
    QVariantMap one(const QString &sql, const QVariantList &args = {});
    int execute(const QString &sql, const QVariantList &args = {});
    QString path() const { return path_; }

private:
    QSqlDatabase conn();
    QString path_;
    QRecursiveMutex mutex_;
};

#endif
