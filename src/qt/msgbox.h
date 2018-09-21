#ifndef MSGBOX_H
#define MSGBOX_H

#include <QDialog>
#include <QPushButton>
class MsgBox : public QDialog
{
    Q_OBJECT

    QPushButton *button1;
    QPushButton *button2;

public:
    MsgBox(QString title, QString msg, QString btn1, QString btn2 = QString(), QWidget *parent = nullptr, Qt::WindowFlags f = 0);
    virtual ~MsgBox() {}

    static int question(QString title, QString msg, QString btn1, QString btn2 = QString(), QWidget* parent = nullptr, Qt::WindowFlags f = 0)
    {
        return MsgBox(title, msg, btn1, btn2, parent).exec();
    }

public Q_SLOTS:
    void handleButton1()
    {
        done(QDialog::Accepted);
    }
    void handleButton2()
    {
        done(QDialog::Rejected);
    }
};


#endif // MSGBOX_H
