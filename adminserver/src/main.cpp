/**
 * @file main.cpp
 * @brief 管理端入口：打开 SQLite、监听 TCP 8888、启动运营窗口。
 *
 * 本进程 = 全组唯一的「服务器」：写库 + 听 8888。同一台机器用文件锁禁止开第二份；
 * 不同电脑各开一份则会变成两套互不相通的系统，联调时只允许一台开管理端。
 */
#include "appstyle.h"
#include "database.h"
#include "dispatch.h"
#include "mainwindow.h"
#include "tcpserver.h"
#include "uidialog.h"

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QAbstractSocket>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLockFile>
#include <QPalette>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setStyle(new ChargeHubStyle);
    QPalette pal;
    pal.setColor(QPalette::Window, QColor("#09090B"));
    pal.setColor(QPalette::WindowText, QColor("#ECEDEE"));
    pal.setColor(QPalette::Base, QColor("#18181B"));
    pal.setColor(QPalette::AlternateBase, QColor("#1C1C1F"));
    pal.setColor(QPalette::Text, QColor("#ECEDEE"));
    pal.setColor(QPalette::Button, QColor("#18181B"));
    pal.setColor(QPalette::ButtonText, QColor("#ECEDEE"));
    pal.setColor(QPalette::BrightText, QColor("#FAFAFA"));
    pal.setColor(QPalette::Highlight, QColor("#14B8A6"));
    pal.setColor(QPalette::HighlightedText, QColor("#042F2E"));
    pal.setColor(QPalette::ToolTipBase, QColor("#18181B"));
    pal.setColor(QPalette::ToolTipText, QColor("#FAFAFA"));
    pal.setColor(QPalette::PlaceholderText, QColor("#71717A"));
    pal.setColor(QPalette::Light, QColor("#27272A"));
    pal.setColor(QPalette::Mid, QColor("#3F3F46"));
    pal.setColor(QPalette::Dark, QColor("#09090B"));
    app.setPalette(pal);
    app.setStyleSheet(QString::fromUtf8(R"(
QWidget { font-family:"Microsoft YaHei","Noto Sans CJK SC"; font-size:13px; color:#ECEDEE; }
QMainWindow, QDialog, QWidget#root, QWidget#page, QStackedWidget { background:#09090B; }
QFrame#sidebar { background:#0C0C0E; border:none; }
QLineEdit, QSpinBox, QDoubleSpinBox {
    background:#18181B; border:1px solid #3F3F46; border-radius:10px;
    padding:10px 14px; color:#FAFAFA; min-height:26px;
    selection-background-color:#14B8A6; selection-color:#052E2B;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus { border:1px solid #14B8A6; }
QComboBox {
    background:#18181B; border:1px solid #3F3F46; border-radius:8px;
    padding:6px 28px 6px 12px; color:#FAFAFA; min-height:22px; min-width:108px;
    selection-background-color:#14B8A6; selection-color:#052E2B;
}
QComboBox:hover { border:1px solid #71717A; }
QComboBox:focus, QComboBox:on { border:1px solid #14B8A6; }
QComboBox QAbstractItemView {
    background:#18181B; color:#FAFAFA; border:1px solid #3F3F46;
    outline:0; padding:4px;
    selection-background-color:#14B8A6; selection-color:#052E2B;
}
QComboBox QAbstractItemView::item { min-height:34px; padding:6px 12px; }
QComboBox QAbstractItemView::item:hover { background:#27272A; color:#FAFAFA; }
QPushButton {
    background:#14B8A6; border:none; border-radius:8px; padding:9px 16px;
    color:#042F2E; font-weight:700; min-height:36px;
}
QPushButton:hover { background:#2DD4BF; }
QPushButton#ghost {
    background:#18181B; color:#E4E4E7; border:1px solid #3F3F46; font-weight:600;
}
QPushButton#ghost:hover { background:#27272A; border:1px solid #52525B; }
QPushButton#primary { min-height:46px; font-size:15px; }
QPushButton#danger { background:#DC2626; color:#FFFFFF; }
QPushButton#danger:hover { background:#B91C1C; }
QListWidget#nav {
    background:transparent; border:none; padding:6px 4px; font-size:14px; outline:none;
}
QListWidget#nav::item { padding:11px 12px; border-radius:8px; margin:2px 4px; color:#A1A1AA; }
QListWidget#nav::item:selected { background:#14B8A6; color:#042F2E; font-weight:700; }
QListWidget#nav::item:hover:!selected { background:#18181B; color:#FAFAFA; }
QTableWidget {
    background:#18181B; gridline-color:#27272A; alternate-background-color:#1C1C1F;
    color:#ECEDEE; border:1px solid #27272A; border-radius:10px;
    selection-background-color:#134E4A; selection-color:#FAFAFA;
}
QHeaderView::section {
    background:#111113; color:#A1A1AA; padding:9px 8px; border:none;
    border-bottom:1px solid #27272A; font-weight:700;
}
QTableCornerButton::section { background:#111113; border:none; }
QLabel#title { font-size:18px; font-weight:700; color:#FAFAFA; }
QLabel#muted { color:#A1A1AA; font-size:12px; }
QLabel#brandMark { color:#FAFAFA; font-size:18px; font-weight:800; }
QLabel#brandSub { color:#71717A; font-size:12px; }
QLabel#kpi {
    background:#18181B; border:1px solid #27272A; border-radius:12px;
    padding:16px 18px; font-size:16px; font-weight:700; color:#5EEAD4;
}
QFrame#toolbar, QFrame#card {
    background:#18181B; border:1px solid #27272A; border-radius:12px;
}
QScrollArea { background:#09090B; border:none; }
QScrollArea > QWidget { background:#09090B; }
QAbstractItemView {
    background:#18181B; color:#ECEDEE; border:none;
    alternate-background-color:#1C1C1F;
}
QScrollBar:vertical {
    background:transparent; width:10px; margin:2px 1px 2px 0;
}
QScrollBar::handle:vertical {
    background:#3F3F46; min-height:32px; border-radius:5px;
}
QScrollBar::handle:vertical:hover { background:#71717A; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; width:0; border:none; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:none; }
QScrollBar:horizontal {
    background:transparent; height:10px; margin:0 2px 1px 2px;
}
QScrollBar::handle:horizontal {
    background:#3F3F46; min-width:32px; border-radius:5px;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { height:0; width:0; border:none; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background:none; }
QStatusBar { background:#0C0C0E; color:#A1A1AA; }
QDialog#uiSheet { background:#141417; border:1px solid #3F3F46; }
QFrame#uiSheetHead { background:#141417; border:none; border-bottom:1px solid #27272A; }
QFrame#uiSheetFoot { background:#111113; border:none; border-top:1px solid #27272A; }
QLabel#uiSheetTitle { font-size:22px; font-weight:800; color:#FAFAFA; }
QLabel#uiSheetHint { font-size:14px; color:#A1A1AA; line-height:22px; }
QLabel#uiField { font-size:13px; font-weight:700; color:#D4D4D8; }
QLabel#uiBadgeInfo, QLabel#uiBadgeWarn, QLabel#uiBadgeErr, QLabel#uiBadgeAsk {
    border-radius:16px; font-size:22px; font-weight:800;
}
QLabel#uiBadgeInfo { background:#134E4A; color:#5EEAD4; }
QLabel#uiBadgeWarn { background:#713F12; color:#FCD34D; }
QLabel#uiBadgeErr { background:#7F1D1D; color:#FECACA; }
QLabel#uiBadgeAsk { background:#1E3A5F; color:#93C5FD; }
QMessageBox { background:#141417; color:#FAFAFA; min-width:480px; }
QMessageBox QLabel { font-size:14px; min-width:360px; padding:8px 4px; }
QMessageBox QPushButton { min-width:108px; min-height:40px; }
QToolTip { background:#18181B; color:#FAFAFA; border:1px solid #3F3F46; padding:6px 8px; }
)"));

    const QString dbPath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("data/chargehub.db");
    QDir().mkpath(QFileInfo(dbPath).absolutePath());
    QLockFile instanceLock(QFileInfo(dbPath).absolutePath() + QStringLiteral("/chargehub.lock"));
    instanceLock.setStaleLockTime(30000);
    if (!instanceLock.tryLock(200)) {
        uiError(nullptr, QString::fromUtf8("已经在运行"),
                QString::fromUtf8(
                    "已经有一个管理端在运行。\n\n"
                    "管理端就是服务器：同一时刻全组只能开一份，否则会各写各的库、用户端连错机器。\n"
                    "请关掉另一份管理端，或到那台电脑上操作。\n"
                    "其他人请只开用户端，服务器地址填那台电脑底栏里的 IP:8888。"));
        return 1;
    }
    Database db(dbPath);
    if (!db.open()) {
        uiError(nullptr, QString::fromUtf8("无法启动"), QString::fromUtf8("无法打开数据库"));
        return 1;
    }
    Dispatch dispatch(&db);
    TcpServer server(&dispatch);
    if (!server.listen(QHostAddress::Any, 8888)) {
        uiError(nullptr, QString::fromUtf8("端口被占用"),
                QString::fromUtf8(
                    "端口 8888 被占用（通常是另一份管理端还在）。\n"
                    "请结束旧的 adminserver 后再开。全组联调只允许一台电脑开管理端。"));
        return 1;
    }

    UiSheet login(nullptr, QString::fromUtf8("运营管理平台"),
                  QString::fromUtf8("演示账号 admin / 123456，也可在下方注册新管理员。"));
    login.setWindowTitle(QString::fromUtf8("ChargeHub 运营后台"));
    login.polish(560, 640);
    auto *mark = new QLabel(QStringLiteral("CH"));
    mark->setObjectName("logoMark");
    mark->setFixedSize(52, 52);
    mark->setAlignment(Qt::AlignCenter);
    mark->setStyleSheet("background:#14B8A6;color:#042F2E;border-radius:14px;font-size:16px;font-weight:800;");
    auto *u = new QLineEdit("admin");
    u->setPlaceholderText(QString::fromUtf8("管理员账号（字母开头，3~16 位）"));
    auto *p = new QLineEdit("123456");
    p->setEchoMode(QLineEdit::Password);
    p->setPlaceholderText(QString::fromUtf8("密码（6~20 位）"));
    auto *p2 = new QLineEdit("123456");
    p2->setEchoMode(QLineEdit::Password);
    p2->setPlaceholderText(QString::fromUtf8("确认密码（仅注册需要）"));
    login.body()->addWidget(mark, 0, Qt::AlignLeft);
    login.body()->addSpacing(4);
    uiAddField(login.body(), QString::fromUtf8("账号"), u);
    uiAddField(login.body(), QString::fromUtf8("密码"), p);
    uiAddField(login.body(), QString::fromUtf8("确认密码"), p2);
    login.body()->addStretch(1);
    auto *regBtn = login.addGhost(QString::fromUtf8("注册新管理员"));
    auto *loginBtn = login.addPrimary(QString::fromUtf8("登  录"));
    QObject::connect(loginBtn, &QPushButton::clicked, [&]() {
        const QJsonObject r = dispatch.adminLogin(u->text().trimmed(), p->text());
        if (r.value("ok").toBool())
            login.accept();
        else
            uiWarn(&login, QString::fromUtf8("登录失败"), r.value("message").toString());
    });
    QObject::connect(regBtn, &QPushButton::clicked, [&]() {
        if (p->text() != p2->text()) {
            uiWarn(&login, QString::fromUtf8("注册失败"), QString::fromUtf8("两次输入的密码不一致"));
            return;
        }
        const QJsonObject r = dispatch.adminRegister(u->text().trimmed(), p->text());
        if (!r.value("ok").toBool()) {
            uiWarn(&login, QString::fromUtf8("注册失败"), r.value("message").toString());
            return;
        }
        uiInfo(&login, QString::fromUtf8("注册成功"), QString::fromUtf8("账号已创建，正在进入系统"));
        login.accept();
    });
    if (login.exec() != QDialog::Accepted)
        return 0;

    MainWindow w(&dispatch);
    QStringList ips;
    const auto addrs = QNetworkInterface::allAddresses();
    for (const auto &addr : addrs) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol && !addr.isLoopback())
            ips << addr.toString();
    }
    if (ips.isEmpty())
        ips << QStringLiteral("127.0.0.1");
    w.statusBar()->showMessage(
        QString::fromUtf8("用户端请填写  %1:8888    数据库 %2")
            .arg(ips.join("  /  "), db.path()));
    w.show();
    return app.exec();
}
