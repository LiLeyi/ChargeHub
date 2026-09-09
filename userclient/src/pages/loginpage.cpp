#include "loginpage.h"

#include "uidialog.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {
QString u8(const char *text) { return QString::fromUtf8(text); }

QPushButton *makeAuthTab(const QString &text)
{
    auto *b = new QPushButton(text);
    b->setObjectName(QStringLiteral("authTab"));
    b->setCheckable(true);
    b->setAutoExclusive(true);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}
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
    form->setContentsMargins(20, 14, 20, 16);
    form->setSpacing(6);

    hostEdit_ = new QLineEdit;
    QSettings settings(QStringLiteral("ChargeHub"), QStringLiteral("UserClient"));
    hostEdit_->setText(settings.value(QStringLiteral("server"), QStringLiteral("127.0.0.1:8888")).toString());
    hostEdit_->setPlaceholderText(u8("服务器  127.0.0.1:8888"));
    auto *connectButton = new QPushButton(u8("连接"));
    connectButton->setObjectName(QStringLiteral("ghost"));
    connectButton->setMaximumWidth(64);
    connect(connectButton, &QPushButton::clicked, this, &LoginPage::connectRequested);
    auto *hostRow = new QHBoxLayout;
    hostRow->setSpacing(8);
    hostRow->addWidget(hostEdit_, 1);
    hostRow->addWidget(connectButton);

    auto *nav = new QFrame;
    nav->setObjectName(QStringLiteral("authNav"));
    auto *navLay = new QHBoxLayout(nav);
    navLay->setContentsMargins(0, 2, 0, 6);
    navLay->setSpacing(4);
    loginTab_ = makeAuthTab(u8("登录"));
    registerTab_ = makeAuthTab(u8("注册"));
    navLay->addWidget(loginTab_, 1);
    navLay->addWidget(registerTab_, 1);

    authStack_ = new QStackedWidget;
    auto *loginPage = new QWidget;
    auto *loginForm = new QVBoxLayout(loginPage);
    loginForm->setContentsMargins(0, 0, 0, 0);
    loginForm->setSpacing(6);
    auto *loginTitle = new QLabel(u8("欢迎回来"));
    loginTitle->setObjectName(QStringLiteral("title"));
    auto *loginDesc = new QLabel(u8("用已有手机号登录。先连接管理端，再进入找桩。"));
    loginDesc->setObjectName(QStringLiteral("muted"));
    loginDesc->setWordWrap(true);
    loginPhoneEdit_ = new QLineEdit(QStringLiteral("13800138000"));
    loginPhoneEdit_->setPlaceholderText(u8("手机号"));
    loginPasswordEdit_ = new QLineEdit(QStringLiteral("123456"));
    loginPasswordEdit_->setEchoMode(QLineEdit::Password);
    loginPasswordEdit_->setPlaceholderText(u8("密码（6~20 位）"));
    loginButton_ = new QPushButton(u8("登录"));
    loginButton_->setObjectName(QStringLiteral("primary"));
    loginButton_->setDefault(true);
    loginButton_->setAutoDefault(true);
    auto *tips = new QLabel(u8("演示号 13800138000 / 123456"));
    tips->setObjectName(QStringLiteral("muted"));
    tips->setWordWrap(true);
    loginForm->addWidget(loginTitle);
    loginForm->addWidget(loginDesc);
    loginForm->addSpacing(4);
    loginForm->addWidget(loginPhoneEdit_);
    loginForm->addWidget(loginPasswordEdit_);
    rememberMe_ = new QCheckBox(u8("记住我（7 天内自动登录）"));
    loginForm->addWidget(rememberMe_);
    loginForm->addStretch(1);
    loginForm->addWidget(loginButton_);
    auto *goRegister = new QPushButton(u8("没有账号？去注册"));
    goRegister->setObjectName(QStringLiteral("link"));
    goRegister->setCursor(Qt::PointingHandCursor);
    goRegister->setFlat(true);
    connect(goRegister, &QPushButton::clicked, this, [this] { showAuthPage(1); });
    loginForm->addWidget(goRegister);
    loginForm->addWidget(tips);

    auto *registerPage = new QWidget;
    auto *regForm = new QVBoxLayout(registerPage);
    regForm->setContentsMargins(0, 0, 0, 0);
    regForm->setSpacing(6);
    auto *regTitle = new QLabel(u8("创建账号"));
    regTitle->setObjectName(QStringLiteral("title"));
    auto *regDesc = new QLabel(u8("填写手机号和密码。注册成功后写入管理端数据库，之后可用该号登录。"));
    regDesc->setObjectName(QStringLiteral("muted"));
    regDesc->setWordWrap(true);
    regPhoneEdit_ = new QLineEdit;
    regPhoneEdit_->setPlaceholderText(u8("11 位手机号"));
    regPasswordEdit_ = new QLineEdit;
    regPasswordEdit_->setEchoMode(QLineEdit::Password);
    regPasswordEdit_->setPlaceholderText(u8("设置密码"));
    confirmEdit_ = new QLineEdit;
    confirmEdit_->setEchoMode(QLineEdit::Password);
    confirmEdit_->setPlaceholderText(u8("再输入一次密码"));
    passwordRules_ = new QLabel;
    passwordRules_->setObjectName(QStringLiteral("hintBox"));
    passwordRules_->setWordWrap(true);
    passwordRules_->setTextFormat(Qt::RichText);
    registerButton_ = new QPushButton(u8("注册并登录"));
    registerButton_->setObjectName(QStringLiteral("primary"));
    registerButton_->setDefault(false);
    registerButton_->setAutoDefault(true);
    auto *regHint = new QLabel(u8("演示号请走「登录」页，不要用 13800138000 再注册。"));
    regHint->setObjectName(QStringLiteral("muted"));
    regHint->setWordWrap(true);
    regForm->addWidget(regTitle);
    regForm->addWidget(regDesc);
    regForm->addSpacing(4);
    regForm->addWidget(regPhoneEdit_);
    regForm->addWidget(regPasswordEdit_);
    regForm->addWidget(confirmEdit_);
    regForm->addWidget(passwordRules_);
    regForm->addStretch(1);
    regForm->addWidget(registerButton_);
    auto *goLogin = new QPushButton(u8("已有账号？去登录"));
    goLogin->setObjectName(QStringLiteral("link"));
    goLogin->setCursor(Qt::PointingHandCursor);
    goLogin->setFlat(true);
    connect(goLogin, &QPushButton::clicked, this, [this] { showAuthPage(0); });
    regForm->addWidget(goLogin);
    regForm->addWidget(regHint);

    authStack_->addWidget(loginPage);
    authStack_->addWidget(registerPage);

    statusLabel_ = new QLabel(u8("请先连接服务器"));
    statusLabel_->setObjectName(QStringLiteral("muted"));
    statusLabel_->setWordWrap(true);

    form->addLayout(hostRow);
    form->addWidget(nav);
    form->addWidget(authStack_, 1);
    form->addWidget(statusLabel_);

    root->addWidget(brandPane);
    root->addWidget(formPane, 1);

    connect(loginTab_, &QPushButton::clicked, this, [this] { showAuthPage(0); });
    connect(registerTab_, &QPushButton::clicked, this, [this] { showAuthPage(1); });
    connect(loginButton_, &QPushButton::clicked, this, &LoginPage::submitLogin);
    connect(registerButton_, &QPushButton::clicked, this, &LoginPage::submitRegistration);
    connect(regPasswordEdit_, &QLineEdit::textChanged, this, &LoginPage::refreshPasswordHints);
    connect(confirmEdit_, &QLineEdit::textChanged, this, &LoginPage::refreshPasswordHints);

    refreshPasswordHints();
    showAuthPage(0);
}

void LoginPage::showAuthPage(int index)
{
    if (index == 1 && regPhoneEdit_->text().trimmed().isEmpty())
        regPhoneEdit_->setText(loginPhoneEdit_->text().trimmed());
    authStack_->setCurrentIndex(index);
    loginTab_->setChecked(index == 0);
    registerTab_->setChecked(index == 1);
    if (loginButton_)
        loginButton_->setDefault(index == 0);
    if (registerButton_)
        registerButton_->setDefault(index == 1);
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

bool LoginPage::isRememberMeChecked() const
{
    return rememberMe_ && rememberMe_->isChecked();
}

void LoginPage::setRememberMeChecked(bool on)
{
    if (rememberMe_)
        rememberMe_->setChecked(on);
}

void LoginPage::setLoginAccount(const QString &phone, const QString &password)
{
    if (loginPhoneEdit_ && !phone.isEmpty())
        loginPhoneEdit_->setText(phone);
    if (loginPasswordEdit_ && !password.isEmpty())
        loginPasswordEdit_->setText(password);
}

void LoginPage::refreshPasswordHints()
{
    const QString pw = regPasswordEdit_ ? regPasswordEdit_->text() : QString();
    const QString cf = confirmEdit_ ? confirmEdit_->text() : QString();
    bool hasUpper = false, hasLower = false, hasDigit = false;
    for (const QChar &ch : pw) {
        if (ch.isUpper())
            hasUpper = true;
        else if (ch.isLower())
            hasLower = true;
        else if (ch.isDigit())
            hasDigit = true;
    }
    const bool lenOk = pw.size() >= 6 && pw.size() <= 20;
    const bool matchOk = !cf.isEmpty() && pw == cf;
    auto line = [](bool ok, const QString &text) {
        return QStringLiteral("<span style='color:%1'>%2 %3</span>")
            .arg(ok ? QStringLiteral("#0F766E") : QStringLiteral("#64748B"),
                 ok ? QStringLiteral("✓") : QStringLiteral("○"), text);
    };
    passwordRules_->setText(
        u8("<b>注册密码须同时满足：</b><br>")
        + line(lenOk, u8("长度 6～20 位")) + QStringLiteral("<br>")
        + line(hasUpper, u8("包含大写字母")) + QStringLiteral("<br>")
        + line(hasLower, u8("包含小写字母")) + QStringLiteral("<br>")
        + line(hasDigit, u8("包含数字")) + QStringLiteral("<br>")
        + line(matchOk, u8("两次输入一致")));
}

void LoginPage::submitLogin()
{
    QString phone, password;
    if (!validateCredentials(loginPhoneEdit_, loginPasswordEdit_, &phone, &password, false))
        return;
    setStatus(u8("正在登录…"));
    emit authenticationRequested(QStringLiteral("LOGIN"), phone, password);
}

void LoginPage::submitRegistration()
{
    QString phone, password;
    if (!validateCredentials(regPhoneEdit_, regPasswordEdit_, &phone, &password, true))
        return;
    if (password != confirmEdit_->text()) {
        uiWarn(this, u8("格式错误"), u8("两次输入的密码不一致"));
        return;
    }
    setStatus(u8("正在注册…"));
    emit authenticationRequested(QStringLiteral("REGISTER"), phone, password);
}

bool LoginPage::validateCredentials(QLineEdit *phoneEdit, QLineEdit *passwordEdit, QString *phone,
                                    QString *password, bool isRegister)
{
    if (!phoneEdit || !passwordEdit)
        return false;
    *phone = phoneEdit->text().trimmed();
    *password = passwordEdit->text();
    if (!QRegularExpression(QStringLiteral("^1[3-9][0-9]{9}$")).match(*phone).hasMatch()) {
        uiWarn(this, u8("格式错误"), u8("请输入正确的手机号格式"));
        return false;
    }
    if (password->size() < 6 || password->size() > 20) {
        uiWarn(this, u8("格式错误"), u8("密码长度须为 6~20 位"));
        return false;
    }
    if (isRegister) {
        bool hasUpper = false, hasLower = false, hasDigit = false;
        for (const QChar &ch : *password) {
            if (ch.isUpper())
                hasUpper = true;
            else if (ch.isLower())
                hasLower = true;
            else if (ch.isDigit())
                hasDigit = true;
        }
        if (!hasUpper || !hasLower || !hasDigit) {
            uiWarn(this, u8("密码强度不足"),
                   u8("注册密码必须同时包含大写字母、小写字母和数字，长度为 6～20 位"));
            return false;
        }
    }
    return true;
}
