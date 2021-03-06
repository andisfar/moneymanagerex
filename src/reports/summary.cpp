/*******************************************************
 Copyright (C) 2006 Madhan Kanagavel
 Copyright (C) 2017 James Higley

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 ********************************************************/

#include "summary.h"
#include "constants.h"
#include "htmlbuilder.h"
#include "model/allmodel.h"
#include <algorithm>

class mmHistoryItem
{
public:
    mmHistoryItem();

    int         acctId;
    int         stockId;
    wxDate      purchaseDate;
    wxString    purchaseDateStr;
    double      purchasePrice;
    double      numShares;
    Model_StockHistory::Data_Set stockHist;
};

mmHistoryItem::mmHistoryItem()
{
    acctId = stockId = 0;
    purchasePrice = numShares = 0.0;
}

class mmHistoryData : public std::vector<mmHistoryItem>
{
public:
    double getDailyBalanceAt(const Model_Account::Data *account, const wxDate& date);
};

double mmHistoryData::getDailyBalanceAt(const Model_Account::Data *account, const wxDate& date)
{
    wxString strDate = date.FormatISODate();
    std::map<int, double> totBalance;

    for (const auto & stock : *this)
    {
        if (stock.acctId != account->id())
            continue;

        wxString precValueDate, nextValueDate;

        double valueAtDate = 0.0, precValue = 0.0, nextValue = 0.0;

        for (const auto & hist : stock.stockHist)
        {
            // test for the date requested
            if (hist.DATE == strDate)
            {
                valueAtDate = hist.VALUE;
                break;
            }
            // if not found, search for previous and next date
            if (precValue == 0.0 && hist.DATE < strDate)
            {
                precValue = hist.VALUE;
                precValueDate = hist.DATE;
            }
            if (hist.DATE > strDate)
            {
                nextValue = hist.VALUE;
                nextValueDate = hist.DATE;
            }
            // end conditions: prec value assigned and price date < requested date
            if (precValue != 0.0 && hist.DATE < strDate)
                break;
        }
        if (valueAtDate == 0.0)
        {
            //  if previous not found but if the given date is after purchase date, takes purchase price
            if (precValue == 0.0 && date >= stock.purchaseDate)
            {
                precValue = stock.purchasePrice;
                precValueDate = stock.purchaseDateStr;
            }
            //  if next not found and the accoung is open, takes previous date
            if (nextValue == 0.0 && Model_Account::status(account) == Model_Account::OPEN)
            {
                nextValue = precValue;
                nextValueDate = precValueDate;
            }
            if (precValue > 0.0 && nextValue > 0.0 && precValueDate >= stock.purchaseDateStr && nextValueDate >= stock.purchaseDateStr)
                valueAtDate = precValue;
        }

        totBalance[stock.stockId] += stock.numShares * valueAtDate;
    }

    double balance = 0.0;
    for (const auto& it : totBalance)
        balance += it.second;

    return balance;
}

mmReportSummaryByDate::mmReportSummaryByDate(int mode)
: mmPrintableBase(wxString::Format("Accounts Balance - %s", (mode == MONTHLY ? "Monthly" : "Yearly")))
, mode_(mode)
{
}

wxString mmReportSummaryByDate::getHTMLText()
{
    double          total, balancePerDay[6];
    mmHTMLBuilder   hb;
    wxDate          date, dateStart = wxDate::Today(), dateEnd = wxDate::Today();
    wxDateSpan      span;
    mmHistoryItem   *pHistItem;
    mmHistoryData   arHistory;
    std::vector<balanceMap> balanceMapVec(Model_Account::instance().all().size());
    std::vector<std::map<wxDate, double>::const_iterator>   arIt(balanceMapVec.size());
    std::vector<double> arBalance(balanceMapVec.size());
    struct BalanceEntry
    {
        wxDate date;
        std::vector<double> values;
    };
    std::vector<BalanceEntry> totBalanceData;

    std::vector<wxDate> arDates;

    hb.init();
    const auto name = wxString::Format(_("Accounts Balance - %s"), mode_ == MONTHLY ? _("Monthly Report") : _("Yearly Report"));
    hb.addReportHeader(name);

    hb.addDivContainer("shadow");
    {
        hb.startTable();
        {
            hb.startThead();
            {
                hb.startTableRow();
                {
                    hb.addTableHeaderCell(_("Date"));
                    hb.addTableHeaderCell(_("Cash"), true);
                    hb.addTableHeaderCell(_("Bank Accounts"), true);
                    hb.addTableHeaderCell(_("Credit Card Accounts"), true);
                    hb.addTableHeaderCell(_("Loan Accounts"), true);
                    hb.addTableHeaderCell(_("Term Accounts"), true);
                    hb.addTableHeaderCell(_("Total"), true);
                    hb.addTableHeaderCell(_("Stocks"), true);
                    hb.addTableHeaderCell(_("Balance"), true);
                }
                hb.endTableRow();
            }
            hb.endThead();

            int i = 0;
            for (const auto& account: Model_Account::instance().all())
            {
                if (Model_Account::type(account) != Model_Account::INVESTMENT)
                {
                    // balanceMapVec contains transactions totals day by day
                    const Model_Currency::Data* currency = Model_Account::currency(account);
                    for (const auto& tran : Model_Account::transaction(account))
                    {
                        balanceMapVec[i][Model_Checking::TRANSDATE(tran)]
                            += Model_Checking::balance(tran, account.ACCOUNTID)
                            * Model_CurrencyHistory::getDayRate(currency->id(), tran.TRANSDATE);
                    }
                    if (Model_Account::type(account) != Model_Account::TERM && balanceMapVec[i].size())
                    {
                        date = balanceMapVec[i].begin()->first;
                        if (date.IsEarlierThan(dateStart))
                            dateStart = date;
                    }
                    arBalance[i] = account.INITIALBAL * Model_CurrencyHistory::getDayRate(currency->id(), dateStart);
                }
                else
                {
                    Model_Stock::Data_Set stocks = Model_Stock::instance().find(Model_Stock::HELDAT(account.id()));
                    for (const auto & stock : stocks)
                    {
                        arHistory.resize(arHistory.size() + 1);
                        pHistItem = arHistory.data() + arHistory.size() - 1;
                        pHistItem->acctId = account.id();
                        pHistItem->stockId = stock.STOCKID;
                        pHistItem->purchasePrice = stock.PURCHASEPRICE;
                        pHistItem->purchaseDate = Model_Stock::PURCHASEDATE(stock);
                        pHistItem->purchaseDateStr = stock.PURCHASEDATE;
                        pHistItem->numShares = stock.NUMSHARES;
                        pHistItem->stockHist = Model_StockHistory::instance().find(Model_StockHistory::SYMBOL(stock.SYMBOL));
                        std::stable_sort(pHistItem->stockHist.begin(), pHistItem->stockHist.end(), SorterByDATE());
                        std::reverse(pHistItem->stockHist.begin(), pHistItem->stockHist.end());
                    }
                }
                i++;
            }

            if (mode_ == MONTHLY)
            {
                dateEnd.SetToLastMonthDay(dateEnd.GetMonth(), dateEnd.GetYear());
                span = wxDateSpan::Months(1);
            }
            else if (mode_ == YEARLY)
            {
                dateEnd.Set(31, wxDateTime::Dec, wxDateTime::Now().GetYear());
                span = wxDateSpan::Years(1);
            }
            else
            {
                wxFAIL_MSG("unknown report mode");
            }

            date = dateEnd;
            while (date.IsLaterThan(dateStart))
                date -= span;
            dateStart = date;

            int c = 0;
            for (const auto& acctMap: balanceMapVec)
                arIt[c++] = acctMap.begin();

            //  prepare the dates array
            while (dateStart <= dateEnd)
            {
                if (mode_ == MONTHLY)
                    dateStart.SetToLastMonthDay(dateStart.GetMonth(), dateStart.GetYear());
                arDates.push_back(dateStart);
                dateStart += span;
            }
 
            for (const auto & dd : arDates)
            {
                int k = 0;
                for (auto& account: Model_Account::instance().all())
                {
                    if (Model_Account::type(account) != Model_Account::INVESTMENT)
                    {
                        for (; arIt[k] != balanceMapVec[k].end(); ++arIt[k])
                        {
                            if (arIt[k]->first.IsLaterThan(dd))
                                break;
                            arBalance[k] += arIt[k]->second;
                        }
                    }
                    else
                    {
                        double convRate = 1.0;
                        Model_Currency::Data* currency = Model_Account::currency(account);
                        if (currency)
                            convRate = Model_CurrencyHistory::getDayRate(currency->id(), dd);
                        arBalance[k] = arHistory.getDailyBalanceAt(&account, dd) * convRate;
                    }
                    k++;
                }

                // prepare columns for report: date, cash, checking, credit card, loan, term, partial total, investment, grand total
                int precision = Model_Currency::precision(Model_Currency::GetBaseCurrency());
                BalanceEntry totBalanceEntry;
                totBalanceEntry.date = dd;
                for (int j = 0; j < 6; j++)
                    balancePerDay[j] = 0.0;
                int a = 0;
                for (const auto& account: Model_Account::instance().all())
                {
                    balancePerDay[Model_Account::type(account)] += arBalance[a++];
                }
                totBalanceEntry.values.push_back(balancePerDay[Model_Account::CASH]);
                totBalanceEntry.values.push_back(balancePerDay[Model_Account::CHECKING]);
                totBalanceEntry.values.push_back(balancePerDay[Model_Account::CREDIT_CARD]);
                totBalanceEntry.values.push_back(balancePerDay[Model_Account::LOAN]);
                totBalanceEntry.values.push_back(balancePerDay[Model_Account::TERM]);
                total = balancePerDay[Model_Account::CASH] + balancePerDay[Model_Account::CHECKING] 
                    + balancePerDay[Model_Account::CREDIT_CARD] + balancePerDay[Model_Account::LOAN]
                    + balancePerDay[Model_Account::TERM];
                totBalanceEntry.values.push_back(total);
                totBalanceEntry.values.push_back(balancePerDay[Model_Account::INVESTMENT]);
                total += balancePerDay[Model_Account::INVESTMENT];
                totBalanceEntry.values.push_back(total);
                totBalanceData.push_back(totBalanceEntry);
            }
            
            arIt.clear();
            arDates.clear();

            hb.startTbody();
            {
                wxString yearPrec;
                for (const auto& entry : totBalanceData)
                {
                    wxString year = wxString::Format("%d",entry.date.GetYear());
                    if ((mode_ == MONTHLY) && (yearPrec != year))
                    {
                        hb.startAltTableRow();
                            hb.addTableCell(year);
                            hb.addEmptyTableCell(8);
                        hb.endTableRow();
                    }
                    hb.startTableRow();
                        (mode_ == MONTHLY) ? hb.addTableCellMonth(entry.date.GetMonth()) : hb.addTableCell(year);
                        for (const auto& value : entry.values)
                            hb.addMoneyCell(value);
                    hb.endTableRow();
                    yearPrec = year;
                }
            }
            hb.endTbody();
        }
        hb.endTable();
    }
    hb.endDiv();
    
    hb.end();

    totBalanceData.clear();

    wxLogDebug("======= mmReportSummaryByDateMontly::getHTMLText =======");
    wxLogDebug("%s", hb.getHTMLText());

    return hb.getHTMLText();
}

mmReportSummaryByDateMontly::mmReportSummaryByDateMontly()
    : mmReportSummaryByDate(MONTHLY)
{
    setReportParameters(Reports::MonthlySummaryofAccounts);
}

mmReportSummaryByDateYearly::mmReportSummaryByDateYearly()
    : mmReportSummaryByDate(YEARLY)
{
    setReportParameters(Reports::YearlySummaryofAccounts);
}
