#include "msgbox.h"

#include <QVBoxLayout>
#include <QTextBrowser>
#include <QStyle>
#include <QApplication>
#include <QDesktopWidget>

MsgBox::MsgBox(QWidget *parent, QString title, QString msg, QString name1, QString name2, QString name3, Qt::WindowFlags f) : QDialog(parent,f)
{

    setWindowTitle(title);
    QVBoxLayout *layout = new QVBoxLayout;
    QTextBrowser *txt = new QTextBrowser();
    txt->setText(msg);
    layout->addWidget(txt);

    QHBoxLayout *hlayout = new QHBoxLayout;

    code2 = First;
    code3 = First;
    if (!name1.isNull())
    {
        QPushButton * button1 = new QPushButton(name1);
        connect(button1, SIGNAL (clicked()), this, SLOT (handleButton1()));
        hlayout->addWidget(button1);
        code2 = Cancel;
        code3 = Cancel;
    }

    if (!name2.isNull())
    {
        QPushButton * button2 = new QPushButton(name2);
        connect(button2, SIGNAL (clicked()), this, SLOT (handleButton2()));
        hlayout->addWidget(button2);
        code3 = Cancel;
    }

    if (!name3.isNull())
    {
        QPushButton * button3 = new QPushButton(name3);
        connect(button3, SIGNAL (clicked()), this, SLOT (handleButton3()));
        hlayout->addWidget(button3);
        if (code2 == Cancel)
            code2 = Second;
    }

    layout->addLayout(hlayout);
    setLayout(layout);
    setGeometry(
        QStyle::alignedRect(
            Qt::LeftToRight,
            Qt::AlignCenter,
            txt->size(),
            qApp->desktop()->availableGeometry() )
    );
}

#include <rpc/server.h>
#include <rpc/client.h>
UniValue InvokeRPC(std::string strMethod,
                 std::string arg1,
                 std::string arg2,
                 std::string arg3,
                 std::string arg4,
                 std::string arg5)
{
    std::vector<std::string> vArgs;
    if (!arg1.empty()) vArgs.push_back(arg1);
    if (!arg2.empty()) vArgs.push_back(arg2);
    if (!arg3.empty()) vArgs.push_back(arg3);
    if (!arg4.empty()) vArgs.push_back(arg4);
    if (!arg5.empty()) vArgs.push_back(arg5);

    JSONRPCRequest request;
    request.strMethod = strMethod;
    request.params = RPCConvertValues(strMethod, vArgs);
    request.fHelp = false;

    rpcfn_type method = tableRPC[strMethod]->actor;
    try {
        UniValue result = (*method)(request);
        return result;
    }
    catch (const UniValue& objError) {
        throw std::runtime_error(find_value(objError, "message").get_str());
    }
}
