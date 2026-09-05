#ifndef CHARGEHUB_UIDIALOG_H
#define CHARGEHUB_UIDIALOG_H

/**
 * @file uidialog.h
 * @brief 统一弹窗：提示、确认、表单。只改界面，不改业务接口。
 */
#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSize>
#include <QVBoxLayout>
#include <QWidget>

/** 提示条颜色：信息 / 警告 / 错误 / 确认。 */
enum class UiTone { Info, Warn, Error, Ask };

inline QString uiBadgeText(UiTone tone)
{
    switch (tone) {
    case UiTone::Warn:
        return QString::fromUtf8("!");
    case UiTone::Error:
        return QString::fromUtf8("×");
    case UiTone::Ask:
        return QString::fromUtf8("?");
    default:
        return QString::fromUtf8("i");
    }
}

inline const char *uiBadgeName(UiTone tone)
{
    switch (tone) {
    case UiTone::Warn:
        return "uiBadgeWarn";
    case UiTone::Error:
        return "uiBadgeErr";
    case UiTone::Ask:
        return "uiBadgeAsk";
    default:
        return "uiBadgeInfo";
    }
}

class UiSheet : public QDialog {
public:
    explicit UiSheet(QWidget *parent, const QString &title, const QString &hint = QString())
        : QDialog(parent)
    {
        setObjectName(QStringLiteral("uiSheet"));
        setAttribute(Qt::WA_StyledBackground, true);
        setWindowTitle(title);
        setModal(true);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto *head = new QFrame;
        head->setObjectName(QStringLiteral("uiSheetHead"));
        head->setAttribute(Qt::WA_StyledBackground, true);
        auto *hl = new QVBoxLayout(head);
        hl->setContentsMargins(32, 28, 32, 20);
        hl->setSpacing(8);
        auto *t = new QLabel(title);
        t->setObjectName(QStringLiteral("uiSheetTitle"));
        t->setWordWrap(true);
        hl->addWidget(t);
        if (!hint.isEmpty()) {
            auto *h = new QLabel(hint);
            h->setObjectName(QStringLiteral("uiSheetHint"));
            h->setWordWrap(true);
            hl->addWidget(h);
        }

        bodyHost_ = new QWidget;
        body_ = new QVBoxLayout(bodyHost_);
        body_->setContentsMargins(32, 16, 32, 12);
        body_->setSpacing(12);

        auto *foot = new QFrame;
        foot->setObjectName(QStringLiteral("uiSheetFoot"));
        foot->setAttribute(Qt::WA_StyledBackground, true);
        footer_ = new QHBoxLayout(foot);
        footer_->setContentsMargins(32, 16, 32, 22);
        footer_->setSpacing(12);
        footer_->addStretch();

        root->addWidget(head, 0);
        root->addWidget(bodyHost_, 1);
        root->addWidget(foot, 0);
    }

    QVBoxLayout *body() const { return body_; }
    QHBoxLayout *footer() const { return footer_; }

    QPushButton *addGhost(const QString &text)
    {
        auto *b = new QPushButton(text);
        b->setObjectName(QStringLiteral("ghost"));
        b->setMinimumSize(120, 44);
        footer_->addWidget(b);
        return b;
    }

    QPushButton *addPrimary(const QString &text)
    {
        auto *b = new QPushButton(text);
        b->setDefault(true);
        b->setMinimumSize(128, 44);
        footer_->addWidget(b);
        return b;
    }

    QPushButton *addDanger(const QString &text)
    {
        auto *b = new QPushButton(text);
        b->setObjectName(QStringLiteral("danger"));
        b->setDefault(true);
        b->setMinimumSize(128, 44);
        footer_->addWidget(b);
        return b;
    }

    QPushButton *addCancel(const QString &text = QString::fromUtf8("取消"))
    {
        auto *b = addGhost(text);
        QObject::connect(b, &QPushButton::clicked, this, &QDialog::reject);
        return b;
    }

    QPushButton *addOk(const QString &text = QString::fromUtf8("确定"))
    {
        auto *b = addPrimary(text);
        QObject::connect(b, &QPushButton::clicked, this, &QDialog::accept);
        return b;
    }

    QPushButton *addClose(const QString &text = QString::fromUtf8("关闭"))
    {
        auto *b = addPrimary(text);
        QObject::connect(b, &QPushButton::clicked, this, &QDialog::reject);
        return b;
    }

    void polish(int w = 580, int h = 520)
    {
        setMinimumSize(w, qMin(h, 420));
        resize(w, h);
    }

private:
    QWidget *bodyHost_ = nullptr;
    QVBoxLayout *body_ = nullptr;
    QHBoxLayout *footer_ = nullptr;
};

inline QLabel *uiFieldLabel(const QString &text)
{
    auto *l = new QLabel(text);
    l->setObjectName(QStringLiteral("uiField"));
    return l;
}

inline void uiAddField(QVBoxLayout *lay, const QString &label, QWidget *field)
{
    lay->addWidget(uiFieldLabel(label));
    lay->addWidget(field);
}

inline bool uiPrompt(QWidget *parent, const QString &title, const QString &text, UiTone tone,
                     const QString &okText, const QString &cancelText = QString(), bool danger = false)
{
    QDialog dlg(parent);
    dlg.setObjectName(QStringLiteral("uiSheet"));
    dlg.setAttribute(Qt::WA_StyledBackground, true);
    dlg.setWindowTitle(title);
    dlg.setModal(true);

    auto *root = new QVBoxLayout(&dlg);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *head = new QFrame;
    head->setObjectName(QStringLiteral("uiSheetHead"));
    head->setAttribute(Qt::WA_StyledBackground, true);
    auto *hl = new QHBoxLayout(head);
    hl->setContentsMargins(32, 28, 32, 24);
    hl->setSpacing(18);

    auto *badge = new QLabel(uiBadgeText(tone));
    badge->setObjectName(QString::fromLatin1(uiBadgeName(tone)));
    badge->setFixedSize(52, 52);
    badge->setAlignment(Qt::AlignCenter);

    auto *texts = new QVBoxLayout;
    texts->setSpacing(8);
    auto *t = new QLabel(title);
    t->setObjectName(QStringLiteral("uiSheetTitle"));
    t->setWordWrap(true);
    auto *d = new QLabel(text);
    d->setObjectName(QStringLiteral("uiSheetHint"));
    d->setWordWrap(true);
    d->setMinimumWidth(400);
    texts->addWidget(t);
    texts->addWidget(d);

    hl->addWidget(badge, 0, Qt::AlignTop);
    hl->addLayout(texts, 1);

    auto *foot = new QFrame;
    foot->setObjectName(QStringLiteral("uiSheetFoot"));
    foot->setAttribute(Qt::WA_StyledBackground, true);
    auto *fl = new QHBoxLayout(foot);
    fl->setContentsMargins(32, 16, 32, 22);
    fl->setSpacing(12);
    fl->addStretch();

    if (!cancelText.isEmpty()) {
        auto *c = new QPushButton(cancelText);
        c->setObjectName(QStringLiteral("ghost"));
        c->setMinimumSize(120, 44);
        QObject::connect(c, &QPushButton::clicked, &dlg, &QDialog::reject);
        fl->addWidget(c);
    }
    auto *ok = new QPushButton(okText);
    if (danger)
        ok->setObjectName(QStringLiteral("danger"));
    ok->setDefault(true);
    ok->setMinimumSize(128, 44);
    QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    fl->addWidget(ok);

    root->addWidget(head, 1);
    root->addWidget(foot, 0);

    dlg.adjustSize();
    const QSize want = dlg.size().expandedTo(QSize(540, 260));
    dlg.resize(QSize(qMin(want.width(), 720), qMin(want.height(), 520)));
    return dlg.exec() == QDialog::Accepted;
}

inline void uiInfo(QWidget *parent, const QString &title, const QString &text)
{
    uiPrompt(parent, title, text, UiTone::Info, QString::fromUtf8("知道了"));
}

inline void uiWarn(QWidget *parent, const QString &title, const QString &text)
{
    uiPrompt(parent, title, text, UiTone::Warn, QString::fromUtf8("知道了"));
}

inline void uiError(QWidget *parent, const QString &title, const QString &text)
{
    uiPrompt(parent, title, text, UiTone::Error, QString::fromUtf8("知道了"));
}

inline bool uiAsk(QWidget *parent, const QString &title, const QString &text,
                  const QString &okText = QString::fromUtf8("确定"),
                  const QString &cancelText = QString::fromUtf8("取消"), bool danger = false)
{
    return uiPrompt(parent, title, text, UiTone::Ask, okText, cancelText, danger);
}

#endif
