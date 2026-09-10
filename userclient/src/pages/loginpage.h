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
    /**
     * 创建登录/注册双页并从 QSettings 读取隐藏的服务器地址。
     * @param parent Qt 父对象，负责页面生命周期；可为空。
     */
    explicit LoginPage(QWidget *parent = nullptr);

    /** @return 隐藏服务器地址中解析出的主机名；空值回退 127.0.0.1。 */
    QString serverHost() const;
    /** @return 隐藏服务器地址中解析出的端口；无有效端口时返回 8888。 */
    quint16 serverPort() const;
    /** @return QSettings 中读取的原始 `host:port` 字符串（已去首尾空白）。 */
    QString serverAddress() const;
    /** @param message 要显示在表单底部的连接或认证状态。 */
    void setStatus(const QString &message);
    /** @return “记住我”复选框当前是否勾选。 */
    bool isRememberMeChecked() const;
    /** @param on true 勾选“记住我”，false 取消。 */
    void setRememberMeChecked(bool on);
    /**
     * 回填登录表单，供记住登录流程使用；空参数不会覆盖已有内容。
     * @param phone 登录手机号。
     * @param password 登录密码。
     */
    void setLoginAccount(const QString &phone, const QString &password);

signals:
    /** 用户请求重新建立 TCP 连接时发出；当前隐藏地址版本保留此兼容信号。 */
    void connectRequested();
    /**
     * 表单校验通过后发出认证请求。
     * @param type `LOGIN` 或 `REGISTER`。
     * @param phone 已去空白且格式有效的手机号。
     * @param password 原始密码。
     */
    void authenticationRequested(QString type, QString phone, QString password);

private:
    /** @param index 0 显示登录页，1 显示注册页；同时切换默认回车按钮。 */
    void showAuthPage(int index);
    /** 校验登录表单，成功则发出 authenticationRequested。无返回值。 */
    void submitLogin();
    /** 校验注册表单及两次密码，成功则发出 authenticationRequested。无返回值。 */
    void submitRegistration();
    /** 扫描注册密码的长度、大小写、数字及确认一致性并刷新提示。 */
    void refreshPasswordHints();
    /**
     * 统一校验手机号和密码，并通过输出参数返回规范化结果。
     * @param phoneEdit 手机号输入框，不得为空。
     * @param passwordEdit 密码输入框，不得为空。
     * @param phone 输出：去除首尾空白的手机号。
     * @param password 输出：保持原样的密码。
     * @param isRegister true 时额外检查大小写字母和数字组合。
     * @return 全部规则通过返回 true；否则弹出错误提示并返回 false。
     */
    bool validateCredentials(QLineEdit *phoneEdit, QLineEdit *passwordEdit, QString *phone,
                             QString *password, bool isRegister);

    QString serverAddress_;
    QLineEdit *loginPhoneEdit_ = nullptr;
    QLineEdit *loginPasswordEdit_ = nullptr;
    QLineEdit *regPhoneEdit_ = nullptr;
    QLineEdit *regPasswordEdit_ = nullptr;
    QLineEdit *confirmEdit_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *passwordRules_ = nullptr;
    QStackedWidget *authStack_ = nullptr;
    QPushButton *loginButton_ = nullptr;
    QPushButton *registerButton_ = nullptr;
    QCheckBox *rememberMe_ = nullptr;
};

#endif
