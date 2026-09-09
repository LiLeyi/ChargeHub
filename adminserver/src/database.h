#ifndef CHARGEHUB_DATABASE_H
#define CHARGEHUB_DATABASE_H

/**
 * @file database.h
 * @brief SQLite 封装：管理端进程里唯一的写库入口。
 *
 * 【职责】打开 chargehub.db，提供 query / one / execute / executeAffected / transaction。
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
    /** 记住库文件路径，此时还不打开文件。path 一般是可执行文件旁 data/chargehub.db。 */
    explicit Database(const QString &path);
    /** 关闭当前线程的 QSQLITE 连接并摘掉连接名，避免退出时 Qt 报警。 */
    ~Database();

    /**
     * 打开（或创建）库文件，建表、给旧库补列、必要时灌演示账号。
     * 已有联调数据不会被清空。WAL + foreign_keys + busy_timeout。
     * @return 文件打不开则 false。
     */
    bool open();

    /**
     * 多行 SELECT。args 按顺序绑定 ? 占位符。
     * 每一行变成「列名 → 值」的 QVariantMap。失败或没有行则空向量。
     */
    QVector<QVariantMap> query(const QString &sql, const QVariantList &args = {});

    /**
     * 只要第一行。内部调用 query。
     * 没有匹配行时返回空 QVariantMap（调用方用 isEmpty() 判断）。
     */
    QVariantMap one(const QString &sql, const QVariantList &args = {});

    /**
     * INSERT / UPDATE / DELETE。
     * @return 成功：INSERT 返回 lastInsertId，UPDATE/DELETE 常为 0；失败返回 -1，并写入 lastError_。
     */
    int execute(const QString &sql, const QVariantList &args = {});

    /**
     * 执行 UPDATE / DELETE 等需要检查命中行数的写语句。
     * @return 成功返回实际影响行数（可为 0），失败返回 -1，并写入 lastError_。
     */
    qint64 executeAffected(const QString &sql, const QVariantList &args = {});

    /**
     * 事务：BEGIN → 跑 fn → fn 返回 true 且 COMMIT 成功才算成功，否则 ROLLBACK。
     * 开充（插单+改桩）、停充、结算（改订单+扣余额）、充值必须走这里。
     */
    bool transaction(const std::function<bool()> &fn);

    /** 最近一次 execute / transaction 失败时的驱动错误文本。 */
    QString lastError() const { return lastError_; }
    /** 当前库文件绝对或相对路径。 */
    QString path() const { return path_; }

private:
    /**
     * 按线程取连接：名字 db_<线程id>。
     * GUI 线程和（若有）网络相关调用共用一把 mutex_，避免同时写坏文件。
     */
    QSqlDatabase conn();
    QString path_;
    QString lastError_;
    QRecursiveMutex mutex_;
};

#endif
