#ifndef CHARGEHUB_LOGINPAGE_H
#define CHARGEHUB_LOGINPAGE_H

#include <QWidget>

class QLabel;
class QLineEdit;

class LoginPage : public QWidget
{
    Q_OBJECT
public:
    explicit LoginPage(QWidget *parent = nullptr);

    QString serverHost() const;
    quint16 serverPort() const;
    QString serverAddress() const;
    void setStatus(const QString &message);

signals:
    void connectRequested();
    void authenticationRequested(QString type, QString phone, QString password);

private slots:
    /** 当手机号文本改变时，动态显示/隐藏确认密码框 */
    void onPhoneTextChanged(const QString &text);

private:
    void submitLogin();
    void submitRegistration();
    bool validateCredentials(QString *phone, QString *password, bool isRegister);

    QLineEdit *phoneEdit_ = nullptr;
    QLineEdit *passwordEdit_ = nullptr;
    QLineEdit *confirmEdit_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    bool isRegisterMode_ = false;
};

#endif
