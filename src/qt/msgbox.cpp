#include "msgbox.h"

#include <QVBoxLayout>
#include <QTextEdit>
#include <QStyle>
#include <QApplication>
#include <QDesktopWidget>

MsgBox::MsgBox(QString title, QString msg, QString btn1, QString btn2, QWidget *parent, Qt::WindowFlags f) : QDialog(parent,f)
{
    button1 = nullptr;
    button2 = nullptr;

    setWindowTitle(title);

    QTextEdit *txt = new QTextEdit();
    txt->setText(msg);
    if (!btn1.isNull())
    {
        button1 = new QPushButton(btn1);
        connect(button1, SIGNAL (clicked()), this, SLOT (handleButton1()));
    }

    if (!btn2.isNull())
    {
        button2 = new QPushButton(btn2);
        connect(button2, SIGNAL (clicked()), this, SLOT (handleButton2()));
    }

    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(txt);
    QHBoxLayout *hlayout = new QHBoxLayout;
    if (button1) hlayout->addWidget(button1);
    if (button2) hlayout->addWidget(button2);
    layout->addLayout(hlayout);
    setLayout(layout);
    //setWindowModality(Qt::ApplicationModal);
    //setModal(Qt::ApplicationModal);
    setGeometry(
        QStyle::alignedRect(
            Qt::LeftToRight,
            Qt::AlignCenter,
            txt->size(),
            qApp->desktop()->availableGeometry() )
    );
}

