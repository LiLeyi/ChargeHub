#ifndef CHARGEHUB_LOGINPAGE_H
#define CHARGEHUB_LOGINPAGE_H

#include <QWidget>

class QLabel;
class QLineEdit;
class QCheckBox;
class QPushButton;

class LoginPage : public QWidget
{
    Q_OBJECT
public:
    explicit LoginPage(QWidget *parent = nullptr);

    QString serverHost() const;
    quint16 serverPort() const;
    QString serverAddress() const;
    void setStatus(const QString &message);
    void setPhone(const QString &phone);
    void clearPassword();

    // 自动登录相关
    bool isRememberMeChecked() const;
    QString getPhone() const;
    QString getPassword() const;

signals:
    void connectRequested();
    void authenticationRequested(QString type, QString phone, QString password);
    void switchToRegister();
    void switchToLogin();

private slots:
    void onLoginClicked();
    void onRegisterClicked();
    void onSwitchToRegister();
    void onSwitchToLogin();

private:
    void showLoginForm();
    void showRegisterForm();
    bool validateCredentials(QString *phone, QString *password, bool isRegister);

    QLineEdit *phoneEdit_ = nullptr;
    QLineEdit *passwordEdit_ = nullptr;
    QLineEdit *confirmEdit_ = nullptr;
    QCheckBox *rememberMe_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QPushButton *loginButton_ = nullptr;
    QPushButton *registerButton_ = nullptr;
    QPushButton *switchButton_ = nullptr;
    QWidget *loginForm_ = nullptr;
    QWidget *registerForm_ = nullptr;
    bool isRegisterMode_ = false;
};

#endif
