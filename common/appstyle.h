#ifndef CHARGEHUB_APPSTYLE_H
#define CHARGEHUB_APPSTYLE_H

/**
 * @file appstyle.h
 * @brief 统一 Fusion。关闭 ComboBox 原生弹出层，避免 WSLg/虚拟机里下拉错位。
 *
 * 管理端、用户端 main 里 app.setStyle(new ChargeHubStyle)。只影响观感。
 */

#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOption>
#include <QWidget>

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
