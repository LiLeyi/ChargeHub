#ifndef CHARGEHUB_APPSTYLE_H
#define CHARGEHUB_APPSTYLE_H

/**
 * @file appstyle.h
 * @brief 统一 Fusion 风格；下拉框不用系统原生弹出层，避免虚拟机里错位。
 */

#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOption>
#include <QWidget>

// 统一 Fusion 风格，下拉框不用系统原生弹出层
class ChargeHubStyle : public QProxyStyle {
public:
    ChargeHubStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}
    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                  QStyleHintReturn *returnData) const override
    {
        if (hint == QStyle::SH_ComboBox_Popup)
            return 0;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

#endif
