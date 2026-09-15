#ifndef CHARGEHUB_APPSTYLE_H
#define CHARGEHUB_APPSTYLE_H

/**
 * @file appstyle.h
 * @brief 统一 Fusion。关闭 ComboBox 原生弹出层，避免 WSLg/虚拟机里下拉错位。
 *
 * 管理端、用户端 main 里先 chargehubPrepareIme()，再 app.setStyle(new ChargeHubStyle)。
 */

#include <QFile>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOption>
#include <QWidget>

/** WSLg 的 Windows 输入法进不了 Qt/xcb。有 ibus 时在 QApplication 之前挂上。 */
inline void chargehubPrepareIme()
{
#ifdef Q_OS_LINUX
    if (!QFile::exists(QStringLiteral("/usr/bin/ibus-daemon")))
        return;
    const auto setIfEmpty = [](const char *key, const char *val) {
        if (!qgetenv(key).isEmpty())
            return;
        qputenv(key, val);
    };
    setIfEmpty("QT_IM_MODULE", "ibus");
    setIfEmpty("GTK_IM_MODULE", "ibus");
    setIfEmpty("XMODIFIERS", "@im=ibus");
#else
    (void)0;
#endif
}

// 统一 Fusion 风格，下拉框不用系统原生弹出层
class ChargeHubStyle : public QProxyStyle {
public:
    ChargeHubStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}
    /** SH_ComboBox_Popup=0：下拉在控件下方画，不走系统原生弹出层。 */
    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                  QStyleHintReturn *returnData) const override
    {
        if (hint == QStyle::SH_ComboBox_Popup)
            return 0;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

#endif
