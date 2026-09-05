#ifndef CHARGEHUB_DATABASE_H
#define CHARGEHUB_DATABASE_H

/**
 * @file database.h
 * @brief SQLite 封装：管理端进程里唯一的写库入口。
 *
 * 【职责】打开 chargehub.db，提供 query / one / execute / transaction。
 * 【原理】每条 SQL 带互斥锁；transaction(fn) 在 BEGIN 里跑 fn，false 则 ROLLBACK。
 *         开充、停充、结算、充值必须走事务，避免「扣了钱订单没落」。
 * 【协作】只被 Dispatch 当写者。用户端禁止 open。大屏/预测用 Python 另开只读或只写分析表。
 *         表结构以 database/schema.sql 为准，open() 里 CREATE IF NOT EXISTS + 列迁移。
 * 【详见】docs/模块与协作说明.md
 */

#include <functional>

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
    bool open();                         ///< 打开库、建表、做列迁移
    QVector<QVariantMap> query(const QString &sql, const QVariantList &args = {}); ///< 多行
    QVariantMap one(const QString &sql, const QVariantList &args = {});            ///< 首行，没有则空
    /** 成功返回 lastInsertId（UPDATE 常为 0）；失败返回 -1。 */
    int execute(const QString &sql, const QVariantList &args = {});
    bool transaction(const std::function<bool()> &fn); ///< 事务，fn 返回 false 则回滚
    QString lastError() const { return lastError_; }
    QString path() const { return path_; }

private:
    QSqlDatabase conn();
    QString path_;
    QString lastError_;
    QRecursiveMutex mutex_;
};

#endif
