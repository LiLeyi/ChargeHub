/**
 * @file main.cpp
 * @brief 用户端入口：套样式后打开 UserWindow，不打开 SQLite。
 *
 * 样式管登录页和弹窗（UiSheet）。业务全在 UserWindow + Client。
 */
#include "appstyle.h"
#include "userwindow.h"

#include <QApplication>
#include <QStatusBar>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setStyle(new ChargeHubStyle);
    app.setStyleSheet(QString::fromUtf8(R"(
QWidget { font-family:"Microsoft YaHei","Noto Sans CJK SC"; font-size:13px; color:#111827; }
QMainWindow { background:#F4F6F8; }
QWidget#formPane { background:#FFFFFF; }
QFrame#sidebar, QFrame#brandPane { background:#0F172A; border:none; }
QFrame#brandPane { border-left:4px solid #0D9488; }
QFrame#topbar { background:#FFFFFF; border:none; border-bottom:1px solid #E6E8EC; }
QFrame#toolbar {
    background:#FFFFFF; border:1px solid #E6E8EC; border-radius:12px;
}
QLineEdit, QSpinBox, QDoubleSpinBox {
    background:#FFFFFF; border:1px solid #E2E8F0; border-radius:10px;
    padding:10px 14px; color:#111827; min-height:26px;
    selection-background-color:#0D9488; selection-color:#FFFFFF;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus { border:1px solid #0D9488; }
QComboBox {
    background:#FFFFFF; border:1px solid #E2E8F0; border-radius:8px;
    padding:6px 28px 6px 12px; color:#111827; min-height:22px; min-width:108px;
    selection-background-color:#0D9488; selection-color:#FFFFFF;
}
QComboBox:hover { border:1px solid #94A3B8; }
QComboBox:focus, QComboBox:on { border:1px solid #0D9488; }
QComboBox QAbstractItemView {
    background:#FFFFFF; color:#111827; border:1px solid #E2E8F0;
    outline:0; padding:4px;
    selection-background-color:#0D9488; selection-color:#FFFFFF;
}
QComboBox QAbstractItemView::item { min-height:34px; padding:6px 12px; }
QComboBox QAbstractItemView::item:hover { background:#F0FDFA; color:#0F766E; }
QPushButton {
    background:#0D9488; color:#FFFFFF; border:none; border-radius:8px;
    padding:9px 16px; font-weight:700; min-height:36px;
}
QPushButton:hover { background:#0F766E; }
QPushButton:pressed { background:#115E59; }
QPushButton:disabled { background:#E5E7EB; color:#9CA3AF; }
QPushButton#ghost {
    background:#FFFFFF; color:#334155; border:1px solid #E2E8F0; font-weight:600;
}
QPushButton#ghost:hover { background:#F8FAFC; border:1px solid #CBD5E1; }
QPushButton#primary { min-height:46px; font-size:15px; }
QPushButton#danger { background:#DC2626; }
QPushButton#danger:hover { background:#B91C1C; }
QPushButton#star, QPushButton#starOn {
    background:transparent; border:none; min-width:40px; max-width:40px;
    min-height:40px; padding:0; font-size:28px; font-weight:400;
}
QPushButton#star { color:#CBD5E1; }
QPushButton#star:hover { color:#F59E0B; background:transparent; }
QPushButton#starOn { color:#F59E0B; }
QPushButton#starOn:hover { color:#D97706; background:transparent; }
QPushButton#postReview { min-width:128px; }
QPlainTextEdit {
    background:#FFFFFF; border:1px solid #E2E8F0; border-radius:8px;
    padding:10px 12px; color:#111827; min-height:120px;
    selection-background-color:#0D9488; selection-color:#FFFFFF;
}
QPlainTextEdit:focus { border:1px solid #0D9488; }
QLabel#hintBox {
    background:#F8FAFC; border:1px solid #E6E8EC; border-radius:8px;
    padding:10px 12px; color:#64748B; font-size:12px;
}
QLabel#reviewTitle { font-size:26px; font-weight:800; color:#1D4ED8; }
QLabel#reviewAvg { color:#F59E0B; font-size:16px; font-weight:700; }
QLabel#reviewDiv { color:#94A3B8; font-size:13px; font-weight:600; }
QLabel#avatarDot {
    background:#0D9488; color:#FFFFFF; border-radius:18px;
    font-size:14px; font-weight:800;
}
QLabel#userAvatar {
    background:#0D9488; color:#FFFFFF; border-radius:16px;
    font-size:22px; font-weight:800;
}
QLabel#title { font-size:22px; font-weight:800; color:#0F172A; }
QLabel#pageTitle { font-size:18px; font-weight:700; color:#0F172A; }
QLabel#muted { color:#64748B; font-size:12px; }
QLabel#brandMark { color:#F8FAFC; font-size:34px; font-weight:800; }
QLabel#sideBrand { color:#F8FAFC; font-size:18px; font-weight:800; }
QLabel#brandSub { color:#94A3B8; font-size:15px; }
QLabel#sideSub { color:#94A3B8; font-size:12px; }
QLabel#brandFeat { color:#5EEAD4; font-size:13px; }
QLabel#logoMark {
    background:#0D9488; color:#FFFFFF; border-radius:12px;
    font-size:15px; font-weight:800;
}
QLabel#kpi {
    background:#0F172A; color:#5EEAD4; border-radius:12px;
    font-size:28px; font-weight:800; padding:18px 20px;
}
QLabel#cardTitle { font-size:15px; font-weight:700; color:#0F172A; }
QLabel#pillOk {
    background:#ECFDF5; color:#047857; border-radius:8px;
    padding:4px 10px; font-size:12px; font-weight:700;
}
QLabel#pillOff {
    background:#FEF2F2; color:#B91C1C; border-radius:8px;
    padding:4px 10px; font-size:12px; font-weight:700;
}
QLabel#pillWarn {
    background:#FFFBEB; color:#B45309; border-radius:8px;
    padding:4px 10px; font-size:12px; font-weight:700;
}
QFrame#card {
    background:#FFFFFF; border-radius:12px; border:1px solid #E6E8EC;
}
QListWidget#nav {
    background:transparent; border:none; color:#CBD5E1; font-size:14px; outline:none;
}
QListWidget#nav::item { padding:11px 12px; border-radius:8px; margin:2px 4px; }
QListWidget#nav::item:selected { background:#0D9488; color:#FFFFFF; }
QListWidget#nav::item:hover:!selected { background:#1E293B; color:#F8FAFC; }
QScrollArea, QAbstractScrollArea { border:none; background:transparent; }
QScrollBar:vertical {
    background:#EEF2F6; width:12px; margin:2px 2px 2px 0;
}
QScrollBar::handle:vertical {
    background:#94A3B8; min-height:40px; border-radius:6px;
}
QScrollBar::handle:vertical:hover { background:#94A3B8; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; width:0; border:none; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:none; }
QScrollBar:horizontal {
    background:transparent; height:10px; margin:0 2px 1px 2px;
}
QScrollBar::handle:horizontal {
    background:#CBD5E1; min-width:32px; border-radius:5px;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { height:0; width:0; border:none; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background:none; }
QStatusBar { background:#0F172A; color:#94A3B8; padding:4px 12px; }
QDialog#uiSheet { background:#FFFFFF; border:1px solid #E2E8F0; }
QFrame#uiSheetHead { background:#FFFFFF; border:none; border-bottom:1px solid #E6E8EC; }
QFrame#uiSheetFoot { background:#F8FAFC; border:none; border-top:1px solid #E6E8EC; }
QLabel#uiSheetTitle { font-size:22px; font-weight:800; color:#0F172A; }
QLabel#uiSheetHint { font-size:14px; color:#64748B; line-height:22px; }
QLabel#uiField { font-size:13px; font-weight:700; color:#334155; }
QLabel#uiBadgeInfo, QLabel#uiBadgeWarn, QLabel#uiBadgeErr, QLabel#uiBadgeAsk {
    border-radius:16px; font-size:22px; font-weight:800;
}
QLabel#uiBadgeInfo { background:#CCFBF1; color:#0F766E; }
QLabel#uiBadgeWarn { background:#FEF3C7; color:#B45309; }
QLabel#uiBadgeErr { background:#FEE2E2; color:#B91C1C; }
QLabel#uiBadgeAsk { background:#DBEAFE; color:#1D4ED8; }
QMessageBox { background:#FFFFFF; color:#0F172A; min-width:480px; }
QMessageBox QLabel { font-size:14px; min-width:360px; padding:8px 4px; }
QMessageBox QPushButton { min-width:108px; min-height:40px; }
QToolTip { background:#0F172A; color:#F8FAFC; border:none; padding:6px 8px; }
)"));
    UserWindow w;
    w.show();
    return app.exec();
}
