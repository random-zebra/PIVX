// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef GOVERNANCE_H
#define GOVERNANCE_H

#include "base58.h"
#include "init.h"
#include "key.h"
#include "main.h"
#include "masternode.h"
#include "masternode-budget.h"  // !TODO: remove circular dependency after split
#include "net.h"
#include "sync.h"
#include "util.h"

#include <univalue.h>

#define VOTE_ABSTAIN 0
#define VOTE_YES 1
#define VOTE_NO 2

class CGovernanceManager;
class CBudgetProposal;
class CBudgetProposalBroadcast;
class CBudgetVote;

extern std::map<uint256, int64_t> askedForSourceProposalOrBudget;
extern std::vector<CBudgetProposalBroadcast> vecImmatureBudgetProposals;
extern CGovernanceManager governanceManager;

//Check the collateral transaction for the budget proposal/finalized budget
bool IsBudgetCollateralValid(uint256 nTxCollateralHash, uint256 nExpectedHash, std::string& strError, int64_t& nTime, int& nConf, bool fBudgetFinalization=false);

/**
 * Save Budget Manager (budget.dat) - remove duplication with gov manager
 */
class CGovernanceDB
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

    CGovernanceDB();
    bool Write(const CGovernanceManager& objToSave) const;
    ReadResult Read(CGovernanceManager& objToLoad, bool fDryRun = false) const;
};

/*
 * Budget Manager : Contains all proposals for the budget
 */
class CGovernanceManager
{
private:
    // hold txes until they mature enough to use
    std::map<uint256, uint256> mapCollateralTxids;

    // keep track of the scanning errors I've seen
    std::map<uint256, CBudgetProposal> mapProposals;

    std::map<uint256, CBudgetProposal> mapSeenMasternodeBudgetProposals;
    std::map<uint256, CBudgetVote> mapSeenMasternodeBudgetVotes;
    std::map<uint256, CBudgetVote> mapOrphanMasternodeBudgetVotes;

public:
    // critical section to protect the inner data structures
    mutable RecursiveMutex cs;

    CGovernanceManager()
    {
        mapProposals.clear();
    }

    void ClearSeen()
    {
        mapSeenMasternodeBudgetProposals.clear();
        mapSeenMasternodeBudgetVotes.clear();
    }

    bool HaveSeenProposal(const uint256& hash) const;
    bool HaveSeenVote(const uint256& hash) const;

    void AddSeenProposal(CBudgetProposal& budgetProposal);
    void AddSeenVote(CBudgetVote& vote);

    // hash must be in relative map (proposal/votes)
    CDataStream GetProposalSerialized(const uint256& hash) const;
    CDataStream GetVoteSerialized(const uint256& hash) const;

    int sizeProposals() const { return (int)mapProposals.size(); }

    void ResetSync();
    void MarkSynced();
    void Sync(CNode* node, uint256 nProp, bool fPartial = false);

    void Calculate();
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    void NewBlock();

    CBudgetProposal* FindProposal(const uint256& nHash);
    const CBudgetProposal* GetProposal(const uint256& nHash) const;
    const CBudgetProposal* GetProposal(const std::string& strProposalName) const;

    static CAmount GetTotalBudget(int nHeight);
    std::vector<CBudgetProposal*> GetBudget();
    std::vector<CBudgetProposal*> GetAllProposals();

    bool AddProposal(CBudgetProposal& budgetProposal);
    bool UpdateProposal(CBudgetVote& vote, CNode* pfrom, std::string& strError);

    void CheckOrphanVotes();
    void Clear()
    {
        LOCK(cs);

        LogPrintf("Governance Manager object cleared\n");
        mapProposals.clear();
        mapSeenMasternodeBudgetProposals.clear();
        mapSeenMasternodeBudgetVotes.clear();
        mapOrphanMasternodeBudgetVotes.clear();
    }
    void CheckAndRemove();
    std::string ToString() const;


    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(mapSeenMasternodeBudgetProposals);
        READWRITE(mapSeenMasternodeBudgetVotes);
        READWRITE(mapOrphanMasternodeBudgetVotes);
        READWRITE(mapProposals);
    }
};

/*
 * Budget Proposal : Contains the masternode votes for each budget
 */

class CBudgetProposal
{
private:
    // critical section to protect the inner data structures
    mutable RecursiveMutex cs;
    CAmount nAlloted;

public:
    bool fValid;
    std::string strProposalName;

    /*
        json object with name, short-description, long-description, pdf-url and any other info
        This allows the proposal website to stay 100% decentralized
    */
    std::string strURL;
    int nBlockStart;
    int nBlockEnd;
    CAmount nAmount;
    CScript address;
    int64_t nTime;
    std::string strInvalid;
    uint256 nFeeTXHash;

    std::map<uint256, CBudgetVote> mapVotes;
    //cache object

    CBudgetProposal();
    CBudgetProposal(const CBudgetProposal& other);
    CBudgetProposal(std::string strProposalNameIn, std::string strURLIn, int nBlockStartIn, int nBlockEndIn, CScript addressIn, CAmount nAmountIn, uint256 nFeeTXHashIn);

    void Calculate();
    bool AddOrUpdateVote(CBudgetVote& vote, std::string& strError);
    bool HasMinimumRequiredSupport();
    std::pair<std::string, std::string> GetVotes();

    /*
     * set fValid and strInvalid. return fValid
     * if fSkipCollateral is false (default), set also nTime and nConf
     */
    bool UpdateValid(int& nConf, bool fSkipCollateral = false);
    bool IsValid() const { return fValid; }
    std::string IsInvalidReason() const { return strInvalid; }

    bool IsEstablished() const;
    bool IsPassing(const CBlockIndex* pindexPrev, int nBlockStartBudget, int nBlockEndBudget, int mnCount);

    std::string GetName() const { return strProposalName; }
    std::string GetURL() const { return strURL; }
    int GetBlockStart() const { return nBlockStart; }
    int GetBlockEnd() const { return nBlockEnd; }
    CScript GetPayee() const { return address; }
    int GetTotalPaymentCount() const;
    int GetRemainingPaymentCount() const;
    int GetBlockStartCycle() const;
    int GetBlockCurrentCycle() const;
    int GetBlockEndCycle() const;
    double GetRatio() const;
    UniValue GetVotesArray() const;
    int GetYeas() const;
    int GetNays() const;
    int GetAbstains() const;
    CAmount GetAmount() const { return nAmount; }
    void SetAllotted(CAmount nAllotedIn) { nAlloted = nAllotedIn; }
    CAmount GetAllotted() const { return nAlloted; }

    void CleanAndRemove();

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << strProposalName;
        ss << strURL;
        ss << nBlockStart;
        ss << nBlockEnd;
        ss << nAmount;
        ss << std::vector<unsigned char>(address.begin(), address.end());
        uint256 h1 = ss.GetHash();

        return h1;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        //for syncing with other clients
        READWRITE(LIMITED_STRING(strProposalName, 20));
        READWRITE(LIMITED_STRING(strURL, 64));
        READWRITE(nTime);
        READWRITE(nBlockStart);
        READWRITE(nBlockEnd);
        READWRITE(nAmount);
        READWRITE(*(CScriptBase*)(&address));
        READWRITE(nTime);
        READWRITE(nFeeTXHash);

        //for saving to the serialized db
        READWRITE(mapVotes);
    }
};

// Proposals are cast then sent to peers with this object, which leaves the votes out
class CBudgetProposalBroadcast : public CBudgetProposal
{
public:
    CBudgetProposalBroadcast() : CBudgetProposal() {}
    CBudgetProposalBroadcast(const CBudgetProposal& other) : CBudgetProposal(other) {}
    CBudgetProposalBroadcast(const CBudgetProposalBroadcast& other) : CBudgetProposal(other) {}
    CBudgetProposalBroadcast(std::string strProposalNameIn, std::string strURLIn, int nPaymentCount, CScript addressIn, CAmount nAmountIn, int nBlockStartIn, uint256 nFeeTXHashIn);

    void swap(CBudgetProposalBroadcast& first, CBudgetProposalBroadcast& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.strProposalName, second.strProposalName);
        swap(first.nBlockStart, second.nBlockStart);
        swap(first.strURL, second.strURL);
        swap(first.nBlockEnd, second.nBlockEnd);
        swap(first.nAmount, second.nAmount);
        swap(first.address, second.address);
        swap(first.nTime, second.nTime);
        swap(first.nFeeTXHash, second.nFeeTXHash);
        first.mapVotes.swap(second.mapVotes);
    }

    CBudgetProposalBroadcast& operator=(CBudgetProposalBroadcast from)
    {
        swap(*this, from);
        return *this;
    }

    void Relay();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        //for syncing with other clients

        READWRITE(LIMITED_STRING(strProposalName, 20));
        READWRITE(LIMITED_STRING(strURL, 64));
        READWRITE(nTime);
        READWRITE(nBlockStart);
        READWRITE(nBlockEnd);
        READWRITE(nAmount);
        READWRITE(*(CScriptBase*)(&address));
        READWRITE(nFeeTXHash);
    }
};

/*
 * CBudgetVote - Allow a masternode node to vote and broadcast throughout the network
 */
class CBudgetVote : public CSignedMessage
{
public:
    bool fValid;  //if the vote is currently valid / counted
    bool fSynced; //if we've sent this to our peers
    CTxIn vin;
    uint256 nProposalHash;
    int nVote;
    int64_t nTime;

    CBudgetVote();
    CBudgetVote(CTxIn vin, uint256 nProposalHash, int nVoteIn);

    void Relay();

    std::string GetVoteString() const
    {
        std::string ret = "ABSTAIN";
        if (nVote == VOTE_YES) ret = "YES";
        if (nVote == VOTE_NO) ret = "NO";
        return ret;
    }

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
        READWRITE(nProposalHash);
        READWRITE(nVote);
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

#endif
