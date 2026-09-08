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
    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *brandPane = new QFrame;
    brandPane->setObjectName(QStringLiteral("brandPane"));
    brandPane->setMinimumWidth(380);
    auto *brandLayout = new QVBoxLayout(brandPane);
    brandLayout->setContentsMargins(48, 56, 48, 48);
    auto *mark = new QLabel(QStringLiteral("CH"));
    mark->setObjectName(QStringLiteral("logoMark"));
    mark->setFixedSize(48, 48);
    mark->setAlignment(Qt::AlignCenter);
    auto *brand = new QLabel(u8("ChargeHub"));
    brand->setObjectName(QStringLiteral("brandMark"));
    auto *subtitle = new QLabel(u8("电动汽车充电综合服务平台\n桌面用户端"));
    subtitle->setObjectName(QStringLiteral("brandSub"));
    subtitle->setWordWrap(true);
    auto *features = new QLabel(u8("附近电站  ·  预约占桩\n充电结算  ·  钱包充值"));
    features->setObjectName(QStringLiteral("brandFeat"));
    features->setWordWrap(true);
    brandLayout->addStretch();
    brandLayout->addWidget(mark);
    brandLayout->addSpacing(16);
    brandLayout->addWidget(brand);
    brandLayout->addSpacing(10);
    brandLayout->addWidget(subtitle);
    brandLayout->addSpacing(28);
    brandLayout->addWidget(features);
    brandLayout->addStretch();

    auto *formPane = new QWidget;
    formPane->setObjectName(QStringLiteral("formPane"));
    auto *form = new QVBoxLayout(formPane);
    form->setContentsMargins(48, 36, 48, 28);
    form->setSpacing(8);
    auto *title = new QLabel(u8("账号登录"));
    title->setObjectName(QStringLiteral("title"));
    auto *description = new QLabel(u8("使用手机号登录或注册。先连接管理端，再登录。"));
    description->setObjectName(QStringLiteral("muted"));
    description->setWordWrap(true);
    phoneEdit_ = new QLineEdit(QStringLiteral("13800138000"));
    phoneEdit_->setPlaceholderText(u8("手机号（11 位）"));
    passwordEdit_ = new QLineEdit(QStringLiteral("123456"));
    passwordEdit_->setEchoMode(QLineEdit::Password);
    passwordEdit_->setPlaceholderText(u8("密码（6~20 位）"));
    confirmEdit_ = new QLineEdit(QStringLiteral("123456"));
    confirmEdit_->setEchoMode(QLineEdit::Password);
    confirmEdit_->setPlaceholderText(u8("确认密码（仅注册）"));
    hostEdit_ = new QLineEdit;
    QSettings settings(QStringLiteral("ChargeHub"), QStringLiteral("UserClient"));
    hostEdit_->setText(settings.value(QStringLiteral("server"), QStringLiteral("127.0.0.1:8888")).toString());
    hostEdit_->setPlaceholderText(u8("服务器地址，例如 192.168.1.8:8888"));

    auto *connectButton = new QPushButton(u8("连接"));
    connectButton->setObjectName(QStringLiteral("ghost"));
    connectButton->setMaximumWidth(100);
    connect(connectButton, &QPushButton::clicked, this, &LoginPage::connectRequested);
    auto *hostRow = new QHBoxLayout;
    hostRow->addWidget(hostEdit_, 1);
    hostRow->addWidget(connectButton);
    auto *passwordRow = new QHBoxLayout;
    passwordRow->addWidget(passwordEdit_);
    passwordRow->addWidget(confirmEdit_);
    auto *loginButton = new QPushButton(u8("登  录"));
    loginButton->setObjectName(QStringLiteral("primary"));
    loginButton->setDefault(true);
    loginButton->setAutoDefault(true);
    loginButton->setMinimumHeight(46);
    auto *registerButton = new QPushButton(u8("注册新账号"));
    registerButton->setObjectName(QStringLiteral("ghost"));
    connect(loginButton, &QPushButton::clicked, this, &LoginPage::submitLogin);
    connect(registerButton, &QPushButton::clicked, this, &LoginPage::submitRegistration);
    statusLabel_ = new QLabel(u8("请先连接服务器"));
    statusLabel_->setObjectName(QStringLiteral("muted"));
    auto *tips = new QLabel(u8("本机填 127.0.0.1:8888。演示账号 13800138000 / 123456"));
    tips->setObjectName(QStringLiteral("muted"));
    tips->setWordWrap(true);
    form->addWidget(title);
    form->addWidget(description);
    form->addSpacing(8);
    form->addWidget(new QLabel(u8("手机号")));
    form->addWidget(phoneEdit_);
    form->addWidget(new QLabel(u8("密码 / 确认密码")));
    form->addLayout(passwordRow);
    form->addWidget(new QLabel(u8("服务器地址")));
    form->addLayout(hostRow);
    form->addWidget(statusLabel_);
    form->addStretch(1);
    form->addWidget(loginButton);
    form->addWidget(registerButton);
    form->addWidget(tips);
    outer->addWidget(brandPane, 4);
    outer->addWidget(formPane, 5);
}

QString LoginPage::serverHost() const
{
    QString raw = serverAddress();
    if (raw.isEmpty()) raw = QStringLiteral("127.0.0.1");
    if (raw.contains(QLatin1String("://"))) raw = raw.section(QLatin1String("://"), 1, 1);
    raw = raw.section('/', 0, 0);
    if (raw.count('.') >= 1 && raw.contains(':')) return raw.section(':', 0, -2);
    return raw;
}

quint16 LoginPage::serverPort() const
{
    const QString raw = serverAddress();
    if (raw.contains(':')) {
        bool ok = false;
        const int port = raw.section(':', -1).toInt(&ok);
        if (ok && port > 0 && port < 65536) return quint16(port);
    }
    return 8888;
}

QString LoginPage::serverAddress() const
{
    return hostEdit_ ? hostEdit_->text().trimmed() : QString();
}

void LoginPage::setStatus(const QString &message) { statusLabel_->setText(message); }

void LoginPage::submitLogin()
{
    QString phone, password;
    if (!validateCredentials(&phone, &password)) return;
    setStatus(u8("正在登录…"));
    emit authenticationRequested(QStringLiteral("LOGIN"), phone, password);
}

void LoginPage::submitRegistration()
{
    QString phone, password;
    if (!validateCredentials(&phone, &password)) return;
    if (password != confirmEdit_->text()) {
        uiWarn(this, u8("格式错误"), u8("两次输入的密码不一致"));
        return;
    }
    setStatus(u8("正在注册…"));
    emit authenticationRequested(QStringLiteral("REGISTER"), phone, password);
}

bool LoginPage::validateCredentials(QString *phone, QString *password)
{
    *phone = phoneEdit_->text().trimmed();
    *password = passwordEdit_->text();
    if (!QRegularExpression(QStringLiteral("^1[3-9][0-9]{9}$")).match(*phone).hasMatch()) {
        uiWarn(this, u8("格式错误"), u8("请输入正确的手机号格式"));
        return false;
    }
    if (password->size() < 6 || password->size() > 20) {
        uiWarn(this, u8("格式错误"), u8("密码长度须为 6~20 位"));
        return false;
    }
    return true;
}
