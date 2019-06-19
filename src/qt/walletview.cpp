// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/walletview.h>

#include <qt/addressbookpage.h>
#include <qt/askpassphrasedialog.h>
#include <qt/bitcoingui.h>
#include <qt/clientmodel.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/overviewpage.h>
#include <qt/platformstyle.h>
#include <qt/receivecoinsdialog.h>
#include <qt/sendcoinsdialog.h>
#include <qt/signverifymessagedialog.h>
#include <qt/transactiontablemodel.h>
#include <qt/transactionview.h>
#include <qt/walletmodel.h>

#include <interfaces/node.h>
#include <ui_interface.h>

#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QProgressDialog>
#include <QPushButton>
#include <QVBoxLayout>

WalletView::WalletView(const PlatformStyle *_platformStyle, QWidget *parent):
    QStackedWidget(parent),
    clientModel(nullptr),
    walletModel(nullptr),
    platformStyle(_platformStyle),
    nBlocksReceived(0),
    rawSignState(Init)
{
    // Create tabs
    overviewPage = new OverviewPage(platformStyle);

    transactionsPage = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout();
    QHBoxLayout *hbox_buttons = new QHBoxLayout();
    transactionView = new TransactionView(platformStyle, this);
    vbox->addWidget(transactionView);
    QPushButton *exportButton = new QPushButton(tr("&Export"), this);
    exportButton->setToolTip(tr("Export the data in the current tab to a file"));
    if (platformStyle->getImagesOnButtons()) {
        exportButton->setIcon(platformStyle->SingleColorIcon(":/icons/export"));
    }
    hbox_buttons->addStretch();
    hbox_buttons->addWidget(exportButton);
    vbox->addLayout(hbox_buttons);
    transactionsPage->setLayout(vbox);

    receiveCoinsPage = new ReceiveCoinsDialog(platformStyle);
    sendCoinsPage = new SendCoinsDialog(platformStyle);

    usedSendingAddressesPage = new AddressBookPage(platformStyle, AddressBookPage::ForEditing, AddressBookPage::SendingTab, this);
    usedReceivingAddressesPage = new AddressBookPage(platformStyle, AddressBookPage::ForEditing, AddressBookPage::ReceivingTab, this);

    addWidget(overviewPage);
    addWidget(transactionsPage);
    addWidget(receiveCoinsPage);
    addWidget(sendCoinsPage);

    // Clicking on a transaction on the overview pre-selects the transaction on the transaction history page
    connect(overviewPage, &OverviewPage::transactionClicked, transactionView, static_cast<void (TransactionView::*)(const QModelIndex&)>(&TransactionView::focusTransaction));

    connect(overviewPage, &OverviewPage::outOfSyncWarningClicked, this, &WalletView::requestedSyncWarningInfo);

    // Highlight transaction after send
    connect(sendCoinsPage, &SendCoinsDialog::coinsSent, transactionView, static_cast<void (TransactionView::*)(const uint256&)>(&TransactionView::focusTransaction));

    // Clicking on "Export" allows to export the transaction list
    connect(exportButton, &QPushButton::clicked, transactionView, &TransactionView::exportClicked);

    // Pass through messages from sendCoinsPage
    connect(sendCoinsPage, &SendCoinsDialog::message, this, &WalletView::message);
    // Pass through messages from transactionView
    connect(transactionView, &TransactionView::message, this, &WalletView::message);
}

WalletView::~WalletView()
{
}

void WalletView::setBitcoinGUI(BitcoinGUI *gui)
{
    if (gui)
    {
        // Clicking on a transaction on the overview page simply sends you to transaction history page
        connect(overviewPage, &OverviewPage::transactionClicked, gui, &BitcoinGUI::gotoHistoryPage);

        // Navigate to transaction history page after send
        connect(sendCoinsPage, &SendCoinsDialog::coinsSent, gui, &BitcoinGUI::gotoHistoryPage);

        // Receive and report messages
        connect(this, &WalletView::message, [gui](const QString &title, const QString &message, unsigned int style) {
            gui->message(title, message, style);
        });

        // Pass through encryption status changed signals
        connect(this, &WalletView::encryptionStatusChanged, gui, &BitcoinGUI::updateWalletStatus);

        // Pass through transaction notifications
        connect(this, &WalletView::incomingTransaction, gui, &BitcoinGUI::incomingTransaction);

        // Connect HD enabled state signal
        connect(this, &WalletView::hdEnabledStatusChanged, gui, &BitcoinGUI::updateWalletStatus);
    }
}

void WalletView::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;

    overviewPage->setClientModel(_clientModel);
    sendCoinsPage->setClientModel(_clientModel);

    connect(_clientModel, SIGNAL(numConnectionsChanged(int)), this, SLOT(updateNumConnections(int)));
    connect(_clientModel, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)), this, SLOT(numBlocksChanged(int,QDateTime,double,bool)));
}

void WalletView::setWalletModel(WalletModel *_walletModel)
{
    this->walletModel = _walletModel;

    // Put transaction list in tabs
    transactionView->setModel(_walletModel);
    overviewPage->setWalletModel(_walletModel);
    receiveCoinsPage->setModel(_walletModel);
    sendCoinsPage->setModel(_walletModel);
    usedReceivingAddressesPage->setModel(_walletModel ? _walletModel->getAddressTableModel() : nullptr);
    usedSendingAddressesPage->setModel(_walletModel ? _walletModel->getAddressTableModel() : nullptr);

    if (_walletModel)
    {
        // Receive and pass through messages from wallet model
        connect(_walletModel, &WalletModel::message, this, &WalletView::message);

        // Handle changes in encryption status
        connect(_walletModel, &WalletModel::encryptionStatusChanged, this, &WalletView::encryptionStatusChanged);
        updateEncryptionStatus();

        // update HD status
        Q_EMIT hdEnabledStatusChanged();

        // Balloon pop-up for new transaction
        connect(_walletModel->getTransactionTableModel(), &TransactionTableModel::rowsInserted, this, &WalletView::processNewTransaction);

        // Ask for passphrase if needed
        connect(_walletModel, &WalletModel::requireUnlock, this, &WalletView::unlockWallet);
        //connect(_walletModel, SIGNAL(requireUnlock(QString)), this, SLOT(unlockWallet(QString)));

        // Show progress dialog
        connect(_walletModel, &WalletModel::showProgress, this, &WalletView::showProgress);
    }
}

void WalletView::processNewTransaction(const QModelIndex& parent, int start, int /*end*/)
{
    // Prevent balloon-spam when initial block download is in progress
    if (!walletModel || !clientModel || clientModel->node().isInitialBlockDownload())
        return;

    TransactionTableModel *ttm = walletModel->getTransactionTableModel();
    if (!ttm || ttm->processingQueuedTransactions())
        return;

    QString date = ttm->index(start, TransactionTableModel::Date, parent).data().toString();
    qint64 amount = ttm->index(start, TransactionTableModel::Amount, parent).data(Qt::EditRole).toULongLong();
    QString type = ttm->index(start, TransactionTableModel::Type, parent).data().toString();
    QModelIndex index = ttm->index(start, 0, parent);
    QString address = ttm->data(index, TransactionTableModel::AddressRole).toString();
    QString label = ttm->data(index, TransactionTableModel::LabelRole).toString();

    Q_EMIT incomingTransaction(date, walletModel->getOptionsModel()->getDisplayUnit(), amount, type, address, label, walletModel->getWalletName());
}

void WalletView::gotoOverviewPage()
{
    setCurrentWidget(overviewPage);
}

void WalletView::gotoHistoryPage()
{
    setCurrentWidget(transactionsPage);
}

void WalletView::gotoReceiveCoinsPage()
{
    setCurrentWidget(receiveCoinsPage);
}

void WalletView::gotoSendCoinsPage(QString addr)
{
    setCurrentWidget(sendCoinsPage);

    if (!addr.isEmpty())
        sendCoinsPage->setAddress(addr);
}

void WalletView::gotoSignMessageTab(QString addr)
{
    // calls show() in showTab_SM()
    SignVerifyMessageDialog *signVerifyMessageDialog = new SignVerifyMessageDialog(platformStyle, this);
    signVerifyMessageDialog->setAttribute(Qt::WA_DeleteOnClose);
    signVerifyMessageDialog->setModel(walletModel);
    signVerifyMessageDialog->showTab_SM(true);

    if (!addr.isEmpty())
        signVerifyMessageDialog->setAddress_SM(addr);
}

void WalletView::gotoVerifyMessageTab(QString addr)
{
    // calls show() in showTab_VM()
    SignVerifyMessageDialog *signVerifyMessageDialog = new SignVerifyMessageDialog(platformStyle, this);
    signVerifyMessageDialog->setAttribute(Qt::WA_DeleteOnClose);
    signVerifyMessageDialog->setModel(walletModel);
    signVerifyMessageDialog->showTab_VM(true);

    if (!addr.isEmpty())
        signVerifyMessageDialog->setAddress_VM(addr);
}

bool WalletView::handlePaymentRequest(const SendCoinsRecipient& recipient)
{
    return sendCoinsPage->handlePaymentRequest(recipient);
}

void WalletView::showOutOfSyncWarning(bool fShow)
{
    overviewPage->showOutOfSyncWarning(fShow);
}

void WalletView::updateEncryptionStatus()
{
    Q_EMIT encryptionStatusChanged();
}

void WalletView::encryptWallet(bool status)
{
    if(!walletModel)
        return;
    AskPassphraseDialog dlg(status ? AskPassphraseDialog::Encrypt : AskPassphraseDialog::Decrypt, this);
    dlg.setModel(walletModel);
    dlg.exec();

    updateEncryptionStatus();
}

void WalletView::backupWallet()
{
    QString filename = GUIUtil::getSaveFileName(this,
        tr("Backup Wallet"), QString(),
        tr("Wallet Data (*.dat)"), nullptr);

    if (filename.isEmpty())
        return;

    if (!walletModel->wallet().backupWallet(filename.toLocal8Bit().data())) {
        Q_EMIT message(tr("Backup Failed"), tr("There was an error trying to save the wallet data to %1.").arg(filename),
            CClientUIInterface::MSG_ERROR);
        }
    else {
        Q_EMIT message(tr("Backup Successful"), tr("The wallet data was successfully saved to %1.").arg(filename),
            CClientUIInterface::MSG_INFORMATION);
    }
}

void WalletView::changePassphrase()
{
    AskPassphraseDialog dlg(AskPassphraseDialog::ChangePass, this);
    dlg.setModel(walletModel);
    dlg.exec();
}

void WalletView::unlockWallet(QString warningtext)
{
    if(!walletModel)
        return;
    // Unlock wallet when requested by wallet model
    if (walletModel->getEncryptionStatus() == WalletModel::Locked)
    {
        AskPassphraseDialog dlg(AskPassphraseDialog::Unlock, this, warningtext);
        dlg.setModel(walletModel);
        dlg.exec();
    }
}

void WalletView::usedSendingAddresses()
{
    if(!walletModel)
        return;

    GUIUtil::bringToFront(usedSendingAddressesPage);
}

void WalletView::usedReceivingAddresses()
{
    if(!walletModel)
        return;

    GUIUtil::bringToFront(usedReceivingAddressesPage);
}

void WalletView::showProgress(const QString &title, int nProgress)
{
    if (nProgress == 0) {
        progressDialog = new QProgressDialog(title, tr("Cancel"), 0, 100);
        GUIUtil::PolishProgressDialog(progressDialog);
        progressDialog->setWindowModality(Qt::ApplicationModal);
        progressDialog->setMinimumDuration(0);
        progressDialog->setAutoClose(false);
        progressDialog->setValue(0);
    } else if (nProgress == 100) {
        if (progressDialog) {
            progressDialog->close();
            progressDialog->deleteLater();
        }
    } else if (progressDialog) {
        if (progressDialog->wasCanceled()) {
            getWalletModel()->wallet().abortRescan();
        } else {
            progressDialog->setValue(nProgress);
        }
    }
}

void WalletView::requestedSyncWarningInfo()
{
    Q_EMIT outOfSyncWarningClicked();
}

#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <qt/msgbox.h>
//#include <util.h>

void WalletView::readtxs()
{
    txs.clear();
    stxs.clear();
    QFile inputFile(QString::fromStdString(gArgs.GetArg("-rawtxpath", "rawtx.txt")));
    if (inputFile.open(QIODevice::ReadOnly))
    {
        QTextStream in(&inputFile);
        while (!in.atEnd())
        {
            QString tx = in.readLine();
            auto it = txs.begin();
            for (; it != txs.end() && tx.compare(QString::fromStdString(*it)) != 0;
                 ++it);
            if (it == txs.end())
            {
                std::string tx_str = tx.toUtf8().constData();

                try {
                    UniValue result = InvokeRPC("signrawtransaction", tx_str, "[]", "[]");
                    if (find_value(result.get_obj(), "complete").get_bool()) {
                        printf("found signed tx\n");
                        stxs.push_back(tx_str);
                        continue;
                    }
                } catch (std::runtime_error e) { }

                txs.push_back(tx_str);
            }
        }
        inputFile.close();
    }
}

void WalletView::updatetxs()
{
    txs.insert(txs.end(), stxs.begin(), stxs.end());

    QFile outputFile(QString::fromStdString(gArgs.GetArg("-rawtxpath", "rawtx.txt")));
    if (txs.empty())
    {
        outputFile.remove();
    }
    else if (outputFile.open(QIODevice::ReadWrite | QFile::Text | QIODevice::Truncate))
    {
        QTextStream out( &outputFile );
        //printf("Update rawtx.txt\n");
        while(!txs.empty())
        {
            //printf("write: %s\n", txs.front().c_str());
            out << QString::fromStdString(txs.front()) << endl;
            txs.erase(txs.begin());
        }
        //printf("Done\n");
        outputFile.close();
    }
}

void WalletView::update()
{
    if (rawSignState != Init)
        return;

    rawSignState = Cancel;

    readtxs();

    if (!txs.empty()) {

        QString utransactionstr = "transaction";
        QString uitstr = "it";
        if (txs.size() > 1)
        {
            utransactionstr = "transactions";
            uitstr = "them";
        }

        QString txsstr = "transaction is";
        QString itstr = "it";
        if (txs.size() + stxs.size() > 1)
        {
            txsstr = "transactions are";
            itstr = "them";
        }

        QString message = QString::number(txs.size()) + " unsigned ";
        if (stxs.size() > 0)
            message += QString("and ") + QString::number(stxs.size()) + " signed ";

        message += txsstr + " found in the queue.\n\nIf you want to broadcast " + itstr + ", please wait for more connections with bitcoin network. Alternatively, you can only sign the unsigned "+utransactionstr+" and broadcast "+uitstr+" later.";

        switch (MsgBox::question(this,
                                 tr("Transactions in the queue"),
                                 message,
                                 "Wait for network", "Sign and update", "Cancel")) {
        case MsgBox::First:
            rawSignState = WaitForSigning;
            break;

        case MsgBox::Second:
            rawSignState = SignOnly;
            updateNumConnections(0);
            rawSignState = WaitForBroadcast;
            break;

        case MsgBox::Cancel:
        default:
            break;
        }

        updateNumConnections(clientModel->getNumConnections());
    }
    else
    {
        rawSignState = WaitForBroadcast;
    }
}

void WalletView::numBlocksChanged(int count, const QDateTime& blockDate, double nVerificationProgress, bool header)
{
    nBlocksReceived = 1;
    if (gArgs.GetBoolArg("-regtest", false))
        updateNumConnections(8);
}

void WalletView::updateNumConnections(int numConnections)
{
    RAWSignState signState = rawSignState;

    if (signState == Cancel)
        return;

    bool fUpdated = false;
    bool fUpdateSigned = false;

    if (signState == WaitForSigning || signState == WaitForBroadcast)
    {
        if (numConnections+nBlocksReceived < 4)
            return;

        rawSignState = Cancel;

        readtxs();

        // process signed txs
        for (auto it=stxs.begin(); it != stxs.end(); )
        {
            const std::string& stx = *it;
            UniValue result;
            result = InvokeRPC("decoderawtransaction", stx);

            switch (MsgBox::question(this,
                                     tr("A signed transaction is found in the queue"),
                                     QString("Previously signed transaction is ready to be broadcasted!\nDo you want to broadcast this transaction now?\n\nSigned transaction in hex format: \n\n") + QString::fromStdString(stx) + QString("\n\nSigned transaction in human readable format:\n\n") + QString::fromStdString(result.write(2)),
                                     "Broadcast", "Delete", "Keep in queue" ))
            {
                case MsgBox::First:
                    result = InvokeRPC("sendrawtransaction", stx);
                    QMessageBox::information(this, tr("Transaction has been sent"), QString("TXID: ") + QString::fromStdString(result.write(2)));

                  case MsgBox::Second:
                    it = stxs.erase(it);
                    fUpdated = true;
                    continue;
                  case MsgBox::Cancel:
                  default:
                    break;
            }
            it++;
        }
    }

    rawSignState = Cancel;

    // process unsigned txs

    if ((signState != WaitForBroadcast) && (txs.size() > 0))
    {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock("Please enter your wallet passphrase if you want to sign transactions in the queue."));
        if(ctx.isValid())
        {
            for (auto it=txs.begin(); it != txs.end(); )
            {
                const std::string& tx = *it;
                UniValue result;
                try {
                    result = InvokeRPC("signrawtransaction", tx);

                    if (find_value(result.get_obj(), "complete").get_bool())
                    {
                        std::string txhex = find_value(result.get_obj(), "hex").get_str();
                        result = InvokeRPC("decoderawtransaction", txhex);

                        MsgBox::DialogCode answer;
                        QString message;

                        if (signState != SignOnly)
                        {
                            answer = MsgBox::question(this,
                                        tr("Do you want to broadcast signed transaction?"),
                                        QString("The transaction is successfully signed!\nDo you want to broadcast this transaction?\n\nSigned transaction in hex format: \n\n") + QString::fromStdString(txhex) + QString("\n\nSigned transaction in human readable format:\n\n") + QString::fromStdString(result.write(2)),
                                        "Yes", "No" );
                            message = QString("This transaction won't be broadcasted. Do you want to update the original transaction to signed version or delete it?\n\n") + QString::fromStdString(tx);
                        }
                        else
                        {
                            answer = MsgBox::Cancel;
                            message = QString("Transaction successfully signed!\nDo you want to replace the original transaction with the signed version or delete it?\n\n") + QString::fromStdString(result.write(2));
                        }

                        if (MsgBox::First == answer)
                        {
                            result = InvokeRPC("sendrawtransaction", txhex);

                            QMessageBox::information(this, tr("The transaction has just sent"), QString("TXID: ") + QString::fromStdString(result.write(2)));
                            it = txs.erase(it);
                            fUpdated = true;
                            continue;
                        }
                        else if (MsgBox::Cancel == answer)
                        {
                            //If "No" answered ask whether we should delete or save this transaction
                            switch( MsgBox::question(this,
                                        tr("Do you want to update this transaction?"),
                                        message,
                                        "Update", "Delete", "Keep unsigned" ) )
                            {
                              case MsgBox::First:
                                stxs.push_back(txhex);
                                fUpdateSigned = true;

                              case MsgBox::Second:
                                it = txs.erase(it);
                                fUpdated = true;
                              continue;

                              case MsgBox::Cancel:
                              default:
                                break;
                            }
                        }

                    } else {
                        //ask whether we should delete this transaction
                        switch( MsgBox::question(this,
                                    "Error while signing the transaction",
                                    QString("Error while signing the transaction!\nDo you want to delete the original one from the text file?\n\n---\nError details:\n\n" + QString::fromStdString(result.write(2))),
                                    "Delete", "Keep" ) )
                        {
                          case MsgBox::First:
                            it = txs.erase(it);
                            fUpdated = true;
                            continue;

                          case MsgBox::Cancel:
                          default:
                            break;
                        }
                    }
                }
                catch (std::runtime_error e)
                {
                    //ask whether we should delete this transaction
                    switch( MsgBox::question(this,
                                e.what(),
                                QString("Error processing the transaction:\n\n") + QString::fromStdString(tx) + ("\n\nDo you want to delete this bad transaction from the text file?\n\n"
                                        "\n\n---\nError details:\n\n" + QString::fromStdString(e.what())),
                                        "Delete", "Keep" ) )
                    {
                      case MsgBox::First:
                        it = txs.erase(it);
                        fUpdated = true;
                        continue;

                      case MsgBox::Cancel:
                      default:
                        break;
                    }
                }
                it++;
            }
        }
    }

    //Update incoming transactions
    if (fUpdated)
        updatetxs();

    if (fUpdateSigned && signState == SignOnly)
        QMessageBox::information(this, "Updated transactions have not been broadcasted", "Some of the transactions have been signed but have not broadcasted yet. You will be prompted for broadcasting them later when there are enough connections with the bitcoin network.");
}
