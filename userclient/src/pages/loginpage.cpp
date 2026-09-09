#include "loginpage.h"

#include "uidialog.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QVBoxLayout>

namespace {
QString u8(const char *text) { return QString::fromUtf8(text); }
}

LoginPage::LoginPage(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("loginRoot"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *brandPane = new QFrame;
    brandPane->setObjectName(QStringLiteral("brandPane"));
    auto *brandLayout = new QVBoxLayout(brandPane);
    brandLayout->setContentsMargins(24, 24, 24, 18);
    brandLayout->setSpacing(6);
    auto *mark = new QLabel(QStringLiteral("CH"));
    mark->setObjectName(QStringLiteral("logoMark"));
    mark->setFixedSize(40, 40);
    mark->setAlignment(Qt::AlignCenter);
    auto *brand = new QLabel(u8("ChargeHub"));
    brand->setObjectName(QStringLiteral("brandMark"));
    auto *subtitle = new QLabel(u8("找桩 · 导航 · 充电 · 结算"));
    subtitle->setObjectName(QStringLiteral("brandSub"));
    subtitle->setWordWrap(true);
    brandLayout->addWidget(mark, 0, Qt::AlignLeft);
    brandLayout->addSpacing(8);
    brandLayout->addWidget(brand);
    brandLayout->addWidget(subtitle);

    auto *formPane = new QWidget;
    formPane->setObjectName(QStringLiteral("formPane"));
    auto *form = new QVBoxLayout(formPane);
    form->setContentsMargins(20, 18, 20, 16);
    form->setSpacing(6);
    auto *title = new QLabel(u8("欢迎回来"));
    title->setObjectName(QStringLiteral("title"));
    auto *description = new QLabel(u8("用手机号登录。先连接管理端，再进入找桩。"));
    description->setObjectName(QStringLiteral("muted"));
    description->setWordWrap(true);

    phoneEdit_ = new QLineEdit(QStringLiteral("13800138000"));
    phoneEdit_->setPlaceholderText(u8("手机号"));
    connect(phoneEdit_, &QLineEdit::textChanged, this, &LoginPage::onPhoneTextChanged);

    passwordEdit_ = new QLineEdit(QStringLiteral("123456"));
    passwordEdit_->setEchoMode(QLineEdit::Password);
    passwordEdit_->setPlaceholderText(u8("密码（6~20 位）"));

    confirmEdit_ = new QLineEdit(QStringLiteral(""));
    confirmEdit_->setEchoMode(QLineEdit::Password);
    confirmEdit_->setPlaceholderText(u8("确认密码（注册时填写，须含大小写和数字）"));
    confirmEdit_->hide();

    auto *loginButton = new QPushButton(u8("登录"));
    loginButton->setObjectName(QStringLiteral("primary"));
    loginButton->setDefault(true);
    loginButton->setAutoDefault(true);
    auto *registerButton = new QPushButton(u8("注册新账号"));
    registerButton->setObjectName(QStringLiteral("ghost"));
    connect(loginButton, &QPushButton::clicked, this, &LoginPage::submitLogin);
    connect(registerButton, &QPushButton::clicked, this, &LoginPage::submitRegistration);

    statusLabel_ = new QLabel(u8("正在连接服务器..."));
    statusLabel_->setObjectName(QStringLiteral("muted"));
    auto *tips = new QLabel(u8("演示号 13800138000 / 123456"));
    tips->setObjectName(QStringLiteral("muted"));
    tips->setWordWrap(true);

    form->addWidget(title);
    form->addWidget(description);
    form->addSpacing(6);
    form->addWidget(phoneEdit_);
    form->addWidget(passwordEdit_);
    form->addWidget(confirmEdit_);
    form->addWidget(statusLabel_);
    form->addStretch(1);
    form->addWidget(loginButton);
    form->addWidget(registerButton);
    form->addWidget(tips);

    root->addWidget(brandPane);
    root->addWidget(formPane, 1);
}

QString LoginPage::serverHost() const
{
    return QStringLiteral("127.0.0.1");
}

quint16 LoginPage::serverPort() const
{
    return 8888;
}

QString LoginPage::serverAddress() const
{
    return QStringLiteral("127.0.0.1:8888");
}

void LoginPage::setStatus(const QString &message)
{
    statusLabel_->setText(message);
}

void LoginPage::onPhoneTextChanged(const QString &text)
{
    // 检查输入的手机号是否已注册（演示账号）
    bool isExistingUser = (text == QStringLiteral("13800138000") ||
                           text == QStringLiteral("13912345678") ||
                           text == QStringLiteral("18611112222") ||
                           text == QStringLiteral("17700009999"));

    if (!isExistingUser && text.length() == 11) {
        // 新手机号，显示确认密码框
        confirmEdit_->show();
        isRegisterMode_ = true;
    } else {
        confirmEdit_->hide();
        isRegisterMode_ = false;
    }
}

void LoginPage::submitLogin()
{
    QString phone, password;
    if (!validateCredentials(&phone, &password, false)) return;
    setStatus(u8("正在登录…"));
    emit authenticationRequested(QStringLiteral("LOGIN"), phone, password);
}

void LoginPage::submitRegistration()
{
    QString phone, password;
    if (!validateCredentials(&phone, &password, true)) return;

    // 如果是注册模式，检查确认密码
    if (isRegisterMode_) {
        if (password != confirmEdit_->text()) {
            uiWarn(this, u8("格式错误"), u8("两次输入的密码不一致"));
            return;
        }
    }

    setStatus(u8("正在注册…"));
    emit authenticationRequested(QStringLiteral("REGISTER"), phone, password);
}

bool LoginPage::validateCredentials(QString *phone, QString *password, bool isRegister)
{
    *phone = phoneEdit_->text().trimmed();
    *password = passwordEdit_->text();

    if (!QRegularExpression(QStringLiteral("^1[3-9][0-9]{9}$")).match(*phone).hasMatch()) {
        uiWarn(this, u8("格式错误"), u8("请输入正确的手机号格式"));
        return false;
    }

    // 密码长度检测（登录和注册都检查）
    if (password->size() < 6 || password->size() > 20) {
        uiWarn(this, u8("密码错误"), u8("密码长度须为 6~20 位"));
        return false;
    }

    // 只有注册时才进行强密码检验（包含大小写和数字）
    if (isRegister) {
        bool hasUpper = false, hasLower = false, hasDigit = false;
        for (const QChar &ch : *password) {
            if (ch.isUpper()) hasUpper = true;
            else if (ch.isLower()) hasLower = true;
            else if (ch.isDigit()) hasDigit = true;
        }

        if (!hasUpper || !hasLower || !hasDigit) {
            uiWarn(this, u8("密码强度不足"),
                   u8("注册密码必须包含大写字母、小写字母和数字"));
            return false;
        }
    }

    return true;
}
