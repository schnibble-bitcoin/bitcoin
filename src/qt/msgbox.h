#ifndef MSGBOX_H
#define MSGBOX_H

#include <QDialog>
#include <QPushButton>


class MsgBox : public QDialog
{
    Q_OBJECT

public:
    enum DialogCode
    {
        Cancel = 0,
        First = 1,
        Second = 2
    };

    MsgBox(QWidget *parent, QString title, QString msg, QString name1, QString name2 = QString(), QString name3 = QString(), Qt::WindowFlags f = 0);
    virtual ~MsgBox() {}

    static DialogCode question(QWidget* parent, QString title, QString msg, QString name1, QString name2 = QString(), QString name3 = QString(), Qt::WindowFlags f = 0)
    {
        return (DialogCode)MsgBox(parent, title, msg, name1, name2, name3, f).exec();
    }

private:
    const DialogCode code1 = First;
    DialogCode code2;
    DialogCode code3;

public Q_SLOTS:
    void handleButton1()
    {
        done(code1);
    }
    void handleButton2()
    {
        done(code2);
    }
    void handleButton3()
    {
        done(code3);
    }
};

#include <univalue.h>
UniValue InvokeRPC(std::string strMethod,
                 std::string arg1 = std::string(),
                 std::string arg2 = std::string(),
                 std::string arg3 = std::string(),
                 std::string arg4 = std::string(),
                 std::string arg5 = std::string());

#endif // MSGBOX_H
