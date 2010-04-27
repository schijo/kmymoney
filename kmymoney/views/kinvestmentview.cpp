/***************************************************************************
                          kinvestmentview.cpp  -  description
                             -------------------
    begin                : Mon Mar 12 2007
    copyright            : (C) 2007 by Thomas Baumgart
    email                : Thomas Baumgart <ipwizard@users.sourceforge.net>
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "kinvestmentview.h"

#include <typeinfo>

// ----------------------------------------------------------------------------
// QT Includes

// ----------------------------------------------------------------------------
// KDE Includes


#include <klocale.h>
#include <KToggleAction>

// ----------------------------------------------------------------------------
// Project Includes

#include <mymoneyfile.h>
#include <mymoneyutils.h>
#include <mymoneysecurity.h>
#include <mymoneytransaction.h>
#include <mymoneyinvesttransaction.h>
#include <mymoneyaccount.h>
#include <kmymoneyglobalsettings.h>
#include <kmymoneyaccountcombo.h>
#include <kmymoneycurrencyselector.h>
#include "kmymoney.h"
#include "kinvestmentlistitem.h"
#include "models.h"

class KInvestmentView::Private
{
public:
  Private() :
      m_needReload(false),
      m_newAccountLoaded(false),
      m_recursion(false),
      m_precision(2),
      m_filterProxyModel(0) {}

  MyMoneyAccount    m_account;
  bool              m_needReload;
  bool              m_newAccountLoaded;
  bool              m_recursion;
  int               m_precision;
  AccountNamesFilterProxyModel *m_filterProxyModel;
};

KInvestmentView::KInvestmentView(QWidget *parent) :
    KInvestmentViewDecl(parent),
    d(new Private)
{
  d->m_filterProxyModel = new AccountNamesFilterProxyModel(this);
  d->m_filterProxyModel->addAccountType(MyMoneyAccount::Investment);
  d->m_filterProxyModel->setHideEquityAccounts(false);
  d->m_filterProxyModel->setSourceModel(Models::instance()->accountsModel());
  d->m_filterProxyModel->sort(0);
  m_accountComboBox->setModel(d->m_filterProxyModel);

  m_table->setRootIsDecorated(false);
  // m_table->setColumnText(0, i18n("Symbol"));
  m_table->addColumn(i18nc("Investment name", "Name"));
  m_table->addColumn(i18nc("Investment symbol", "Symbol"));

  int col = m_table->addColumn(i18n("Value"));
  m_table->setColumnAlignment(col, Qt::AlignRight);

  col = m_table->addColumn(i18n("Quantity"));
  m_table->setColumnAlignment(col, Qt::AlignRight);

  col = m_table->addColumn(i18n("Price"));
  m_table->setColumnAlignment(col, Qt::AlignRight);

  m_table->setMultiSelection(false);
  m_table->setColumnWidthMode(0, Q3ListView::Maximum);
  m_table->header()->setResizeEnabled(true);
  m_table->setAllColumnsShowFocus(true);
  m_table->setShowSortIndicator(true);
  KConfigGroup grp = KGlobal::config()->group("Investment Settings");
  m_table->restoreLayout(grp);

  connect(m_table, SIGNAL(contextMenu(K3ListView*, Q3ListViewItem* , const QPoint&)),
          this, SLOT(slotListContextMenu(K3ListView*, Q3ListViewItem*, const QPoint&)));
  connect(m_table, SIGNAL(selectionChanged(Q3ListViewItem *)), this, SLOT(slotSelectionChanged(Q3ListViewItem *)));

  connect(m_accountComboBox, SIGNAL(accountSelected(const QString&)),
          this, SLOT(slotSelectAccount(const QString&)));

  connect(m_table, SIGNAL(doubleClicked(Q3ListViewItem*, const QPoint&, int)), kmymoney->action("investment_edit"), SLOT(trigger()));

  connect(MyMoneyFile::instance(), SIGNAL(dataChanged()), this, SLOT(slotLoadView()));
}

KInvestmentView::~KInvestmentView()
{
  KConfigGroup grp = KGlobal::config()->group("Investment Settings");
  m_table->saveLayout(grp);
  delete d;
}

void KInvestmentView::slotSelectionChanged(Q3ListViewItem *item)
{
  kmymoney->slotSelectInvestment();

  KInvestmentListItem *pItem = dynamic_cast<KInvestmentListItem*>(item);
  if (pItem) {
    try {
      MyMoneyAccount account = MyMoneyFile::instance()->account(pItem->account().id());
      kmymoney->slotSelectInvestment(account);

    } catch (MyMoneyException *e) {
      delete e;
    }
  }
}

void KInvestmentView::slotListContextMenu(K3ListView* /* lv */, Q3ListViewItem* /*item*/, const QPoint& /*point*/)
{
  kmymoney->slotSelectInvestment();
  KInvestmentListItem *pItem = dynamic_cast<KInvestmentListItem*>(m_table->selectedItem());
  if (pItem) {
    kmymoney->slotSelectInvestment(MyMoneyFile::instance()->account(pItem->account().id()));
  }
  emit investmentRightMouseClick();
}

void KInvestmentView::slotLoadView(void)
{
  d->m_needReload = true;
  if (isVisible()) {
    loadView();
    d->m_needReload = false;
    // force a new account if the current one is empty
    d->m_newAccountLoaded = d->m_account.id().isEmpty();
  }
}

void KInvestmentView::loadAccounts(void)
{
  MyMoneyFile* file = MyMoneyFile::instance();

  // check if the current account still exists and make it the
  // current account
  if (!d->m_account.id().isEmpty()) {
    try {
      d->m_account = file->account(d->m_account.id());
    } catch (MyMoneyException *e) {
      delete e;
      d->m_account = MyMoneyAccount();
    }
  }

  d->m_filterProxyModel->invalidate();
  m_accountComboBox->expandAll();

  if (d->m_account.id().isEmpty()) {
    // there are no favorite accounts find any account
    QModelIndexList list = d->m_filterProxyModel->match(d->m_filterProxyModel->index(0, 0),
                           Qt::DisplayRole,
                           QVariant(QString("*")),
                           -1,
                           Qt::MatchFlags(Qt::MatchWildcard | Qt::MatchRecursive));
    for (QModelIndexList::ConstIterator it = list.constBegin(); it != list.constEnd(); ++it) {
      if (!it->parent().isValid())
        continue; // skip the top level accounts
      QVariant accountId = d->m_filterProxyModel->data(*it, AccountsModel::AccountIdRole);
      if (accountId.isValid()) {
        MyMoneyAccount a = file->account(accountId.toString());
        if (a.value("PreferredAccount") == "Yes") {
          d->m_account = a;
          break;
        } else if (d->m_account.id().isEmpty()) {
          d->m_account = a;
        }
      }
    }
  }

  if (!d->m_account.id().isEmpty()) {
    m_accountComboBox->setSelected(d->m_account.id());
    try {
      d->m_precision = MyMoneyMoney::denomToPrec(d->m_account.fraction());
    } catch (MyMoneyException *e) {
      qDebug("Security %s for account %s not found", qPrintable(d->m_account.currencyId()), qPrintable(d->m_account.name()));
      delete e;
      d->m_precision = 2;
    }
  }
}


bool KInvestmentView::slotSelectAccount(const MyMoneyObject& obj)
{
  if (typeid(obj) != typeid(MyMoneyAccount))
    return false;

  if (d->m_recursion)
    return false;

  d->m_recursion = true;
  const MyMoneyAccount& acc = dynamic_cast<const MyMoneyAccount&>(obj);
  bool rc = slotSelectAccount(acc.id());
  d->m_recursion = false;
  return rc;
}

bool KInvestmentView::slotSelectAccount(const QString& id, const QString& transactionId, const bool /* reconciliation*/)
{
  bool    rc = true;

  if (!id.isEmpty()) {
    // if the account id differs, then we have to do something
    if (d->m_account.id() != id) {
      try {
        d->m_account = MyMoneyFile::instance()->account(id);
        // if a stock account is selected, we show the
        // the corresponding parent (investment) account
        if (d->m_account.isInvest()) {
          d->m_account = MyMoneyFile::instance()->account(d->m_account.parentAccountId());
        }
        // TODO if we don't have an investment account, then we should switch to the ledger view
        d->m_newAccountLoaded = true;
        if (d->m_account.accountType() == MyMoneyAccount::Investment) {
          slotLoadView();
        } else {
          emit accountSelected(id, transactionId);
          d->m_account = MyMoneyAccount();
          d->m_needReload = true;
          rc = false;
        }

      } catch (MyMoneyException* e) {
        qDebug("Unable to retrieve account %s", qPrintable(id));
        delete e;
        rc = false;
      }
    } else {
      emit accountSelected(d->m_account);
    }
  }

  return rc;
}

void KInvestmentView::clear(void)
{
  // setup header font
  QFont font = KMyMoneyGlobalSettings::listHeaderFont();
  QFontMetrics fm(font);
  int height = fm.lineSpacing() + 6;
  m_table->header()->setMinimumHeight(height);
  m_table->header()->setMaximumHeight(height);
  m_table->header()->setFont(font);

  // setup cell font
  font = KMyMoneyGlobalSettings::listCellFont();
  m_table->setFont(font);

  // clear the table
  m_table->clear();

  // and the selected account in the combo box
  m_accountComboBox->setSelected(QString());
}

void KInvestmentView::loadView(void)
{
  // no account selected
  emit accountSelected(MyMoneyAccount());

  // clear the current contents ...
  clear();

  // ... load the combobox widget and select current account ...
  loadAccounts();

  if (d->m_account.id().isEmpty()) {
    // if we don't have an account we bail out
    setEnabled(false);
    return;
  }
  setEnabled(true);

  MyMoneyFile* file = MyMoneyFile::instance();
  bool showClosedAccounts = kmymoney->toggleAction("view_show_all_accounts")->isChecked()
                            || !KMyMoneyGlobalSettings::hideClosedAccounts();
  try {
    d->m_account = file->account(d->m_account.id());
    QStringList securities = d->m_account.accountList();

    for (QStringList::ConstIterator it = securities.constBegin(); it != securities.constEnd(); ++it) {
      MyMoneyAccount acc = file->account(*it);
      if (!acc.isClosed() || showClosedAccounts)
        new KInvestmentListItem(m_table, acc);
    }
  } catch (MyMoneyException* e) {
    qDebug("KInvestmentView::loadView() - selected account does not exist anymore");
    d->m_account = MyMoneyAccount();
    delete e;
  }

  // and tell everyone what's selected
  emit accountSelected(d->m_account);
}

void KInvestmentView::showEvent(QShowEvent* event)
{
  if (d->m_needReload) {
    loadView();
    d->m_needReload = false;
    d->m_newAccountLoaded = false;

  } else {
    emit accountSelected(d->m_account);
  }

  // don't forget base class implementation
  KInvestmentViewDecl::showEvent(event);
}

#include "kinvestmentview.moc"
