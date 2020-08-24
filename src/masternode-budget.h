// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODE_BUDGET_H
#define MASTERNODE_BUDGET_H

#include "base58.h"
#include "governance/governance.h"
#include "init.h"
#include "key.h"
#include "main.h"
#include "masternode.h"
#include "net.h"
#include "sync.h"
#include "util.h"

class CBudgetManager;
class CFinalizedBudget;
class CFinalizedBudgetBroadcast;
class CFinalizedBudgetVote;
class CTxBudgetPayment;
class CWalletTx;

static const CAmount PROPOSAL_FEE_TX = (50 * COIN);
static const CAmount BUDGET_FEE_TX_OLD = (50 * COIN);
static const CAmount BUDGET_FEE_TX = (5 * COIN);
static const int64_t BUDGET_VOTE_UPDATE_MIN = 60 * 60;
static std::map<uint256, int> mapPayment_History;

extern std::vector<CFinalizedBudgetBroadcast> vecImmatureFinalizedBudgets;
extern CBudgetManager budgetManager;

enum class TrxValidationStatus {
    InValid,         /** Transaction verification failed */
    Valid,           /** Transaction successfully verified */
    DoublePayment,   /** Transaction successfully verified, but includes a double-budget-payment */
    VoteThreshold    /** If not enough masternodes have voted on a finalized budget */
};

/**
 * Save Budget Manager (budget.dat)
 */
class CBudgetDB
{
private:
    fs::path pathDB;
    std::string strMagicMessage;

public:
    enum ReadResult {
        Ok,
        FileError,
        HashReadError,
        IncorrectHash,
        IncorrectMagicMessage,
        IncorrectMagicNumber,
        IncorrectFormat
    };

    CBudgetDB();
    bool Write(const CBudgetManager& objToSave);
    ReadResult Read(CBudgetManager& objToLoad, bool fDryRun = false);
};

/**
* Finalized Budget Manager
* -------------------------------------------------------
*
* This object is responsible for finalization of the budget system. It's built to be completely separate from
* the governance system, to eliminate any network differences.
*/

class CBudgetManager
{
private:
    // hold txes until they mature enough to use
    std::map<uint256, uint256> mapCollateralTxids;

public:
    // critical section to protect the inner data structures
    mutable RecursiveMutex cs;

    std::map<uint256, CFinalizedBudget> mapFinalizedBudgets;

    std::map<uint256, CFinalizedBudget> mapSeenFinalizedBudgets;
    std::map<uint256, CFinalizedBudgetVote> mapSeenFinalizedBudgetVotes;
    std::map<uint256, CFinalizedBudgetVote> mapOrphanFinalizedBudgetVotes;

    CBudgetManager()
    {
        mapFinalizedBudgets.clear();
    }

    void ClearSeen()
    {
        mapSeenFinalizedBudgets.clear();
        mapSeenFinalizedBudgetVotes.clear();
    }

    int sizeFinalized() { return (int)mapFinalizedBudgets.size(); }

    void ResetSync();
    void MarkSynced();
    void Sync(CNode* node, uint256 nProp, bool fPartial=false);

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    void NewBlock();

    CFinalizedBudget *FindFinalizedBudget(const uint256& nHash);

    std::vector<CFinalizedBudget*> GetFinalizedBudgets();

    void SubmitFinalBudget();
    bool UpdateFinalizedBudget(CFinalizedBudgetVote& vote, CNode* pfrom, std::string& strError);

    bool IsBudgetPaymentBlock(int nBlockHeight);
    TrxValidationStatus IsTransactionValid(const CTransaction& txNew, int nBlockHeight);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txNew, CAmount nFees, bool fProofOfStake);

    void CheckOrphanVotes();
    void Clear()
    {
        LOCK(cs);

        LogPrintf("Budget Manager object cleared\n");
        mapFinalizedBudgets.clear();
        mapSeenFinalizedBudgets.clear();
        mapSeenFinalizedBudgetVotes.clear();
        mapOrphanFinalizedBudgetVotes.clear();
    }
    void CheckAndRemove();
    std::string ToString() const;


    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(mapSeenFinalizedBudgets);
        READWRITE(mapSeenFinalizedBudgetVotes);
        READWRITE(mapOrphanFinalizedBudgetVotes);
        READWRITE(mapFinalizedBudgets);
    }

private:
    bool AddFinalizedBudget(CFinalizedBudget& finalizedBudget);
};

class CTxBudgetPayment
{
public:
    uint256 nProposalHash;
    CScript payee;
    CAmount nAmount;

    CTxBudgetPayment()
    {
        payee = CScript();
        nAmount = 0;
        nProposalHash = UINT256_ZERO;
    }

    ADD_SERIALIZE_METHODS;

    //for saving to the serialized db
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(*(CScriptBase*)(&payee));
        READWRITE(nAmount);
        READWRITE(nProposalHash);
    }
};

/*
 * CFinalizedBudgetVote - Allow a masternode node to vote and broadcast throughout the network
 */

class CFinalizedBudgetVote : public CSignedMessage
{
public:
    bool fValid;  //if the vote is currently valid / counted
    bool fSynced; //if we've sent this to our peers
    CTxIn vin;
    uint256 nBudgetHash;
    int64_t nTime;

    CFinalizedBudgetVote();
    CFinalizedBudgetVote(CTxIn vinIn, uint256 nBudgetHashIn);

    void Relay();
    uint256 GetHash() const;

    // override CSignedMessage functions
    uint256 GetSignatureHash() const override { return GetHash(); }
    std::string GetStrMessage() const override;
    const CTxIn GetVin() const override { return vin; };

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(vin);
        READWRITE(nBudgetHash);
        READWRITE(nTime);
        READWRITE(vchSig);
        try
        {
            READWRITE(nMessVersion);
        } catch (...) {
            nMessVersion = MessageVersion::MESS_VER_STRMESS;
        }
    }
};

/*
 * Finalized Budget : Contains the suggested proposals to pay on a given block
 */

class CFinalizedBudget
{
private:
    // critical section to protect the inner data structures
    mutable RecursiveMutex cs;
    bool fAutoChecked; //If it matches what we see, we'll auto vote for it (masternode only)

public:
    bool fValid;
    std::string strBudgetName;
    int nBlockStart;
    std::vector<CTxBudgetPayment> vecBudgetPayments;
    std::map<uint256, CFinalizedBudgetVote> mapVotes;
    uint256 nFeeTXHash;
    int64_t nTime;

    CFinalizedBudget();
    CFinalizedBudget(const CFinalizedBudget& other);

    void CleanAndRemove();
    bool AddOrUpdateVote(CFinalizedBudgetVote& vote, std::string& strError);
    double GetScore();
    bool HasMinimumRequiredSupport();

    bool IsValid(std::string& strError, bool fCheckCollateral = true);

    std::string GetName() { return strBudgetName; }
    std::string GetProposals();
    int GetBlockStart() { return nBlockStart; }
    int GetBlockEnd() { return nBlockStart + (int)(vecBudgetPayments.size() - 1); }
    int GetVoteCount() { return (int)mapVotes.size(); }
    bool IsPaidAlready(uint256 nProposalHash, int nBlockHeight);
    TrxValidationStatus IsTransactionValid(const CTransaction& txNew, int nBlockHeight);
    bool GetBudgetPaymentByBlock(int64_t nBlockHeight, CTxBudgetPayment& payment)
    {
        LOCK(cs);

        int i = nBlockHeight - GetBlockStart();
        if (i < 0) return false;
        if (i > (int)vecBudgetPayments.size() - 1) return false;
        payment = vecBudgetPayments[i];
        return true;
    }
    bool GetPayeeAndAmount(int64_t nBlockHeight, CScript& payee, CAmount& nAmount)
    {
        LOCK(cs);

        int i = nBlockHeight - GetBlockStart();
        if (i < 0) return false;
        if (i > (int)vecBudgetPayments.size() - 1) return false;
        payee = vecBudgetPayments[i].payee;
        nAmount = vecBudgetPayments[i].nAmount;
        return true;
    }

    // Verify and vote on finalized budget
    void CheckAndVote();
    //total pivx paid out by this budget
    CAmount GetTotalPayout();
    //vote on this finalized budget as a masternode
    void SubmitVote();

    //checks the hashes to make sure we know about them
    std::string GetStatus();

    uint256 GetHash()
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << strBudgetName;
        ss << nBlockStart;
        ss << vecBudgetPayments;

        uint256 h1 = ss.GetHash();
        return h1;
    }

    ADD_SERIALIZE_METHODS;

    //for saving to the serialized db
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(LIMITED_STRING(strBudgetName, 20));
        READWRITE(nFeeTXHash);
        READWRITE(nTime);
        READWRITE(nBlockStart);
        READWRITE(vecBudgetPayments);
        READWRITE(fAutoChecked);

        READWRITE(mapVotes);
    }
};

// FinalizedBudget are cast then sent to peers with this object, which leaves the votes out
class CFinalizedBudgetBroadcast : public CFinalizedBudget
{
public:
    CFinalizedBudgetBroadcast();
    CFinalizedBudgetBroadcast(const CFinalizedBudget& other);
    CFinalizedBudgetBroadcast(std::string strBudgetNameIn, int nBlockStartIn, std::vector<CTxBudgetPayment> vecBudgetPaymentsIn, uint256 nFeeTXHashIn);

    void swap(CFinalizedBudgetBroadcast& first, CFinalizedBudgetBroadcast& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.strBudgetName, second.strBudgetName);
        swap(first.nBlockStart, second.nBlockStart);
        first.mapVotes.swap(second.mapVotes);
        first.vecBudgetPayments.swap(second.vecBudgetPayments);
        swap(first.nFeeTXHash, second.nFeeTXHash);
        swap(first.nTime, second.nTime);
    }

    CFinalizedBudgetBroadcast& operator=(CFinalizedBudgetBroadcast from)
    {
        swap(*this, from);
        return *this;
    }

    void Relay();

    ADD_SERIALIZE_METHODS;

    //for propagating messages
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        //for syncing with other clients
        READWRITE(LIMITED_STRING(strBudgetName, 20));
        READWRITE(nBlockStart);
        READWRITE(vecBudgetPayments);
        READWRITE(nFeeTXHash);
    }
};

void DumpBudgets();

#endif
