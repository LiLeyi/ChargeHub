/** 管理端回归：实际 Qt 窗口、业务服务与独立 SQLite 库，不连接线上服务。 */
#include <QtTest>
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QTableWidget>
#include <QTemporaryDir>
#include <limits>
#include <memory>

#include "database.h"
#include "dispatch.h"
#include "mainwindow.h"
#include "services/adminservice.h"
#include "services/analyticsservice.h"
#include "services/chargeservice.h"
#include "services/reservationservice.h"
#include "services/sessionservice.h"

class AdminOperationsTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void successfulOperationsStillWork();
    void registrationReportsWriteFailure();
    void stationUpdateRollsBackOnWriteFailure();
    void tariffFailurePreservesPreviousRules();
    void pileAuditFailureRollsBack_data();
    void pileAuditFailureRollsBack();
    void stationCreationRollsBackWhenTariffFails();
    void dispatchPlanDoesNotPartiallyChangePrices_data();
    void dispatchPlanDoesNotPartiallyChangePrices();
    void invalidCoordinatesAreRejected_data();
    void invalidCoordinatesAreRejected();
    void emptyPileStatisticsAreZero();
    void refreshKeepsSelectedEntityAndClearsMissingTargets();
    void stationNameEditPreservesOriginalCoordinates();
    void adminOrdersCanBeFoundByPileNumber();
    void freezeAndUnfreezeReturnActualResults();
    void userAuditFailureRollsBack_data();
    void userAuditFailureRollsBack();

private:
    QVariantMap stationData() const;
    int count(const QString &table) const;
    int firstStationId() const;
    static QTableWidget *tableWithHeaders(MainWindow &window, const QString &first,
                                        const QString &second);
    static int rowWithId(QTableWidget *table, int id, bool idInRole);
    static int currentId(QTableWidget *table, bool idInRole);

    std::unique_ptr<QTemporaryDir> temporaryDirectory;
    std::unique_ptr<Database> database;
    std::unique_ptr<SessionService> sessions;
    std::unique_ptr<ReservationService> reservations;
    std::unique_ptr<ChargeService> charges;
    std::unique_ptr<AdminService> admin;
};

void AdminOperationsTest::init()
{
    temporaryDirectory = std::make_unique<QTemporaryDir>();
    QVERIFY(temporaryDirectory->isValid());
    database = std::make_unique<Database>(temporaryDirectory->filePath("admin.db"));
    QVERIFY2(database->open(), qPrintable(database->lastError()));
    sessions = std::make_unique<SessionService>(database.get());
    reservations = std::make_unique<ReservationService>(database.get());
    charges = std::make_unique<ChargeService>(database.get(), sessions.get(), reservations.get());
    admin = std::make_unique<AdminService>(database.get(), sessions.get(), charges.get());
}

void AdminOperationsTest::cleanup()
{
    admin.reset();
    charges.reset();
    reservations.reset();
    sessions.reset();
    // Database 的连接名按线程生成，必须先关闭旧连接再开始下一用例。
    database.reset();
    temporaryDirectory.reset();
}

QVariantMap AdminOperationsTest::stationData() const
{
    return {{"name", QStringLiteral("回归测试电站")},
            {"address", QStringLiteral("回归测试路 1 号")},
            {"lng", 116.350123456}, {"lat", 39.960123456},
            {"pricePerKwh", 1.30}, {"pileCount", 2}};
}

int AdminOperationsTest::count(const QString &table) const
{
    return database->one("SELECT COUNT(*) AS n FROM " + table).value("n").toInt();
}

int AdminOperationsTest::firstStationId() const
{
    return database->one("SELECT id FROM station ORDER BY id LIMIT 1").value("id").toInt();
}

void AdminOperationsTest::successfulOperationsStillWork()
{
    QVERIFY(admin->adminRegister("testAdmin", "test1234").value("ok").toBool());
    QVERIFY(admin->adminLogin("testAdmin", "test1234").value("ok").toBool());
    const int stationId = admin->addStation(stationData());
    QVERIFY(stationId > 0);
    QCOMPARE(database->one("SELECT COUNT(*) AS n FROM pile WHERE station_id=?", {stationId})
                 .value("n").toInt(), 2);
    QCOMPARE(database->one("SELECT COUNT(*) AS n FROM tariff_rule WHERE station_id=?", {stationId})
                 .value("n").toInt(), 4);
    auto data = stationData();
    data["name"] = QStringLiteral("已修改的测试电站");
    QCOMPARE(admin->updateStation(stationId, data), QStringLiteral("电站已更新"));
    QCOMPARE(database->one("SELECT name FROM station WHERE id=?", {stationId})
                 .value("name").toString(), data.value("name").toString());
    const int pileId = database->one("SELECT id FROM pile WHERE station_id=? LIMIT 1", {stationId})
                           .value("id").toInt();
    admin->markPileFault(pileId);
    QCOMPARE(database->one("SELECT status FROM pile WHERE id=?", {pileId})
                 .value("status").toString(), QStringLiteral("故障"));
    admin->rebootPile(pileId);
    const auto restored = database->one("SELECT status, fault_code, fault_at FROM pile WHERE id=?", {pileId});
    QCOMPARE(restored.value("status").toString(), QStringLiteral("闲置"));
    QVERIFY(restored.value("fault_code").toString().isEmpty());
    QVERIFY(restored.value("fault_at").toString().isEmpty());
}

void AdminOperationsTest::registrationReportsWriteFailure()
{
    const int before = count("admin");
    QVERIFY(database->execute("CREATE TRIGGER reject_admin BEFORE INSERT ON admin "
                              "BEGIN SELECT RAISE(ABORT, 'test registration failure'); END") >= 0);
    const auto response = admin->adminRegister("testAdmin", "test1234");
    QVERIFY(!response.value("ok").toBool());
    QVERIFY(!response.value("message").toString().isEmpty());
    QCOMPARE(count("admin"), before);
}

void AdminOperationsTest::stationUpdateRollsBackOnWriteFailure()
{
    const int stationId = firstStationId();
    QVERIFY(stationId > 0);
    const auto before = database->one("SELECT * FROM station WHERE id=?", {stationId});
    const int auditBefore = count("audit_log");
    QVERIFY(database->execute("CREATE TRIGGER reject_station_update BEFORE UPDATE ON station "
                              "BEGIN SELECT RAISE(ABORT, 'test station update failure'); END") >= 0);
    const QString response = admin->updateStation(stationId, stationData());
    QVERIFY(!response.isEmpty());
    QVERIFY(response != QStringLiteral("电站已更新"));
    QVERIFY(database->one("SELECT * FROM station WHERE id=?", {stationId}) == before);
    QCOMPARE(count("audit_log"), auditBefore);
}

void AdminOperationsTest::tariffFailurePreservesPreviousRules()
{
    const int stationId = firstStationId();
    const auto before = database->query("SELECT * FROM tariff_rule WHERE station_id=? ORDER BY id", {stationId});
    QVERIFY(!before.isEmpty());
    const int auditBefore = count("audit_log");
    QVERIFY(database->execute("CREATE TRIGGER reject_tariff BEFORE INSERT ON tariff_rule "
                              "BEGIN SELECT RAISE(ABORT, 'test tariff failure'); END") >= 0);
    const QString response = admin->applyDefaultTariff(stationId);
    QVERIFY(!response.isEmpty());
    QVERIFY(!response.startsWith(QStringLiteral("已按基准电价启用")));
    QVERIFY(database->query("SELECT * FROM tariff_rule WHERE station_id=? ORDER BY id", {stationId}) == before);
    QCOMPARE(count("audit_log"), auditBefore);
}

void AdminOperationsTest::pileAuditFailureRollsBack_data()
{
    QTest::addColumn<bool>("reboot");
    QTest::newRow("reboot") << true;
    QTest::newRow("markFault") << false;
}

void AdminOperationsTest::pileAuditFailureRollsBack()
{
    QFETCH(bool, reboot);
    const int pileId = database->one("SELECT id FROM pile ORDER BY id LIMIT 1").value("id").toInt();
    QVERIFY(pileId > 0);
    QVERIFY(database->execute("UPDATE pile SET status=?, fault_code=?, fault_at=? WHERE id=?",
                              {reboot ? QStringLiteral("故障") : QStringLiteral("闲置"),
                               QStringLiteral("TEST"), QStringLiteral("2026-09-08 10:00:00"), pileId}) >= 0);
    const auto before = database->one("SELECT * FROM pile WHERE id=?", {pileId});
    const int auditBefore = count("audit_log");
    QVERIFY(database->execute("CREATE TRIGGER reject_audit BEFORE INSERT ON audit_log "
                              "BEGIN SELECT RAISE(ABORT, 'test audit failure'); END") >= 0);
    const QString response = reboot ? admin->rebootPile(pileId) : admin->markPileFault(pileId);
    QVERIFY(!response.isEmpty());
    QVERIFY(!response.startsWith(QStringLiteral("重启指令已发送")));
    QVERIFY(!response.startsWith(QStringLiteral("已标记为故障")));
    QVERIFY(database->one("SELECT * FROM pile WHERE id=?", {pileId}) == before);
    QCOMPARE(count("audit_log"), auditBefore);
}

void AdminOperationsTest::stationCreationRollsBackWhenTariffFails()
{
    const int stationsBefore = count("station");
    const int pilesBefore = count("pile");
    const int tariffsBefore = count("tariff_rule");
    const int auditsBefore = count("audit_log");
    QVERIFY(database->execute("CREATE TRIGGER reject_tariff BEFORE INSERT ON tariff_rule "
                              "BEGIN SELECT RAISE(ABORT, 'test new station tariff failure'); END") >= 0);
    QVERIFY(admin->addStation(stationData()) <= 0);
    QCOMPARE(count("station"), stationsBefore);
    QCOMPARE(count("pile"), pilesBefore);
    QCOMPARE(count("tariff_rule"), tariffsBefore);
    QCOMPARE(count("audit_log"), auditsBefore);
}

void AdminOperationsTest::dispatchPlanDoesNotPartiallyChangePrices_data()
{
    QTest::addColumn<QString>("scenario");
    QTest::newRow("auditFailureRollsBack") << QStringLiteral("auditFailure");
    QTest::newRow("alreadyAdoptedIsUnchanged") << QStringLiteral("alreadyAdopted");
    QTest::newRow("duplicateStationNameIsRejected") << QStringLiteral("duplicateName");
}

void AdminOperationsTest::dispatchPlanDoesNotPartiallyChangePrices()
{
    QFETCH(QString, scenario);
    const auto data = stationData();
    const int stationId = admin->addStation(data);
    QVERIFY(stationId > 0);
    const int planId = database->execute(
        "INSERT INTO dispatch_plan(station,recommend,priority,reason,created_at) VALUES(?,?,?,?,?)",
        {data.value("name"), 80.0, 1, QStringLiteral("回归测试建议"),
         QStringLiteral("2026-09-08 10:00:00")});
    QVERIFY(planId > 0);
    if (scenario == QStringLiteral("alreadyAdopted")) {
        QVERIFY(admin->adoptDispatchPlan(planId).startsWith(QStringLiteral("已采纳：")));
        QCOMPARE(database->one("SELECT adopted FROM dispatch_plan WHERE id=?", {planId})
                     .value("adopted").toInt(), 1);
    } else if (scenario == QStringLiteral("duplicateName")) {
        QVERIFY(database->execute("INSERT INTO station(name,address,lng,lat,price_per_kwh) VALUES(?,?,?,?,?)",
                                  {data.value("name"), QStringLiteral("另一个同名电站"),
                                   120.0, 30.0, 1.50}) > 0);
    } else {
        QVERIFY(database->execute("CREATE TRIGGER reject_plan_audit BEFORE INSERT ON audit_log "
                                  "BEGIN SELECT RAISE(ABORT, 'test plan audit failure'); END") >= 0);
    }
    const auto tariffsBefore = database->query("SELECT * FROM tariff_rule ORDER BY id");
    const auto planBefore = database->one("SELECT * FROM dispatch_plan WHERE id=?", {planId});
    const int auditsBefore = count("audit_log");
    const QString response = admin->adoptDispatchPlan(planId);
    QVERIFY(!response.isEmpty());
    QVERIFY(!response.startsWith(QStringLiteral("已采纳：")));
    QVERIFY(database->query("SELECT * FROM tariff_rule ORDER BY id") == tariffsBefore);
    QVERIFY(database->one("SELECT * FROM dispatch_plan WHERE id=?", {planId}) == planBefore);
    QCOMPARE(count("audit_log"), auditsBefore);
}

void AdminOperationsTest::invalidCoordinatesAreRejected_data()
{
    QTest::addColumn<QString>("field");
    QTest::addColumn<QVariant>("value");
    QTest::newRow("nonNumericLongitude") << QStringLiteral("lng") << QVariant(QStringLiteral("abc"));
    QTest::newRow("nonNumericLatitude") << QStringLiteral("lat") << QVariant(QStringLiteral("abc"));
    QTest::newRow("nan") << QStringLiteral("lng") << QVariant(std::numeric_limits<double>::quiet_NaN());
    QTest::newRow("positiveInfinity") << QStringLiteral("lat") << QVariant(std::numeric_limits<double>::infinity());
    QTest::newRow("negativeInfinity") << QStringLiteral("lng") << QVariant(-std::numeric_limits<double>::infinity());
    QTest::newRow("longitudeTooHigh") << QStringLiteral("lng") << QVariant(180.01);
    QTest::newRow("longitudeTooLow") << QStringLiteral("lng") << QVariant(-180.01);
    QTest::newRow("latitudeTooHigh") << QStringLiteral("lat") << QVariant(90.01);
    QTest::newRow("latitudeTooLow") << QStringLiteral("lat") << QVariant(-90.01);
}

void AdminOperationsTest::invalidCoordinatesAreRejected()
{
    QFETCH(QString, field);
    QFETCH(QVariant, value);
    auto data = stationData();
    data[field] = value;
    const int stationsBefore = count("station");
    const int auditsBefore = count("audit_log");
    QVERIFY(admin->addStation(data) <= 0);
    QCOMPARE(count("station"), stationsBefore);
    const int stationId = firstStationId();
    const auto before = database->one("SELECT * FROM station WHERE id=?", {stationId});
    QVERIFY(admin->updateStation(stationId, data) != QStringLiteral("电站已更新"));
    QVERIFY(database->one("SELECT * FROM station WHERE id=?", {stationId}) == before);
    QCOMPARE(count("audit_log"), auditsBefore);
}

void AdminOperationsTest::emptyPileStatisticsAreZero()
{
    QVERIFY(database->execute("DELETE FROM charge_order") >= 0);
    QVERIFY(database->execute("DELETE FROM reservation") >= 0);
    QVERIFY(database->execute("DELETE FROM station_review") >= 0);
    QVERIFY(database->execute("DELETE FROM review_doc") >= 0);
    QVERIFY(database->execute("DELETE FROM pile") >= 0);
    AnalyticsService analytics(database.get());
    const auto stats = analytics.pileStatusStats();
    QCOMPARE(stats.value("total").toInt(), 0);
    const auto items = stats.value("items").toArray();
    QCOMPARE(items.size(), 3);
    for (const auto &item : items) {
        QCOMPARE(item.toObject().value("count").toInt(), 0);
        QCOMPARE(item.toObject().value("percent").toDouble(), 0.0);
    }
}

QTableWidget *AdminOperationsTest::tableWithHeaders(MainWindow &window, const QString &first,
                                                   const QString &second)
{
    for (auto *table : window.findChildren<QTableWidget *>()) {
        if (table->columnCount() >= 2 && table->horizontalHeaderItem(0)
            && table->horizontalHeaderItem(1)
            && table->horizontalHeaderItem(0)->text() == first
            && table->horizontalHeaderItem(1)->text() == second)
            return table;
    }
    return nullptr;
}

int AdminOperationsTest::rowWithId(QTableWidget *table, int id, bool idInRole)
{
    for (int row = 0; row < table->rowCount(); ++row) {
        auto *item = table->item(row, 0);
        if (item && (idInRole ? item->data(Qt::UserRole).toInt() : item->text().toInt()) == id)
            return row;
    }
    return -1;
}

int AdminOperationsTest::currentId(QTableWidget *table, bool idInRole)
{
    if (table->currentRow() < 0)
        return 0;
    auto *item = table->item(table->currentRow(), 0);
    return item ? (idInRole ? item->data(Qt::UserRole).toInt() : item->text().toInt()) : 0;
}

void AdminOperationsTest::refreshKeepsSelectedEntityAndClearsMissingTargets()
{
    const QString time = QStringLiteral("2026-09-08 10:00:00");
    const auto insertUser = [&](const QString &phone) {
        return database->execute("INSERT INTO user(phone,nickname,created_at) VALUES(?,?,?)",
                                 {phone, QStringLiteral("选择测试用户"), time});
    };
    const int userId = insertUser(QStringLiteral("19900000001"));
    QVERIFY(userId > 0);
    const int pileId = database->one("SELECT id FROM pile ORDER BY id LIMIT 1").value("id").toInt();
    QVERIFY(pileId > 0);
    const auto insertOrder = [&](const QString &number, int owner) {
        return database->execute("INSERT INTO charge_order(order_no,user_id,pile_id,status,start_time,created_at) "
                                 "VALUES(?,?,?,?,?,?)",
                                 {number, owner, pileId, QStringLiteral("已完成"), time, time});
    };
    const int orderId = insertOrder(QStringLiteral("TEST-SELECT-A"), userId);
    QVERIFY(orderId > 0);
    Dispatch dispatch(database.get());
    MainWindow window(&dispatch);
    for (auto *timer : window.findChildren<QTimer *>())
        timer->stop();
    auto *users = tableWithHeaders(window, QStringLiteral("ID"), QStringLiteral("手机号"));
    auto *orders = tableWithHeaders(window, QStringLiteral("订单号"), QStringLiteral("手机号"));
    QVERIFY(users);
    QVERIFY(orders);
    const int userRow = rowWithId(users, userId, false);
    const int orderRow = rowWithId(orders, orderId, true);
    QVERIFY(userRow >= 0);
    QVERIFY(orderRow >= 0);
    users->setCurrentCell(userRow, 0);
    orders->setCurrentCell(orderRow, 0);
    const int newerUser = insertUser(QStringLiteral("19900000002"));
    QVERIFY(newerUser > userId);
    QVERIFY(insertOrder(QStringLiteral("TEST-SELECT-B"), newerUser) > orderId);
    QVERIFY(QMetaObject::invokeMethod(&window, "refresh", Qt::DirectConnection));
    QCOMPARE(currentId(users, false), userId);
    QCOMPARE(currentId(orders, true), orderId);
    QCOMPARE(users->currentRow(), userRow + 1);
    QCOMPARE(orders->currentRow(), orderRow + 1);
    QVERIFY(database->execute("DELETE FROM charge_order WHERE id=?", {orderId}) >= 0);
    QVERIFY(database->execute("DELETE FROM user WHERE id=?", {userId}) >= 0);
    QVERIFY(QMetaObject::invokeMethod(&window, "refresh", Qt::DirectConnection));
    QCOMPARE(users->currentRow(), -1);
    QCOMPARE(orders->currentRow(), -1);
    QVERIFY(users->selectedItems().isEmpty());
    QVERIFY(orders->selectedItems().isEmpty());
}

void AdminOperationsTest::stationNameEditPreservesOriginalCoordinates()
{
    const auto data = stationData();
    const int stationId = admin->addStation(data);
    QVERIFY(stationId > 0);
    const double originalLongitude = data.value("lng").toDouble();
    const double originalLatitude = data.value("lat").toDouble();
    const QString updatedName = QStringLiteral("只修改站名的测试电站");
    Dispatch dispatch(database.get());
    MainWindow window(&dispatch);
    for (auto *timer : window.findChildren<QTimer *>())
        timer->stop();
    auto *stations = tableWithHeaders(window, QStringLiteral("ID"), QStringLiteral("站名"));
    QVERIFY(stations);
    const int row = rowWithId(stations, stationId, false);
    QVERIFY(row >= 0);
    stations->setCurrentCell(row, 0);
    const auto *coordinates = stations->item(row, 3);
    QVERIFY(coordinates);
    QVERIFY(coordinates->data(Qt::UserRole).toDouble() == originalLongitude);
    QVERIFY(coordinates->data(Qt::UserRole + 1).toDouble() == originalLatitude);

    bool formSubmitted = false;
    bool successPromptSeen = false;
    bool timedOut = false;
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer dialogDriver;
    connect(&dialogDriver, &QTimer::timeout, &window, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        if (elapsed.elapsed() > 2000) {
            timedOut = true;
            dialog->reject();
            return;
        }
        if (!formSubmitted) {
            for (auto *field : dialog->findChildren<QLineEdit *>()) {
                if (field->text() == data.value("name").toString()) {
                    field->setText(updatedName);
                    formSubmitted = true;
                    // 模态窗口期间仍可能触发管理端刷新，模拟真实定时器路径。
                    QMetaObject::invokeMethod(&window, "refresh", Qt::DirectConnection);
                    dialog->accept();
                    return;
                }
            }
        } else {
            for (auto *label : dialog->findChildren<QLabel *>())
                successPromptSeen |= label->text().contains(QStringLiteral("电站已更新"));
            dialog->accept();
        }
    });
    dialogDriver.start(1);
    const bool invoked = QMetaObject::invokeMethod(&window, "editStation", Qt::DirectConnection);
    dialogDriver.stop();
    QVERIFY(invoked);
    QVERIFY(!timedOut);
    QVERIFY(formSubmitted);
    QVERIFY(successPromptSeen);
    const auto stored = database->one("SELECT name,lng,lat FROM station WHERE id=?", {stationId});
    QCOMPARE(stored.value("name").toString(), updatedName);
    // 精确比较，避免 QCOMPARE(double) 的模糊容差掩盖坐标截断。
    QVERIFY(stored.value("lng").toDouble() == originalLongitude);
    QVERIFY(stored.value("lat").toDouble() == originalLatitude);
}

void AdminOperationsTest::adminOrdersCanBeFoundByPileNumber()
{
    const auto sample = database->one("SELECT p.pile_no FROM charge_order o JOIN pile p ON p.id=o.pile_id "
                                      "ORDER BY o.id DESC LIMIT 1");
    QVERIFY(!sample.isEmpty());
    const QString pileNumber = sample.value("pile_no").toString();
    QVERIFY(!pileNumber.isEmpty());
    const auto orders = admin->listAdminOrders(pileNumber);
    QVERIFY(!orders.isEmpty());
    for (const auto &order : orders)
        QCOMPARE(order.value("pile_no").toString(), pileNumber);
    QVERIFY(admin->listAdminOrders(QStringLiteral("NO-SUCH-PILE-REGRESSION")).isEmpty());
}

void AdminOperationsTest::freezeAndUnfreezeReturnActualResults()
{
    const int userId = database->execute("INSERT INTO user(phone,nickname,created_at) VALUES(?,?,?)",
                                         {QStringLiteral("19900000003"), QStringLiteral("冻结测试用户"),
                                          QStringLiteral("2026-09-08 10:00:00")});
    QVERIFY(userId > 0);
    const int auditsBefore = count("audit_log");
    QCOMPARE(admin->freezeUser(userId, true), QStringLiteral("用户已冻结"));
    QCOMPARE(database->one("SELECT status FROM user WHERE id=?", {userId})
                 .value("status").toString(), QStringLiteral("冻结"));
    QCOMPARE(admin->freezeUser(userId, false), QStringLiteral("用户已解冻"));
    const auto restored = database->one("SELECT status,close_reason,closed_at FROM user WHERE id=?", {userId});
    QCOMPARE(restored.value("status").toString(), QStringLiteral("正常"));
    QVERIFY(!restored.value("close_reason").isNull());
    QVERIFY(!restored.value("closed_at").isNull());
    QVERIFY(restored.value("close_reason").toString().isEmpty());
    QVERIFY(restored.value("closed_at").toString().isEmpty());
    QCOMPARE(count("audit_log"), auditsBefore + 2);
}

void AdminOperationsTest::userAuditFailureRollsBack_data()
{
    QTest::addColumn<bool>("freeze");
    QTest::newRow("freeze") << true;
    QTest::newRow("unfreeze") << false;
}

void AdminOperationsTest::userAuditFailureRollsBack()
{
    QFETCH(bool, freeze);
    const int userId = database->execute("INSERT INTO user(phone,nickname,status,created_at) VALUES(?,?,?,?)",
                                         {QStringLiteral("19900000004"), QStringLiteral("冻结失败测试用户"),
                                          freeze ? QStringLiteral("正常") : QStringLiteral("冻结"),
                                          QStringLiteral("2026-09-08 10:00:00")});
    QVERIFY(userId > 0);
    const auto before = database->one("SELECT * FROM user WHERE id=?", {userId});
    const int auditsBefore = count("audit_log");
    QVERIFY(database->execute("CREATE TRIGGER reject_user_audit BEFORE INSERT ON audit_log "
                              "BEGIN SELECT RAISE(ABORT, 'test freeze audit failure'); END") >= 0);
    const QString response = admin->freezeUser(userId, freeze);
    QVERIFY(!response.isEmpty());
    QVERIFY(response != QStringLiteral("用户已冻结"));
    QVERIFY(response != QStringLiteral("用户已解冻"));
    QVERIFY(database->one("SELECT * FROM user WHERE id=?", {userId}) == before);
    QCOMPARE(count("audit_log"), auditsBefore);
}

QTEST_MAIN(AdminOperationsTest)
#include "testadminoperations.moc"
