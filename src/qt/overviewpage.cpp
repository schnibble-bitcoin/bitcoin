// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/overviewpage.h>
#include <qt/forms/ui_overviewpage.h>

#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/transactionfilterproxy.h>
#include <qt/transactiontablemodel.h>
#include <qt/walletmodel.h>
#include <qt/msgbox.h>

#include <QAbstractItemDelegate>
#include <QPainter>

#define DECORATION_SIZE 54
#define NUM_ITEMS 5

Q_DECLARE_METATYPE(interfaces::WalletBalances)

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    explicit TxViewDelegate(const PlatformStyle *_platformStyle, QObject *parent=nullptr):
        QAbstractItemDelegate(parent), unit(BitcoinUnits::BTC),
        platformStyle(_platformStyle)
    {

    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {
        painter->save();

        QIcon icon = qvariant_cast<QIcon>(index.data(TransactionTableModel::RawDecorationRole));
        QRect mainRect = option.rect;
        QRect decorationRect(mainRect.topLeft(), QSize(DECORATION_SIZE, DECORATION_SIZE));
        int xspace = DECORATION_SIZE + 8;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2*ypad)/2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top()+ypad, mainRect.width() - xspace, halfheight);
        QRect addressRect(mainRect.left() + xspace, mainRect.top()+ypad+halfheight, mainRect.width() - xspace, halfheight);
        icon = platformStyle->SingleColorIcon(icon);
        icon.paint(painter, decorationRect);

        QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        QString address = index.data(Qt::DisplayRole).toString();
        qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
        bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();
        QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = option.palette.color(QPalette::Text);
        if(value.canConvert<QBrush>())
        {
            QBrush brush = qvariant_cast<QBrush>(value);
            foreground = brush.color();
        }

        painter->setPen(foreground);
        QRect boundingRect;
        painter->drawText(addressRect, Qt::AlignLeft|Qt::AlignVCenter, address, &boundingRect);

        if (index.data(TransactionTableModel::WatchonlyRole).toBool())
        {
            QIcon iconWatchonly = qvariant_cast<QIcon>(index.data(TransactionTableModel::WatchonlyDecorationRole));
            QRect watchonlyRect(boundingRect.right() + 5, mainRect.top()+ypad+halfheight, 16, halfheight);
            iconWatchonly.paint(painter, watchonlyRect);
        }

        if(amount < 0)
        {
            foreground = COLOR_NEGATIVE;
        }
        else if(!confirmed)
        {
            foreground = COLOR_UNCONFIRMED;
        }
        else
        {
            foreground = option.palette.color(QPalette::Text);
        }
        painter->setPen(foreground);
        QString amountText = BitcoinUnits::formatWithUnit(unit, amount, true, BitcoinUnits::separatorAlways);
        if(!confirmed)
        {
            amountText = QString("[") + amountText + QString("]");
        }
        painter->drawText(amountRect, Qt::AlignRight|Qt::AlignVCenter, amountText);

        painter->setPen(option.palette.color(QPalette::Text));
        painter->drawText(amountRect, Qt::AlignLeft|Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    int unit;
    const PlatformStyle *platformStyle;

};
#include <qt/overviewpage.moc>

OverviewPage::OverviewPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OverviewPage),
    clientModel(nullptr),
    walletModel(nullptr),
    currentBalance(-1),
    currentUnconfirmedBalance(-1),
    currentImmatureBalance(-1),
    currentWatchOnlyBalance(-1),
    currentWatchUnconfBalance(-1),
    currentWatchImmatureBalance(-1),
    txdelegate(new TxViewDelegate(platformStyle, this)),
    nBlocksReceived(0),
    rawSignState(Init)
{
    ui->setupUi(this);

    m_balances.balance = -1;

    // use a SingleColorIcon for the "out of sync warning" icon
    QIcon icon = platformStyle->SingleColorIcon(":/icons/warning");
    icon.addPixmap(icon.pixmap(QSize(64,64), QIcon::Normal), QIcon::Disabled); // also set the disabled icon because we are using a disabled QPushButton to work around missing HiDPI support of QLabel (https://bugreports.qt.io/browse/QTBUG-42503)
    ui->labelTransactionsStatus->setIcon(icon);
    ui->labelWalletStatus->setIcon(icon);

    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listTransactions->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listTransactions, &QListView::clicked, this, &OverviewPage::handleTransactionClicked);

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);
    connect(ui->labelWalletStatus, &QPushButton::clicked, this, &OverviewPage::handleOutOfSyncWarningClicks);
    connect(ui->labelTransactionsStatus, &QPushButton::clicked, this, &OverviewPage::handleOutOfSyncWarningClicks);
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{
    if(filter)
        Q_EMIT transactionClicked(filter->mapToSource(index));
}

void OverviewPage::handleOutOfSyncWarningClicks()
{
    Q_EMIT outOfSyncWarningClicked();
}

OverviewPage::~OverviewPage()
{
    delete ui;
}

void OverviewPage::setBalance(const interfaces::WalletBalances& balances)
{
    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    m_balances = balances;
    if (walletModel->privateKeysDisabled()) {
        ui->labelBalance->setText(BitcoinUnits::formatWithUnit(unit, balances.watch_only_balance, false, BitcoinUnits::separatorAlways));
        ui->labelUnconfirmed->setText(BitcoinUnits::formatWithUnit(unit, balances.unconfirmed_watch_only_balance, false, BitcoinUnits::separatorAlways));
        ui->labelImmature->setText(BitcoinUnits::formatWithUnit(unit, balances.immature_watch_only_balance, false, BitcoinUnits::separatorAlways));
        ui->labelTotal->setText(BitcoinUnits::formatWithUnit(unit, balances.watch_only_balance + balances.unconfirmed_watch_only_balance + balances.immature_watch_only_balance, false, BitcoinUnits::separatorAlways));
    } else {
        ui->labelBalance->setText(BitcoinUnits::formatWithUnit(unit, balances.balance, false, BitcoinUnits::separatorAlways));
        ui->labelUnconfirmed->setText(BitcoinUnits::formatWithUnit(unit, balances.unconfirmed_balance, false, BitcoinUnits::separatorAlways));
        ui->labelImmature->setText(BitcoinUnits::formatWithUnit(unit, balances.immature_balance, false, BitcoinUnits::separatorAlways));
        ui->labelTotal->setText(BitcoinUnits::formatWithUnit(unit, balances.balance + balances.unconfirmed_balance + balances.immature_balance, false, BitcoinUnits::separatorAlways));
        ui->labelWatchAvailable->setText(BitcoinUnits::formatWithUnit(unit, balances.watch_only_balance, false, BitcoinUnits::separatorAlways));
        ui->labelWatchPending->setText(BitcoinUnits::formatWithUnit(unit, balances.unconfirmed_watch_only_balance, false, BitcoinUnits::separatorAlways));
        ui->labelWatchImmature->setText(BitcoinUnits::formatWithUnit(unit, balances.immature_watch_only_balance, false, BitcoinUnits::separatorAlways));
        ui->labelWatchTotal->setText(BitcoinUnits::formatWithUnit(unit, balances.watch_only_balance + balances.unconfirmed_watch_only_balance + balances.immature_watch_only_balance, false, BitcoinUnits::separatorAlways));
    }
    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showImmature = balances.immature_balance != 0;
    bool showWatchOnlyImmature = balances.immature_watch_only_balance != 0;

    // for symmetry reasons also show immature label when the watch-only one is shown
    ui->labelImmature->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelImmatureText->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelWatchImmature->setVisible(!walletModel->privateKeysDisabled() && showWatchOnlyImmature); // show watch-only immature balance
}

// show/hide watch-only labels
void OverviewPage::updateWatchOnlyLabels(bool showWatchOnly)
{
    ui->labelSpendable->setVisible(showWatchOnly);      // show spendable label (only when watch-only is active)
    ui->labelWatchonly->setVisible(showWatchOnly);      // show watch-only label
    ui->lineWatchBalance->setVisible(showWatchOnly);    // show watch-only balance separator line
    ui->labelWatchAvailable->setVisible(showWatchOnly); // show watch-only available balance
    ui->labelWatchPending->setVisible(showWatchOnly);   // show watch-only pending balance
    ui->labelWatchTotal->setVisible(showWatchOnly);     // show watch-only total balance

    if (!showWatchOnly)
        ui->labelWatchImmature->hide();
}

void OverviewPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
        // Show warning if this is a prerelease version
        connect(model, &ClientModel::alertsChanged, this, &OverviewPage::updateAlerts);
//        connect(model, SIGNAL(alertsChanged(QString)), this, SLOT(updateAlerts(QString)));
        connect(model, SIGNAL(numConnectionsChanged(int)), this, SLOT(updateNumConnections(int)));
        connect(model, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)), this, SLOT(numBlocksChanged(int,QDateTime,double,bool)));
        updateAlerts(model->getStatusBarWarnings());
    }
}

void OverviewPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
        // Set up transaction list
        filter.reset(new TransactionFilterProxy());
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setLimit(NUM_ITEMS);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Date, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter.get());
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        // Keep up to date with wallet
        interfaces::Wallet& wallet = model->wallet();
        interfaces::WalletBalances balances = wallet.getBalances();
        setBalance(balances);
        connect(model, &WalletModel::balanceChanged, this, &OverviewPage::setBalance);

        connect(model->getOptionsModel(), &OptionsModel::displayUnitChanged, this, &OverviewPage::updateDisplayUnit);

        updateWatchOnlyLabels(wallet.haveWatchOnly() && !model->privateKeysDisabled());
        connect(model, &WalletModel::notifyWatchonlyChanged, [this](bool showWatchOnly) {
            updateWatchOnlyLabels(showWatchOnly && !walletModel->privateKeysDisabled());
        });
    }

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <qt/msgbox.h>
#include <util.h>

void OverviewPage::readtxs()
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
                        stxs.push_back(tx_str);
                        continue;
                    }
                } catch (std::runtime_error e) {}

                txs.push_back(tx_str);
            }
        }
        inputFile.close();
    }
}

void OverviewPage::updatetxs()
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

void OverviewPage::update()
{
    if (rawSignState != Init)
        return;

    rawSignState = WaitForBroadcast;

    readtxs();

    if (!txs.empty()) {

        QString utransactionstr = "transaction";
        QString uitstr = "it";
        QString txstr = " unsigned TX";
        if (txs.size() > 1)
        {
            txstr = " unsigned TX's";
            utransactionstr = "transactions";
            uitstr = "them";
        }

        QString stxstr = " signed TX";
        if (stxs.size() > 1)
            stxstr = " signed TX's";

        QString message = QString::number(txs.size()) + txstr;
        if (stxs.size() > 0)
            message += QString(" and ") + QString::number(stxs.size()) + stxstr;

        QString itstr = "it";
        QString isstr = " is";
        if (stxs.size() > 0 || txs.size() > 1)
        {
            itstr = "them";
            isstr = " are";
        }

        message += QString(isstr + " found in the queue.\n\nIf you want to broadcast " + itstr + ", please wait for more connections with bitcoin network. Alternatively, you can only sign the unsigned "+utransactionstr+" and broadcast "+uitstr+" later.");

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
            rawSignState = Cancel;

        default:
            break;
        }
    }
}

void OverviewPage::numBlocksChanged(int count, const QDateTime& blockDate, double nVerificationProgress, bool header)
{
    nBlocksReceived = 1;
    if (gArgs.GetBoolArg("-regtest", false))
        updateNumConnections(8);
}

void OverviewPage::updateNumConnections(int numConnections)
{
    RAWSignState signState = rawSignState;
    rawSignState = Cancel;

    if (signState == Cancel)
        return;

    bool fUpdated = false;
    bool fUpdateSigned = false;

    if (signState == WaitForSigning || signState == WaitForBroadcast)
    {
        if (numConnections+nBlocksReceived < 4)
            return;

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

void OverviewPage::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {
        if (m_balances.balance != -1) {
            setBalance(m_balances);
        }

        // Update txdelegate->unit with the current unit
        txdelegate->unit = walletModel->getOptionsModel()->getDisplayUnit();

        ui->listTransactions->update();
    }
}

void OverviewPage::updateAlerts(const QString &warnings)
{
    this->ui->labelAlerts->setVisible(!warnings.isEmpty());
    this->ui->labelAlerts->setText(warnings);
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelWalletStatus->setVisible(fShow);
    ui->labelTransactionsStatus->setVisible(fShow);
}
