/**
 * @file main.cpp
 * @brief 用户端入口：套样式后打开 UserWindow，不打开 SQLite。
 *
 * 样式管登录页和弹窗（UiSheet）。业务全在 UserWindow + Client。
 * 视觉按手机充电 App：竖屏、底栏、单列卡片；按钮保持轻量，避免撑满。
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
QWidget { font-family:"Microsoft YaHei","Noto Sans CJK SC"; font-size:13px; color:#0F172A; }
QMainWindow { background:#F4F7F8; }
QWidget#loginRoot { background:#0B3D3A; }
QWidget#formPane {
    background:#F4F7F8; border-top-left-radius:22px; border-top-right-radius:22px;
}
QFrame#brandPane { background:#0B3D3A; border:none; }
QFrame#topbar { background:#F4F7F8; border:none; }
QFrame#tabBar {
    background:#FFFFFF; border:none; border-top:1px solid #E7EEF0;
}
QFrame#toolbar, QFrame#searchCard {
    background:#FFFFFF; border:1px solid #E7EEF0; border-radius:14px;
}
QLineEdit, QSpinBox, QDoubleSpinBox {
    background:#FFFFFF; border:1px solid #E2E8F0; border-radius:10px;
    padding:7px 10px; color:#0F172A; min-height:18px;
    selection-background-color:#0D9488; selection-color:#FFFFFF;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus { border:1px solid #0D9488; }
QComboBox {
    background:#FFFFFF; border:1px solid #E2E8F0; border-radius:10px;
    padding:5px 22px 5px 10px; color:#0F172A; min-height:20px;
    selection-background-color:#0D9488; selection-color:#FFFFFF;
}
QComboBox:hover { border:1px solid #94A3B8; }
QComboBox:focus, QComboBox:on { border:1px solid #0D9488; }
QComboBox QAbstractItemView {
    background:#FFFFFF; color:#0F172A; border:1px solid #E2E8F0;
    outline:0; padding:2px;
    selection-background-color:#0D9488; selection-color:#FFFFFF;
}
QComboBox QAbstractItemView::item { min-height:30px; padding:4px 10px; }
QComboBox QAbstractItemView::item:hover { background:#F0FDFA; color:#0F766E; }
QPushButton {
    background:#0D9488; color:#FFFFFF; border:none; border-radius:10px;
    padding:5px 12px; font-weight:600; font-size:13px; min-height:30px;
}
QPushButton:hover { background:#0F766E; }
QPushButton:pressed { background:#115E59; }
QPushButton:disabled { background:#E5E7EB; color:#9CA3AF; }
QPushButton#ghost {
    background:#FFFFFF; color:#475569; border:1px solid #E2E8F0; font-weight:600;
}
QPushButton#ghost:hover { background:#F8FAFC; border:1px solid #CBD5E1; }
QPushButton#primary { min-height:34px; font-size:14px; border-radius:10px; font-weight:700; }
QPushButton#danger { background:#DC2626; }
QPushButton#danger:hover { background:#B91C1C; }
QPushButton#chip {
    background:#ECFDF5; color:#0F766E; border:none; font-weight:600;
    min-height:24px; padding:3px 10px; border-radius:12px; font-size:12px;
}
QPushButton#chip:hover { background:#CCFBF1; }
QPushButton#tabBtn {
    background:transparent; color:#94A3B8; border:none; border-radius:8px;
    min-height:36px; padding:4px 2px; font-size:11px; font-weight:600;
}
QPushButton#tabBtn:hover { background:#F0FDFA; color:#0F766E; }
QPushButton#tabBtn:checked { background:transparent; color:#0D9488; font-weight:800; }
QPushButton#link {
    background:transparent; color:#0D9488; border:none;
    min-height:26px; padding:2px 4px; font-size:13px; font-weight:600;
}
QPushButton#link:hover { background:transparent; color:#0F766E; }
QPushButton#star, QPushButton#starOn {
    background:transparent; border:none; min-width:32px; max-width:32px;
    min-height:32px; padding:0; font-size:22px; font-weight:400;
}
QPushButton#star { color:#CBD5E1; }
QPushButton#star:hover { color:#F59E0B; background:transparent; }
QPushButton#starOn { color:#F59E0B; }
QPushButton#starOn:hover { color:#D97706; background:transparent; }
QPushButton#postReview { min-width:88px; }
QPlainTextEdit {
    background:#FFFFFF; border:1px solid #E2E8F0; border-radius:10px;
    padding:8px 10px; color:#0F172A; min-height:88px;
    selection-background-color:#0D9488; selection-color:#FFFFFF;
}
QPlainTextEdit:focus { border:1px solid #0D9488; }
QLabel#hintBox {
    background:#F0FDFA; border:none; border-radius:10px;
    padding:8px 10px; color:#0F766E; font-size:12px;
}
QLabel#reviewTitle { font-size:18px; font-weight:800; color:#0F172A; }
QLabel#reviewAvg { color:#F59E0B; font-size:14px; font-weight:700; }
QLabel#reviewDiv { color:#94A3B8; font-size:12px; font-weight:600; }
QLabel#avatarDot {
    background:#0D9488; color:#FFFFFF; border-radius:16px;
    font-size:13px; font-weight:800;
}
QLabel#userAvatar {
    background:#0D9488; color:#FFFFFF; border-radius:28px;
    font-size:18px; font-weight:800;
}
QLabel#title { font-size:18px; font-weight:800; color:#0F172A; }
QLabel#h2 { font-size:14px; font-weight:800; color:#0F172A; padding:2px 2px 0 2px; }
QLabel#pageTitle { font-size:17px; font-weight:800; color:#0F172A; }
QLabel#muted { color:#64748B; font-size:12px; }
QLabel#brandMark { color:#F8FAFC; font-size:24px; font-weight:800; }
QLabel#brandSub { color:#99F6E4; font-size:12px; }
QLabel#brandFeat { color:#5EEAD4; font-size:12px; }
QLabel#logoMark {
    background:#0D9488; color:#FFFFFF; border-radius:12px;
    font-size:14px; font-weight:800;
}
QLabel#kpi {
    background:#0B3D3A; color:#5EEAD4; border-radius:16px;
    font-size:32px; font-weight:800; padding:14px 12px;
}
QLabel#kpiSub {
    background:#FFFFFF; color:#0F172A; border:1px solid #E7EEF0; border-radius:12px;
    font-size:15px; font-weight:800; padding:10px 8px;
}
QLabel#cardTitle { font-size:14px; font-weight:700; color:#0F172A; }
QLabel#pillOk {
    background:#ECFDF5; color:#047857; border-radius:8px;
    padding:2px 8px; font-size:11px; font-weight:700;
}
QLabel#pillOff {
    background:#FEF2F2; color:#B91C1C; border-radius:8px;
    padding:2px 8px; font-size:11px; font-weight:700;
}
QLabel#pillWarn {
    background:#FFFBEB; color:#B45309; border-radius:8px;
    padding:2px 8px; font-size:11px; font-weight:700;
}
QLabel#pillBusy {
    background:#EFF6FF; color:#1D4ED8; border-radius:8px;
    padding:2px 8px; font-size:11px; font-weight:700;
}
QFrame#card {
    background:#FFFFFF; border-radius:14px; border:1px solid #E7EEF0;
}
QScrollArea, QAbstractScrollArea { border:none; background:transparent; }
QScrollBar:vertical {
    background:transparent; width:5px; margin:4px 1px 4px 0;
}
QScrollBar::handle:vertical {
    background:#CBD5E1; min-height:28px; border-radius:3px;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; width:0; border:none; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:none; }
QScrollBar:horizontal { height:0; }
QStatusBar { background:transparent; color:#64748B; }
QDialog#uiSheet { background:#FFFFFF; border:1px solid #E2E8F0; }
QDialog#uiSheet QPushButton { min-height:32px; min-width:84px; padding:5px 12px; }
QFrame#uiSheetHead { background:#FFFFFF; border:none; border-bottom:1px solid #E6E8EC; }
QFrame#uiSheetFoot { background:#F8FAFC; border:none; border-top:1px solid #E6E8EC; }
QLabel#uiSheetTitle { font-size:18px; font-weight:800; color:#0F172A; }
QLabel#uiSheetHint { font-size:12px; color:#64748B; }
QLabel#uiField { font-size:12px; font-weight:700; color:#334155; }
QLabel#uiBadgeInfo, QLabel#uiBadgeWarn, QLabel#uiBadgeErr, QLabel#uiBadgeAsk {
    border-radius:14px; font-size:16px; font-weight:800;
}
QLabel#uiBadgeInfo { background:#CCFBF1; color:#0F766E; }
QLabel#uiBadgeWarn { background:#FEF3C7; color:#B45309; }
QLabel#uiBadgeErr { background:#FEE2E2; color:#B91C1C; }
QLabel#uiBadgeAsk { background:#DBEAFE; color:#1D4ED8; }
QMessageBox { background:#FFFFFF; color:#0F172A; min-width:280px; }
QMessageBox QLabel { font-size:13px; min-width:220px; padding:6px 4px; }
QMessageBox QPushButton { min-width:80px; min-height:30px; }
QToolTip { background:#0F172A; color:#F8FAFC; border:none; padding:5px 8px; }
)"));
    UserWindow w;
    w.show();
    return app.exec();
}
