// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "init.h"
#include "main.h"

#include "addrman.h"
#include "chainparams.h"
#include "fs.h"
#include "masternode-budget.h"
#include "masternode-sync.h"
#include "masternode.h"
#include "masternodeman.h"
#include "util.h"

CFinalizedBudget::CFinalizedBudget() :
        fAutoChecked(false),
        fValid(true),
        strBudgetName(""),
        nBlockStart(0),
        vecBudgetPayments(),
        mapVotes(),
        nFeeTXHash(),
        nTime(0)
{ }

CFinalizedBudget::CFinalizedBudget(const CFinalizedBudget& other) :
        fAutoChecked(false),
        fValid(true),
        strBudgetName(other.strBudgetName),
        nBlockStart(other.nBlockStart),
        vecBudgetPayments(other.vecBudgetPayments),
        mapVotes(other.mapVotes),
        nFeeTXHash(other.nFeeTXHash),
        nTime(other.nTime)
{ }

bool CFinalizedBudget::AddOrUpdateVote(CFinalizedBudgetVote& vote, std::string& strError)
{
    LOCK(cs);

    uint256 hash = vote.vin.prevout.GetHash();
    std::string strAction = "New vote inserted:";

    if (mapVotes.count(hash)) {
        if (mapVotes[hash].nTime > vote.nTime) {
            strError = strprintf("new vote older than existing vote - %s\n", vote.GetHash().ToString());
            LogPrint(BCLog::MNBUDGET, "CFinalizedBudget::AddOrUpdateVote - %s\n", strError);
            return false;
        }
        if (vote.nTime - mapVotes[hash].nTime < BUDGET_VOTE_UPDATE_MIN) {
            strError = strprintf("time between votes is too soon - %s - %lli sec < %lli sec\n", vote.GetHash().ToString(), vote.nTime - mapVotes[hash].nTime,BUDGET_VOTE_UPDATE_MIN);
            LogPrint(BCLog::MNBUDGET, "CFinalizedBudget::AddOrUpdateVote - %s\n", strError);
            return false;
        }
        strAction = "Existing vote updated:";
    }

    if (vote.nTime > GetTime() + (60 * 60)) {
        strError = strprintf("new vote is too far ahead of current time - %s - nTime %lli - Max Time %lli\n", vote.GetHash().ToString(), vote.nTime, GetTime() + (60 * 60));
        LogPrint(BCLog::MNBUDGET, "CFinalizedBudget::AddOrUpdateVote - %s\n", strError);
        return false;
    }

    mapVotes[hash] = vote;
    LogPrint(BCLog::MNBUDGET, "CFinalizedBudget::AddOrUpdateVote - %s %s\n", strAction.c_str(), vote.GetHash().ToString().c_str());
    return true;
}

// Sort budget proposals by hash
struct sortProposalsByHash  {
    bool operator()(const CBudgetProposal* left, const CBudgetProposal* right)
    {
        return (left->GetHash() < right->GetHash());
    }
};

// Sort budget payments by hash
struct sortPaymentsByHash  {
    bool operator()(const CTxBudgetPayment& left, const CTxBudgetPayment& right)
    {
        return (left.nProposalHash < right.nProposalHash);
    }
};

// Check finalized budget and vote on it if correct. Masternodes only
void CFinalizedBudget::CheckAndVote()
{
    LOCK(cs);

    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) return;

    LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::AutoCheck - %lli - %d\n", pindexPrev->nHeight, fAutoChecked);

    if (!fMasterNode || fAutoChecked) {
        LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::AutoCheck fMasterNode=%d fAutoChecked=%d\n", fMasterNode, fAutoChecked);
        return;
    }

    if (activeMasternode.vin == nullopt) {
        LogPrint(BCLog::MNBUDGET,"%s: Active Masternode not initialized.\n", __func__);
        return;
    }

    // Do this 1 in 4 blocks -- spread out the voting activity
    // -- this function is only called every fourteenth block, so this is really 1 in 56 blocks
    if (rand() % 4 != 0) {
        LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::AutoCheck - waiting\n");
        return;
    }

    fAutoChecked = true; //we only need to check this once

    if (strBudgetMode == "auto") //only vote for exact matches
    {
        std::vector<CBudgetProposal*> vBudgetProposals = governanceManager.GetBudget();

        // We have to resort the proposals by hash (they are sorted by votes here) and sort the payments
        // by hash (they are not sorted at all) to make the following tests deterministic
        // We're working on copies to avoid any side-effects by the possibly changed sorting order

        // Sort copy of proposals by hash
        std::vector<CBudgetProposal*> vBudgetProposalsSortedByHash(vBudgetProposals);
        std::sort(vBudgetProposalsSortedByHash.begin(), vBudgetProposalsSortedByHash.end(), sortProposalsByHash());

        // Sort copy payments by hash
        std::vector<CTxBudgetPayment> vecBudgetPaymentsSortedByHash(vecBudgetPayments);
        std::sort(vecBudgetPaymentsSortedByHash.begin(), vecBudgetPaymentsSortedByHash.end(), sortPaymentsByHash());

        for (unsigned int i = 0; i < vecBudgetPaymentsSortedByHash.size(); i++) {
            LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::AutoCheck Budget-Payments - nProp %d %s\n", i, vecBudgetPaymentsSortedByHash[i].nProposalHash.ToString());
            LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::AutoCheck Budget-Payments - Payee %d %s\n", i, HexStr(vecBudgetPaymentsSortedByHash[i].payee));
            LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::AutoCheck Budget-Payments - nAmount %d %lli\n", i, vecBudgetPaymentsSortedByHash[i].nAmount);
        }

        for (unsigned int i = 0; i < vBudgetProposalsSortedByHash.size(); i++) {
            LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::AutoCheck Budget-Proposals - nProp %d %s\n", i, vBudgetProposalsSortedByHash[i]->GetHash().ToString());
            LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::AutoCheck Budget-Proposals - Payee %d %s\n", i, HexStr(vBudgetProposalsSortedByHash[i]->GetPayee()));
            LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::AutoCheck Budget-Proposals - nAmount %d %lli\n", i, vBudgetProposalsSortedByHash[i]->GetAmount());
        }

        if (vBudgetProposalsSortedByHash.size() == 0) {
            LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::AutoCheck - No Budget-Proposals found, aborting\n");
            return;
        }

        if (vBudgetProposalsSortedByHash.size() != vecBudgetPaymentsSortedByHash.size()) {
            LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::AutoCheck - Budget-Proposal length (%ld) doesn't match Budget-Payment length (%ld).\n",
                      vBudgetProposalsSortedByHash.size(), vecBudgetPaymentsSortedByHash.size());
            return;
        }

        for (unsigned int i = 0; i < vecBudgetPaymentsSortedByHash.size(); i++) {
            if (i > vBudgetProposalsSortedByHash.size() - 1) {
                LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::AutoCheck - Proposal size mismatch, i=%d > (vBudgetProposals.size() - 1)=%d\n", i, vBudgetProposalsSortedByHash.size() - 1);
                return;
            }

            if (vecBudgetPaymentsSortedByHash[i].nProposalHash != vBudgetProposalsSortedByHash[i]->GetHash()) {
                LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::AutoCheck - item #%d doesn't match %s %s\n", i, vecBudgetPaymentsSortedByHash[i].nProposalHash.ToString(), vBudgetProposalsSortedByHash[i]->GetHash().ToString());
                return;
            }

            // if(vecBudgetPayments[i].payee != vBudgetProposals[i]->GetPayee()){ -- triggered with false positive
            if (HexStr(vecBudgetPaymentsSortedByHash[i].payee) != HexStr(vBudgetProposalsSortedByHash[i]->GetPayee())) {
                LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::AutoCheck - item #%d payee doesn't match %s %s\n", i, HexStr(vecBudgetPaymentsSortedByHash[i].payee), HexStr(vBudgetProposalsSortedByHash[i]->GetPayee()));
                return;
            }

            if (vecBudgetPaymentsSortedByHash[i].nAmount != vBudgetProposalsSortedByHash[i]->GetAmount()) {
                LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::AutoCheck - item #%d payee doesn't match %lli %lli\n", i, vecBudgetPaymentsSortedByHash[i].nAmount, vBudgetProposalsSortedByHash[i]->GetAmount());
                return;
            }
        }

        LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::AutoCheck - Finalized Budget Matches! Submitting Vote.\n");
        SubmitVote();
    }
}

// Remove votes from masternodes which are not valid/existent anymore
void CFinalizedBudget::CleanAndRemove()
{
    std::map<uint256, CFinalizedBudgetVote>::iterator it = mapVotes.begin();

    while (it != mapVotes.end()) {
        CMasternode* pmn = mnodeman.Find((*it).second.GetVin());
        (*it).second.fValid = (pmn != nullptr);
        ++it;
    }
}

CAmount CFinalizedBudget::GetTotalPayout()
{
    CAmount ret = 0;

    for (unsigned int i = 0; i < vecBudgetPayments.size(); i++) {
        ret += vecBudgetPayments[i].nAmount;
    }

    return ret;
}

std::string CFinalizedBudget::GetProposals()
{
    LOCK(cs);
    std::string ret = "";

    for (CTxBudgetPayment& budgetPayment : vecBudgetPayments) {
        CBudgetProposal* pbudgetProposal = governanceManager.FindProposal(budgetPayment.nProposalHash);

        std::string token = budgetPayment.nProposalHash.ToString();

        if (pbudgetProposal) token = pbudgetProposal->GetName();
        if (ret == "") {
            ret = token;
        } else {
            ret += "," + token;
        }
    }
    return ret;
}

std::string CFinalizedBudget::GetStatus()
{
    std::string retBadHashes = "";
    std::string retBadPayeeOrAmount = "";

    for (int nBlockHeight = GetBlockStart(); nBlockHeight <= GetBlockEnd(); nBlockHeight++) {
        CTxBudgetPayment budgetPayment;
        if (!GetBudgetPaymentByBlock(nBlockHeight, budgetPayment)) {
            LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::GetStatus - Couldn't find budget payment for block %lld\n", nBlockHeight);
            continue;
        }

        CBudgetProposal* pbudgetProposal = governanceManager.FindProposal(budgetPayment.nProposalHash);
        if (!pbudgetProposal) {
            if (retBadHashes == "") {
                retBadHashes = "Unknown proposal hash! Check this proposal before voting: " + budgetPayment.nProposalHash.ToString();
            } else {
                retBadHashes += "," + budgetPayment.nProposalHash.ToString();
            }
        } else {
            if (pbudgetProposal->GetPayee() != budgetPayment.payee || pbudgetProposal->GetAmount() != budgetPayment.nAmount) {
                if (retBadPayeeOrAmount == "") {
                    retBadPayeeOrAmount = "Budget payee/nAmount doesn't match our proposal! " + budgetPayment.nProposalHash.ToString();
                } else {
                    retBadPayeeOrAmount += "," + budgetPayment.nProposalHash.ToString();
                }
            }
        }
    }

    if (retBadHashes == "" && retBadPayeeOrAmount == "") return "OK";

    return retBadHashes + retBadPayeeOrAmount;
}

bool CFinalizedBudget::IsValid(std::string& strError, bool fCheckCollateral)
{
    // All(!) finalized budgets have the name "main", so get some additional information about them
    std::string strProposals = GetProposals();

    const int nBlocksPerCycle = Params().GetConsensus().nBudgetCycleBlocks;
    // Must be the correct block for payment to happen (once a month)
    if (nBlockStart % nBlocksPerCycle != 0) {
        strError = "Invalid BlockStart";
        return false;
    }

    // The following 2 checks check the same (basically if vecBudgetPayments.size() > 100)
    if (GetBlockEnd() - nBlockStart > 100) {
        strError = "Invalid BlockEnd";
        return false;
    }
    if ((int)vecBudgetPayments.size() > 100) {
        strError = "Invalid budget payments count (too many)";
        return false;
    }
    if (strBudgetName == "") {
        strError = "Invalid Budget Name";
        return false;
    }
    if (nBlockStart == 0) {
        strError = "Budget " + strBudgetName + " (" + strProposals + ") Invalid BlockStart == 0";
        return false;
    }
    if (nFeeTXHash.IsNull()) {
        strError = "Budget " + strBudgetName  + " (" + strProposals + ") Invalid FeeTx == 0";
        return false;
    }

    // Can only pay out 10% of the possible coins (min value of coins)
    if (GetTotalPayout() > governanceManager.GetTotalBudget(nBlockStart)) {
        strError = "Budget " + strBudgetName + " (" + strProposals + ") Invalid Payout (more than max)";
        return false;
    }

    std::string strError2 = "";
    if (fCheckCollateral) {
        int nConf = 0;
        if (!IsBudgetCollateralValid(nFeeTXHash, GetHash(), strError2, nTime, nConf, true)) {
            {
                strError = "Budget " + strBudgetName + " (" + strProposals + ") Invalid Collateral : " + strError2;
                return false;
            }
        }
    }

    // Remove obsolete finalized budgets after some time

    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev == NULL) return true;

    // Get start of current budget-cycle
    int nCurrentHeight = chainActive.Height();
    int nBlockStart = nCurrentHeight - nCurrentHeight % nBlocksPerCycle + nBlocksPerCycle;

    // Remove budgets where the last payment (from max. 100) ends before 2 budget-cycles before the current one
    int nMaxAge = nBlockStart - (2 * nBlocksPerCycle);

    if (GetBlockEnd() < nMaxAge) {
        strError = strprintf("Budget " + strBudgetName + " (" + strProposals + ") (ends at block %ld) too old and obsolete", GetBlockEnd());
        return false;
    }

    return true;
}

bool CFinalizedBudget::IsPaidAlready(uint256 nProposalHash, int nBlockHeight)
{
    // Remove budget-payments from former/future payment cycles
    std::map<uint256, int>::iterator it = mapPayment_History.begin();
    int nPaidBlockHeight = 0;
    uint256 nOldProposalHash;

    for(it = mapPayment_History.begin(); it != mapPayment_History.end(); /* No incrementation needed */ ) {
        nPaidBlockHeight = (*it).second;
        if((nPaidBlockHeight < GetBlockStart()) || (nPaidBlockHeight > GetBlockEnd())) {
            nOldProposalHash = (*it).first;
            LogPrint(BCLog::MNBUDGET, "CFinalizedBudget::IsPaidAlready - Budget Proposal %s, Block %d from old cycle deleted\n",
                      nOldProposalHash.ToString().c_str(), nPaidBlockHeight);
            mapPayment_History.erase(it++);
        }
        else {
            ++it;
        }
    }

    // Now that we only have payments from the current payment cycle check if this budget was paid already
    if(mapPayment_History.count(nProposalHash) == 0) {
        // New proposal payment, insert into map for checks with later blocks from this cycle
        mapPayment_History.insert(std::pair<uint256, int>(nProposalHash, nBlockHeight));
        LogPrint(BCLog::MNBUDGET, "CFinalizedBudget::IsPaidAlready - Budget Proposal %s, Block %d added to payment history\n",
                  nProposalHash.ToString().c_str(), nBlockHeight);
        return false;
    }
    // This budget was paid already -> reject transaction so it gets paid to a masternode instead
    return true;
}

TrxValidationStatus CFinalizedBudget::IsTransactionValid(const CTransaction& txNew, int nBlockHeight)
{
    TrxValidationStatus transactionStatus = TrxValidationStatus::InValid;
    int nCurrentBudgetPayment = nBlockHeight - GetBlockStart();
    if (nCurrentBudgetPayment < 0) {
        LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::IsTransactionValid - Invalid block - height: %d start: %d\n", nBlockHeight, GetBlockStart());
        return TrxValidationStatus::InValid;
    }

    if (nCurrentBudgetPayment > (int)vecBudgetPayments.size() - 1) {
        LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::IsTransactionValid - Invalid last block - current budget payment: %d of %d\n", nCurrentBudgetPayment + 1, (int)vecBudgetPayments.size());
        return TrxValidationStatus::InValid;
    }

    bool paid = false;

    for (CTxOut out : txNew.vout) {
        LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::IsTransactionValid - nCurrentBudgetPayment=%d, payee=%s == out.scriptPubKey=%s, amount=%ld == out.nValue=%ld\n",
                 nCurrentBudgetPayment, HexStr(vecBudgetPayments[nCurrentBudgetPayment].payee), HexStr(out.scriptPubKey),
                 vecBudgetPayments[nCurrentBudgetPayment].nAmount, out.nValue);

        if (vecBudgetPayments[nCurrentBudgetPayment].payee == out.scriptPubKey && vecBudgetPayments[nCurrentBudgetPayment].nAmount == out.nValue) {
            // Check if this proposal was paid already. If so, pay a masternode instead
            paid = IsPaidAlready(vecBudgetPayments[nCurrentBudgetPayment].nProposalHash, nBlockHeight);
            if(paid) {
                LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::IsTransactionValid - Double Budget Payment of %d for proposal %d detected. Paying a masternode instead.\n",
                          vecBudgetPayments[nCurrentBudgetPayment].nAmount, vecBudgetPayments[nCurrentBudgetPayment].nProposalHash.GetHex());
                // No matter what we've found before, stop all checks here. In future releases there might be more than one budget payment
                // per block, so even if the first one was not paid yet this one disables all budget payments for this block.
                transactionStatus = TrxValidationStatus::DoublePayment;
                break;
            }
            else {
                transactionStatus = TrxValidationStatus::Valid;
                LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::IsTransactionValid - Found valid Budget Payment of %d for proposal %d\n",
                          vecBudgetPayments[nCurrentBudgetPayment].nAmount, vecBudgetPayments[nCurrentBudgetPayment].nProposalHash.GetHex());
            }
        }
    }

    if (transactionStatus == TrxValidationStatus::InValid) {
        CTxDestination address1;
        ExtractDestination(vecBudgetPayments[nCurrentBudgetPayment].payee, address1);

        LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::IsTransactionValid - Missing required payment - %s: %d c: %d\n",
                  EncodeDestination(address1), vecBudgetPayments[nCurrentBudgetPayment].nAmount, nCurrentBudgetPayment);
    }

    return transactionStatus;
}

void CFinalizedBudget::SubmitVote()
{
    // function called only from initialized masternodes
    assert(fMasterNode && activeMasternode.vin != nullopt);

    std::string strError = "";
    CPubKey pubKeyMasternode;
    CKey keyMasternode;

    if (!CMessageSigner::GetKeysFromSecret(strMasterNodePrivKey, keyMasternode, pubKeyMasternode)) {
        LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::SubmitVote - Error upon calling GetKeysFromSecret\n");
        return;
    }

    CFinalizedBudgetVote vote(*(activeMasternode.vin), GetHash());
    if (!vote.Sign(keyMasternode, pubKeyMasternode)) {
        LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::SubmitVote - Failure to sign.");
        return;
    }

    if (governanceManager.UpdateFinalizedBudget(vote, NULL, strError)) {
        LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::SubmitVote  - new finalized budget vote - %s\n", vote.GetHash().ToString());

        governanceManager.mapSeenFinalizedBudgetVotes.insert(std::make_pair(vote.GetHash(), vote));
        vote.Relay();
    } else {
        LogPrint(BCLog::MNBUDGET,"CFinalizedBudget::SubmitVote : Error submitting vote - %s\n", strError);
    }
}

CFinalizedBudgetBroadcast::CFinalizedBudgetBroadcast() :
        CFinalizedBudget()
{ }

CFinalizedBudgetBroadcast::CFinalizedBudgetBroadcast(const CFinalizedBudget& other) :
        CFinalizedBudget(other)
{ }

CFinalizedBudgetBroadcast::CFinalizedBudgetBroadcast(std::string strBudgetNameIn,
                                                     int nBlockStartIn,
                                                     std::vector<CTxBudgetPayment> vecBudgetPaymentsIn,
                                                     uint256 nFeeTXHashIn)
{
    strBudgetName = strBudgetNameIn;
    nBlockStart = nBlockStartIn;
    for (CTxBudgetPayment out : vecBudgetPaymentsIn)
        vecBudgetPayments.push_back(out);
    nFeeTXHash = nFeeTXHashIn;
}

void CFinalizedBudgetBroadcast::Relay()
{
    CInv inv(MSG_BUDGET_FINALIZED, GetHash());
    g_connman->RelayInv(inv);
}

CFinalizedBudgetVote::CFinalizedBudgetVote() :
        CSignedMessage(),
        fValid(true),
        fSynced(false),
        vin(),
        nBudgetHash(),
        nTime(0)
{ }

CFinalizedBudgetVote::CFinalizedBudgetVote(CTxIn vinIn, uint256 nBudgetHashIn) :
        CSignedMessage(),
        fValid(true),
        fSynced(false),
        vin(vinIn),
        nBudgetHash(nBudgetHashIn)
{
    nTime = GetAdjustedTime();
}

void CFinalizedBudgetVote::Relay()
{
    CInv inv(MSG_BUDGET_FINALIZED_VOTE, GetHash());
    g_connman->RelayInv(inv);
}

uint256 CFinalizedBudgetVote::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << nBudgetHash;
    ss << nTime;
    return ss.GetHash();
}

std::string CFinalizedBudgetVote::GetStrMessage() const
{
    return vin.prevout.ToStringShort() + nBudgetHash.ToString() + std::to_string(nTime);
}
