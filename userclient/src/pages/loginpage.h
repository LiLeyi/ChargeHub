#ifndef CHARGEHUB_LOGINPAGE_H
#define CHARGEHUB_LOGINPAGE_H

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QWidget>

class LoginPage : public QWidget
{
    Q_OBJECT
public:
    explicit LoginPage(QWidget *parent = nullptr);

    QString serverHost() const;
    quint16 serverPort() const;
    QString serverAddress() const;
    void setStatus(const QString &message);
    bool isRememberMeChecked() const;
    void setRememberMeChecked(bool on);
    void setLoginAccount(const QString &phone, const QString &password);

signals:
    void connectRequested();
    void authenticationRequested(QString type, QString phone, QString password);

private:
    void showAuthPage(int index);
    void submitLogin();
    void submitRegistration();
    void refreshPasswordHints();
    bool validateCredentials(QLineEdit *phoneEdit, QLineEdit *passwordEdit, QString *phone,
                             QString *password, bool isRegister);

    QLineEdit *hostEdit_ = nullptr;
    QLineEdit *loginPhoneEdit_ = nullptr;
    QLineEdit *loginPasswordEdit_ = nullptr;
    QLineEdit *regPhoneEdit_ = nullptr;
    QLineEdit *regPasswordEdit_ = nullptr;
    QLineEdit *confirmEdit_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *passwordRules_ = nullptr;
    QStackedWidget *authStack_ = nullptr;
    QPushButton *loginTab_ = nullptr;
    QPushButton *registerTab_ = nullptr;
    QPushButton *loginButton_ = nullptr;
    QPushButton *registerButton_ = nullptr;
    QCheckBox *rememberMe_ = nullptr;
};

#endif
