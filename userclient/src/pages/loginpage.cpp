#include "loginpage.h"

#include "uidialog.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
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

    // 登录表单
    loginForm_ = new QWidget;
    loginForm_->setObjectName(QStringLiteral("formPane"));
    auto *loginLayout = new QVBoxLayout(loginForm_);
    loginLayout->setContentsMargins(20, 18, 20, 16);
    loginLayout->setSpacing(6);

    auto *loginTitle = new QLabel(u8("欢迎回来"));
    loginTitle->setObjectName(QStringLiteral("title"));
    auto *loginDesc = new QLabel(u8("用手机号登录，开始充电之旅"));
    loginDesc->setObjectName(QStringLiteral("muted"));
    loginDesc->setWordWrap(true);

    phoneEdit_ = new QLineEdit;
    phoneEdit_->setPlaceholderText(u8("手机号"));

    passwordEdit_ = new QLineEdit;
    passwordEdit_->setEchoMode(QLineEdit::Password);
    passwordEdit_->setPlaceholderText(u8("密码（6~20 位）"));

    rememberMe_ = new QCheckBox(u8("记住我（7天内自动登录）"));
    rememberMe_->setStyleSheet("QCheckBox { color: #64748B; font-size: 12px; }"
                               "QCheckBox::indicator { width: 16px; height: 16px; }");

    loginButton_ = new QPushButton(u8("登录"));
    loginButton_->setObjectName(QStringLiteral("primary"));
    loginButton_->setDefault(true);
    loginButton_->setAutoDefault(true);

    switchButton_ = new QPushButton(u8("没有账号？去注册"));
    switchButton_->setObjectName(QStringLiteral("link"));
    switchButton_->setCursor(Qt::PointingHandCursor);

    statusLabel_ = new QLabel(u8("正在连接服务器..."));
    statusLabel_->setObjectName(QStringLiteral("muted"));
    auto *tips = new QLabel(u8("演示号 13800138000 / 123456"));
    tips->setObjectName(QStringLiteral("muted"));
    tips->setWordWrap(true);

    loginLayout->addWidget(loginTitle);
    loginLayout->addWidget(loginDesc);
    loginLayout->addSpacing(6);
    loginLayout->addWidget(phoneEdit_);
    loginLayout->addWidget(passwordEdit_);
    loginLayout->addWidget(rememberMe_);
    loginLayout->addWidget(statusLabel_);
    loginLayout->addStretch(1);
    loginLayout->addWidget(loginButton_);
    loginLayout->addWidget(switchButton_);
    loginLayout->addWidget(tips);

    // 注册表单
    registerForm_ = new QWidget;
    registerForm_->setObjectName(QStringLiteral("formPane"));
    registerForm_->hide();
    auto *registerLayout = new QVBoxLayout(registerForm_);
    registerLayout->setContentsMargins(20, 18, 20, 16);
    registerLayout->setSpacing(6);

    auto *registerTitle = new QLabel(u8("创建账号"));
    registerTitle->setObjectName(QStringLiteral("title"));
    auto *registerDesc = new QLabel(u8("注册新用户，享受充电服务"));
    registerDesc->setObjectName(QStringLiteral("muted"));
    registerDesc->setWordWrap(true);

    auto *regPhone = new QLineEdit;
    regPhone->setPlaceholderText(u8("手机号"));
    auto *regPassword = new QLineEdit;
    regPassword->setEchoMode(QLineEdit::Password);
    regPassword->setPlaceholderText(u8("密码（6~20位，含大小写和数字）"));
    auto *regConfirm = new QLineEdit;
    regConfirm->setEchoMode(QLineEdit::Password);
    regConfirm->setPlaceholderText(u8("确认密码"));

    auto *regStatus = new QLabel;
    regStatus->setObjectName(QStringLiteral("muted"));

    registerButton_ = new QPushButton(u8("注册"));
    registerButton_->setObjectName(QStringLiteral("primary"));
    registerButton_->setDefault(true);

    auto *backToLogin = new QPushButton(u8("已有账号？去登录"));
    backToLogin->setObjectName(QStringLiteral("link"));
    backToLogin->setCursor(Qt::PointingHandCursor);

    registerLayout->addWidget(registerTitle);
    registerLayout->addWidget(registerDesc);
    registerLayout->addSpacing(6);
    registerLayout->addWidget(regPhone);
    registerLayout->addWidget(regPassword);
    registerLayout->addWidget(regConfirm);
    registerLayout->addWidget(regStatus);
    registerLayout->addStretch(1);
    registerLayout->addWidget(registerButton_);
    registerLayout->addWidget(backToLogin);

    root->addWidget(brandPane);
    root->addWidget(loginForm_, 1);
    root->addWidget(registerForm_, 1);

    // 连接信号
    connect(loginButton_, &QPushButton::clicked, this, &LoginPage::onLoginClicked);
    connect(registerButton_, &QPushButton::clicked, this, &LoginPage::onRegisterClicked);
    connect(switchButton_, &QPushButton::clicked, this, &LoginPage::onSwitchToRegister);
    connect(backToLogin, &QPushButton::clicked, this, &LoginPage::onSwitchToLogin);
}

void LoginPage::showLoginForm()
{
    loginForm_->show();
    registerForm_->hide();
    isRegisterMode_ = false;
    loginButton_->setText(u8("登录"));
    switchButton_->setText(u8("没有账号？去注册"));
    statusLabel_->setText(QString());
}

void LoginPage::showRegisterForm()
{
    loginForm_->hide();
    registerForm_->show();
    isRegisterMode_ = true;
    loginButton_->setText(u8("注册"));
    switchButton_->setText(u8("已有账号？去登录"));
    statusLabel_->setText(QString());
}

void LoginPage::onSwitchToRegister()
{
    emit switchToRegister();
    showRegisterForm();
}

void LoginPage::onSwitchToLogin()
{
    emit switchToLogin();
    showLoginForm();
}

void LoginPage::onLoginClicked()
{
    if (isRegisterMode_) {
        onRegisterClicked();
        return;
    }
    QString phone, password;
    if (!validateCredentials(&phone, &password, false)) return;
    setStatus(u8("正在登录…"));
    emit authenticationRequested(QStringLiteral("LOGIN"), phone, password);
}

void LoginPage::onRegisterClicked()
{
    QString phone, password;
    if (!validateCredentials(&phone, &password, true)) return;

    if (password != confirmEdit_->text()) {
        uiWarn(this, u8("格式错误"), u8("两次输入的密码不一致"));
        return;
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

    if (password->size() < 6 || password->size() > 20) {
        uiWarn(this, u8("密码错误"), u8("密码长度须为 6~20 位"));
        return false;
    }

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

void LoginPage::setPhone(const QString &phone)
{
    phoneEdit_->setText(phone);
}

void LoginPage::clearPassword()
{
    passwordEdit_->clear();
    if (confirmEdit_)
        confirmEdit_->clear();
}

bool LoginPage::isRememberMeChecked() const
{
    return rememberMe_ ? rememberMe_->isChecked() : false;
}

QString LoginPage::getPhone() const
{
    return phoneEdit_->text().trimmed();
}

QString LoginPage::getPassword() const
{
    return passwordEdit_->text();
}
