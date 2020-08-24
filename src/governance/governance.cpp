// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "init.h"
#include "main.h"

#include "addrman.h"
#include "chainparams.h"
#include "fs.h"
#include "governance/governance.h"
#include "masternode-budget.h"
#include "masternode-sync.h"
#include "masternode.h"
#include "masternodeman.h"
#include "util.h"


CGovernanceManager governanceManager;
RecursiveMutex cs_governance;

std::map<uint256, int64_t> askedForSourceProposalOrBudget;
std::vector<CBudgetProposalBroadcast> vecImmatureBudgetProposals;

bool IsBudgetCollateralValid(uint256 nTxCollateralHash, uint256 nExpectedHash, std::string& strError, int64_t& nTime, int& nConf, bool fBudgetFinalization)
{
    CTransaction txCollateral;
    uint256 nBlockHash;
    if (!GetTransaction(nTxCollateralHash, txCollateral, nBlockHash, true)) {
        strError = strprintf("Can't find collateral tx %s", txCollateral.ToString());
        LogPrint(BCLog::MNBUDGET,"CBudgetProposalBroadcast::IsBudgetCollateralValid - %s\n", strError);
        return false;
    }

    if (txCollateral.vout.size() < 1) return false;
    if (txCollateral.nLockTime != 0) return false;

    CScript findScript;
    findScript << OP_RETURN << ToByteVector(nExpectedHash);

    bool foundOpReturn = false;
    for (const CTxOut &o : txCollateral.vout) {
        if (!o.scriptPubKey.IsNormalPaymentScript() && !o.scriptPubKey.IsUnspendable()) {
            strError = strprintf("Invalid Script %s", txCollateral.ToString());
            LogPrint(BCLog::MNBUDGET,"CBudgetProposalBroadcast::IsBudgetCollateralValid - %s\n", strError);
            return false;
        }
        if (fBudgetFinalization) {
            // Collateral for budget finalization
            // Note: there are still old valid budgets out there, but the check for the new 5 PIV finalization collateral
            //       will also cover the old 50 PIV finalization collateral.
            LogPrint(BCLog::MNBUDGET, "Final Budget: o.scriptPubKey(%s) == findScript(%s) ?\n", HexStr(o.scriptPubKey), HexStr(findScript));
            if (o.scriptPubKey == findScript) {
                LogPrint(BCLog::MNBUDGET, "Final Budget: o.nValue(%ld) >= BUDGET_FEE_TX(%ld) ?\n", o.nValue, BUDGET_FEE_TX);
                if(o.nValue >= BUDGET_FEE_TX) {
                    foundOpReturn = true;
                }
            }
        }
        else {
            // Collateral for normal budget proposal
            LogPrint(BCLog::MNBUDGET, "Normal Budget: o.scriptPubKey(%s) == findScript(%s) ?\n", HexStr(o.scriptPubKey), HexStr(findScript));
            if (o.scriptPubKey == findScript) {
                LogPrint(BCLog::MNBUDGET, "Normal Budget: o.nValue(%ld) >= PROPOSAL_FEE_TX(%ld) ?\n", o.nValue, PROPOSAL_FEE_TX);
                if(o.nValue >= PROPOSAL_FEE_TX) {
                    foundOpReturn = true;
                }
            }
        }
    }
    if (!foundOpReturn) {
        strError = strprintf("Couldn't find opReturn %s in %s", nExpectedHash.ToString(), txCollateral.ToString());
        LogPrint(BCLog::MNBUDGET,"CBudgetProposalBroadcast::IsBudgetCollateralValid - %s\n", strError);
        return false;
    }

    // RETRIEVE CONFIRMATIONS AND NTIME
    /*
        - nTime starts as zero and is passed-by-reference out of this function and stored in the external proposal
        - nTime is never validated via the hashing mechanism and comes from a full-validated source (the blockchain)
    */

    int conf = GetIXConfirmations(nTxCollateralHash);
    if (!nBlockHash.IsNull()) {
        BlockMap::iterator mi = mapBlockIndex.find(nBlockHash);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex)) {
                conf += chainActive.Height() - pindex->nHeight + 1;
                nTime = pindex->nTime;
            }
        }
    }

    nConf = conf;

    //if we're syncing we won't have swiftTX information, so accept 1 confirmation
    const int nRequiredConfs = Params().GetConsensus().nBudgetFeeConfirmations;
    if (conf >= nRequiredConfs) {
        return true;
    } else {
        strError = strprintf("Collateral requires at least %d confirmations - %d confirmations", nRequiredConfs, conf);
        LogPrint(BCLog::MNBUDGET,"CBudgetProposalBroadcast::IsBudgetCollateralValid - %s - %d confirmations\n", strError, conf);
        return false;
    }
}

void CGovernanceManager::CheckOrphanVotes()
{
    LOCK(cs);

    std::string strError = "";
    auto it = mapOrphanMasternodeBudgetVotes.begin();
    while (it != mapOrphanMasternodeBudgetVotes.end()) {
        if (UpdateProposal(((*it).second), nullptr, strError)) {
            LogPrint(BCLog::MNBUDGET,"%s: Proposal/Budget is known, activating and removing orphan vote\n", __func__);
            mapOrphanMasternodeBudgetVotes.erase(it++);
        } else {
            ++it;
        }
    }
}

//
// CGovernanceDB
//

CGovernanceDB::CGovernanceDB()
{
    pathDB = GetDataDir() / "governance.dat";
    strMagicMessage = "MasternodeGovernance";
}

bool CGovernanceDB::Write(const CGovernanceManager& objToSave) const
{
    LOCK(objToSave.cs);

    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssObj(SER_DISK, CLIENT_VERSION);
    ssObj << strMagicMessage;                   // masternode cache file specific magic message
    ssObj << FLATDATA(Params().MessageStart()); // network specific magic number
    ssObj << objToSave;
    uint256 hash = Hash(ssObj.begin(), ssObj.end());
    ssObj << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fsbridge::fopen(pathDB, "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathDB.string());

    // Write and commit header, data
    try {
        fileout << ssObj;
    } catch (const std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    fileout.fclose();

    LogPrint(BCLog::MNBUDGET,"Written info to budget.dat  %dms\n", GetTimeMillis() - nStart);

    return true;
}

CGovernanceDB::ReadResult CGovernanceDB::Read(CGovernanceManager& objToLoad, bool fDryRun) const
{
    LOCK(objToLoad.cs);

    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE* file = fsbridge::fopen(pathDB, "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        error("%s : Failed to open file %s", __func__, pathDB.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = fs::file_size(pathDB);
    int dataSize = fileSize - sizeof(uint256);
    // Don't try to resize to a negative number if file is small
    if (dataSize < 0)
        dataSize = 0;
    std::vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char*)&vchData[0], dataSize);
        filein >> hashIn;
    } catch (const std::exception& e) {
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return HashReadError;
    }
    filein.fclose();

    CDataStream ssObj(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssObj.begin(), ssObj.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }


    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (masternode cache file specific magic message) and ..
        ssObj >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid masternode cache magic message", __func__);
            return IncorrectMagicMessage;
        }


        // de-serialize file header (network specific magic number) and ..
        ssObj >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }

        // de-serialize data into CGovernanceManager object
        ssObj >> objToLoad;
    } catch (const std::exception& e) {
        objToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint(BCLog::MNBUDGET,"Loaded info from budget.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint(BCLog::MNBUDGET,"  %s\n", objToLoad.ToString());
    if (!fDryRun) {
        LogPrint(BCLog::MNBUDGET,"Budget manager - cleaning....\n");
        objToLoad.CheckAndRemove();
        LogPrint(BCLog::MNBUDGET,"Budget manager - result:\n");
        LogPrint(BCLog::MNBUDGET,"  %s\n", objToLoad.ToString());
    }

    return Ok;
}

bool CGovernanceManager::AddProposal(CBudgetProposal& budgetProposal)
{
    LOCK(cs);
    int nConf = 0;
    if (!budgetProposal.UpdateValid(nConf)) {
        LogPrint(BCLog::MNBUDGET,"%s: invalid budget proposal - %s\n", __func__, budgetProposal.IsInvalidReason());
        return false;
    }

    if (mapProposals.count(budgetProposal.GetHash())) {
        return false;
    }

    mapProposals.insert(std::make_pair(budgetProposal.GetHash(), budgetProposal));
    LogPrint(BCLog::MNBUDGET,"%s: proposal %s added\n", __func__, budgetProposal.GetName().c_str ());
    return true;
}

void CGovernanceManager::CheckAndRemove()
{
    int nHeight = WITH_LOCK(cs_main, return chainActive.Height());
    if (nHeight <= 0) return;

    LogPrint(BCLog::MNBUDGET, "%s: Height=%d\n", __func__, nHeight);

    auto it = mapProposals.begin();
    while (it != mapProposals.end()) {
        int nConf = 0;
        CBudgetProposal* pbudgetProposal = &((*it).second);
        pbudgetProposal->UpdateValid(nConf);
        ++it;
    }
}

const CBudgetProposal* CGovernanceManager::GetProposal(const std::string& strProposalName) const
{
    LOCK(cs);
    //find the prop with the highest yes count
    int nYesCount = -99999;
    const CBudgetProposal* pbudgetProposal = nullptr;

    auto it = mapProposals.begin();
    while (it != mapProposals.end()) {
        if ((*it).second.strProposalName == strProposalName && (*it).second.GetYeas() > nYesCount) {
            pbudgetProposal = &((*it).second);
            nYesCount = pbudgetProposal->GetYeas();
        }
        ++it;
    }

    if (nYesCount == -99999) return nullptr;

    return pbudgetProposal;
}

const CBudgetProposal* CGovernanceManager::GetProposal(const uint256& nHash) const
{
    LOCK(cs);
    auto it = mapProposals.find(nHash);
    return (it != mapProposals.end() ? &it->second : nullptr);
}

CBudgetProposal* CGovernanceManager::FindProposal(const uint256& nHash)
{
    LOCK(cs);
    auto it = mapProposals.find(nHash);
    return (it != mapProposals.end() ? &it->second : nullptr);
}

std::vector<CBudgetProposal*> CGovernanceManager::GetAllProposals()
{
    LOCK(cs);

    std::vector<CBudgetProposal*> vBudgetProposalRet;

    auto it = mapProposals.begin();
    while (it != mapProposals.end()) {
        (*it).second.CleanAndRemove();

        CBudgetProposal* pbudgetProposal = &((*it).second);
        vBudgetProposalRet.push_back(pbudgetProposal);

        ++it;
    }

    return vBudgetProposalRet;
}

bool CGovernanceManager::HaveSeenProposal(const uint256& hash) const
{
    LOCK(cs);
    auto it = mapSeenMasternodeBudgetProposals.find(hash);
    return it != mapSeenMasternodeBudgetProposals.end();
}

bool CGovernanceManager::HaveSeenVote(const uint256& hash) const
{
    LOCK(cs);
    auto it = mapSeenMasternodeBudgetVotes.find(hash);
    return it != mapSeenMasternodeBudgetVotes.end();
}


void CGovernanceManager::AddSeenProposal(CBudgetProposal& budgetProposal)
{
    LOCK(cs);
    mapSeenMasternodeBudgetProposals.insert(std::make_pair(budgetProposal.GetHash(), budgetProposal));
}

void CGovernanceManager::AddSeenVote(CBudgetVote& vote)
{
    LOCK(cs);
    mapSeenMasternodeBudgetVotes.insert(std::make_pair(vote.GetHash(), vote));
}

CDataStream CGovernanceManager::GetProposalSerialized(const uint256& hash) const
{
    LOCK(cs);
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss.reserve(1000);
    ss << mapSeenMasternodeBudgetProposals.at(hash);
    return ss;
}

CDataStream CGovernanceManager::GetVoteSerialized(const uint256& hash) const
{
    LOCK(cs);
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss.reserve(1000);
    ss << mapSeenMasternodeBudgetVotes.at(hash);
    return ss;
}

//
// Sort by votes, if there's a tie sort by their feeHash TX
//
struct sortProposalsByVotes {
    bool operator()(const std::pair<CBudgetProposal*, int>& left, const std::pair<CBudgetProposal*, int>& right)
    {
        if (left.second != right.second)
            return (left.second > right.second);
        return (left.first->nFeeTXHash > right.first->nFeeTXHash);
    }
};

//Need to review this function
std::vector<CBudgetProposal*> CGovernanceManager::GetBudget()
{
    LOCK(cs);

    // ------- Sort budgets by Yes Count

    std::vector<std::pair<CBudgetProposal*, int> > vBudgetPorposalsSort;

    auto it = mapProposals.begin();
    while (it != mapProposals.end()) {
        (*it).second.CleanAndRemove();
        vBudgetPorposalsSort.push_back(std::make_pair(&((*it).second), (*it).second.GetYeas() - (*it).second.GetNays()));
        ++it;
    }

    std::sort(vBudgetPorposalsSort.begin(), vBudgetPorposalsSort.end(), sortProposalsByVotes());

    // ------- Grab The Budgets In Order

    std::vector<CBudgetProposal*> vBudgetProposalsRet;

    CAmount nBudgetAllocated = 0;

    CBlockIndex* pindexPrev;
    {
        LOCK(cs_main);
        pindexPrev = chainActive.Tip();
    }
    if (pindexPrev == NULL) return vBudgetProposalsRet;

    const int nBlocksPerCycle = Params().GetConsensus().nBudgetCycleBlocks;
    int nBlockStart = pindexPrev->nHeight - pindexPrev->nHeight % nBlocksPerCycle + nBlocksPerCycle;
    int nBlockEnd = nBlockStart + nBlocksPerCycle - 1;
    int mnCount = mnodeman.CountEnabled(ActiveProtocol());
    CAmount nTotalBudget = GetTotalBudget(nBlockStart);

    std::vector<std::pair<CBudgetProposal*, int> >::iterator it2 = vBudgetPorposalsSort.begin();
    while (it2 != vBudgetPorposalsSort.end()) {
        CBudgetProposal* pbudgetProposal = (*it2).first;

        LogPrint(BCLog::MNBUDGET,"%s: Processing Budget %s\n", __func__, pbudgetProposal->strProposalName.c_str());
        //prop start/end should be inside this period
        if (pbudgetProposal->IsPassing(pindexPrev, nBlockStart, nBlockEnd, mnCount)) {
            LogPrint(BCLog::MNBUDGET,"%s: ---- Check 1 passed: valid=%d | %ld <= %ld | %ld >= %ld | Yeas=%d Nays=%d Count=%d | established=%d\n",
                      __func__, pbudgetProposal->fValid, pbudgetProposal->nBlockStart, nBlockStart, pbudgetProposal->nBlockEnd,
                      nBlockEnd, pbudgetProposal->GetYeas(), pbudgetProposal->GetNays(), mnCount / 10,
                      pbudgetProposal->IsEstablished());

            if (pbudgetProposal->GetAmount() + nBudgetAllocated <= nTotalBudget) {
                pbudgetProposal->SetAllotted(pbudgetProposal->GetAmount());
                nBudgetAllocated += pbudgetProposal->GetAmount();
                vBudgetProposalsRet.push_back(pbudgetProposal);
                LogPrint(BCLog::MNBUDGET,"%s: ---- Check 2 passed: Budget added\n", __func__);
            } else {
                pbudgetProposal->SetAllotted(0);
                LogPrint(BCLog::MNBUDGET,"%s: ---- Check 2 failed: no amount allotted\n", __func__);
            }
        }
        else {
            LogPrint(BCLog::MNBUDGET,"%s: ---- Check 1 failed: valid=%d | %ld <= %ld | %ld >= %ld | Yeas=%d Nays=%d Count=%d | established=%d\n",
                      __func__, pbudgetProposal->fValid, pbudgetProposal->nBlockStart, nBlockStart, pbudgetProposal->nBlockEnd,
                      nBlockEnd, pbudgetProposal->GetYeas(), pbudgetProposal->GetNays(), mnodeman.CountEnabled(ActiveProtocol()) / 10,
                      pbudgetProposal->IsEstablished());
        }

        ++it2;
    }

    return vBudgetProposalsRet;
}

void CGovernanceManager::NewBlock()
{
    TRY_LOCK(cs, fBudgetNewBlock);
    if (!fBudgetNewBlock) return;

    // !TODO: Add govobj sync
    if (masternodeSync.RequestedMasternodeAssets <= MASTERNODE_SYNC_BUDGET) return;

    //this function should be called 1/14 blocks, allowing up to 100 votes per day on all proposals
    if (chainActive.Height() % 14 != 0) return;

    CheckAndRemove();

    //remove invalid votes once in a while (we have to check the signatures and validity of every vote, somewhat CPU intensive)

    LogPrint(BCLog::MNBUDGET,"%s: askedForSourceProposalOrBudget cleanup - size: %d\n", __func__, askedForSourceProposalOrBudget.size());
    std::map<uint256, int64_t>::iterator it = askedForSourceProposalOrBudget.begin();
    while (it != askedForSourceProposalOrBudget.end()) {
        if ((*it).second > GetTime() - (60 * 60 * 24)) {
            ++it;
        } else {
            askedForSourceProposalOrBudget.erase(it++);
        }
    }

    LogPrint(BCLog::MNBUDGET,"%s: mapProposals cleanup - size: %d\n", __func__, mapProposals.size());
    std::map<uint256, CBudgetProposal>::iterator it2 = mapProposals.begin();
    while (it2 != mapProposals.end()) {
        (*it2).second.CleanAndRemove();
        ++it2;
    }

    LogPrint(BCLog::MNBUDGET,"%s: vecImmatureBudgetProposals cleanup - size: %d\n", __func__, vecImmatureBudgetProposals.size());
    std::vector<CBudgetProposalBroadcast>::iterator it3 = vecImmatureBudgetProposals.begin();
    while (it3 != vecImmatureBudgetProposals.end()) {
        int nConf = 0;
        if (!(*it3).UpdateValid(nConf)) {
            LogPrint(BCLog::MNBUDGET,"%s: mprop (immature) - invalid budget proposal - %s\n", __func__, (*it3).IsInvalidReason());
            it3 = vecImmatureBudgetProposals.erase(it3);
            continue;
        }

        CBudgetProposal budgetProposal((*it3));
        if (AddProposal(budgetProposal)) {
            CBudgetProposalBroadcast(*it3).Relay();
        }

        LogPrint(BCLog::MNBUDGET,"%s: mprop (immature) - new budget - %s\n", __func__, (*it3).GetHash().ToString());
        it3 = vecImmatureBudgetProposals.erase(it3);
    }
}

void CGovernanceManager::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    // lite mode is not supported
    if (fLiteMode) return;
    if (!masternodeSync.IsBlockchainSynced()) return;

    LOCK(cs_governance);

    if (strCommand == NetMsgType::BUDGETVOTESYNC) { //Masternode vote sync
        uint256 nProp;
        vRecv >> nProp;

        if (Params().NetworkID() == CBaseChainParams::MAIN) {
            if (nProp.IsNull()) {
                if (pfrom->HasFulfilledRequest(NetMsgType::BUDGETVOTESYNC)) {
                    LogPrint(BCLog::MNBUDGET,"mnvs - peer already asked me for the list\n");
                    LOCK(cs_main);
                    Misbehaving(pfrom->GetId(), 20);
                    return;
                }
                pfrom->FulfilledRequest(NetMsgType::BUDGETVOTESYNC);
            }
        }

        Sync(pfrom, nProp);
        LogPrint(BCLog::MNBUDGET, "mnvs - Sent Masternode votes to peer %i\n", pfrom->GetId());
    }

    if (strCommand == NetMsgType::BUDGETPROPOSAL) { //Masternode Proposal
        CBudgetProposalBroadcast budgetProposalBroadcast;
        vRecv >> budgetProposalBroadcast;

        if (mapSeenMasternodeBudgetProposals.count(budgetProposalBroadcast.GetHash())) {
            masternodeSync.AddedBudgetItem(budgetProposalBroadcast.GetHash());
            return;
        }


        std::string strError = "";
        int nConf = 0;
        if (!budgetProposalBroadcast.UpdateValid(nConf)) {
            LogPrint(BCLog::MNBUDGET,"Proposal FeeTX is not valid - %s - %s\n",
                    budgetProposalBroadcast.nFeeTXHash.ToString(), budgetProposalBroadcast.IsInvalidReason());
            if (nConf >= 1) vecImmatureBudgetProposals.push_back(budgetProposalBroadcast);
            return;
        }

        mapSeenMasternodeBudgetProposals.insert(std::make_pair(budgetProposalBroadcast.GetHash(), budgetProposalBroadcast));

        CBudgetProposal budgetProposal(budgetProposalBroadcast);
        if (AddProposal(budgetProposal)) {
            budgetProposalBroadcast.Relay();
        }
        masternodeSync.AddedBudgetItem(budgetProposalBroadcast.GetHash());

        LogPrint(BCLog::MNBUDGET,"mprop - new budget - %s\n", budgetProposalBroadcast.GetHash().ToString());

        //We might have active votes for this proposal that are valid now
        CheckOrphanVotes();
    }

    if (strCommand == NetMsgType::BUDGETVOTE) { // Budget Vote
        CBudgetVote vote;
        vRecv >> vote;
        vote.fValid = true;

        if (mapSeenMasternodeBudgetVotes.count(vote.GetHash())) {
            masternodeSync.AddedBudgetItem(vote.GetHash());
            return;
        }

        CMasternode* pmn = mnodeman.Find(vote.vin);
        if (pmn == NULL) {
            LogPrint(BCLog::MNBUDGET,"mvote - unknown masternode - vin: %s\n", vote.vin.prevout.hash.ToString());
            mnodeman.AskForMN(pfrom, vote.vin);
            return;
        }


        mapSeenMasternodeBudgetVotes.insert(std::make_pair(vote.GetHash(), vote));
        if (!vote.CheckSignature()) {
            if (masternodeSync.IsSynced()) {
                LogPrintf("mvote - signature invalid\n");
                LOCK(cs_main);
                Misbehaving(pfrom->GetId(), 20);
            }
            // it could just be a non-synced masternode
            mnodeman.AskForMN(pfrom, vote.vin);
            return;
        }

        std::string strError = "";
        if (UpdateProposal(vote, pfrom, strError)) {
            vote.Relay();
            masternodeSync.AddedBudgetItem(vote.GetHash());
        }

        LogPrint(BCLog::MNBUDGET,"mvote - new budget vote for budget %s - %s\n", vote.nProposalHash.ToString(),  vote.GetHash().ToString());
    }
}

//mark that a full sync is needed
void CGovernanceManager::ResetSync()
{
    LOCK(cs);

    auto it_proposals = mapSeenMasternodeBudgetProposals.begin();
    while (it_proposals != mapSeenMasternodeBudgetProposals.end()) {
        CBudgetProposal* pbudgetProposal = FindProposal((*it_proposals).first);
        if (pbudgetProposal && pbudgetProposal->fValid) {
            //mark votes
            auto it_votes = pbudgetProposal->mapVotes.begin();
            while (it_votes != pbudgetProposal->mapVotes.end()) {
                (*it_votes).second.fSynced = false;
                ++it_votes;
            }
        }
        ++it_proposals;
    }
}

void CGovernanceManager::MarkSynced()
{
    LOCK(cs);
    /*
        Mark that we've sent all valid items
    */

    auto it_proposals = mapSeenMasternodeBudgetProposals.begin();
    while (it_proposals != mapSeenMasternodeBudgetProposals.end()) {
        CBudgetProposal* pbudgetProposal = FindProposal((*it_proposals).first);
        if (pbudgetProposal && pbudgetProposal->fValid) {
            //mark votes
            auto it_votes = pbudgetProposal->mapVotes.begin();
            while (it_votes != pbudgetProposal->mapVotes.end()) {
                if ((*it_votes).second.fValid)
                    (*it_votes).second.fSynced = true;
                ++it_votes;
            }
        }
        ++it_proposals;
    }
}


void CGovernanceManager::Sync(CNode* pfrom, uint256 nProp, bool fPartial)
{
    LOCK(cs);

    /*
        Sync with a client on the network

        --

        This code checks each of the hash maps for all known budget proposals and finalized budget proposals, then checks them against the
        budget object to see if they're OK. If all checks pass, we'll send it to the peer.

    */

    int nInvCount = 0;

    auto it1 = mapSeenMasternodeBudgetProposals.begin();
    while (it1 != mapSeenMasternodeBudgetProposals.end()) {
        CBudgetProposal* pbudgetProposal = FindProposal((*it1).first);
        if (pbudgetProposal && pbudgetProposal->fValid && (nProp.IsNull() || (*it1).first == nProp)) {
            pfrom->PushInventory(CInv(MSG_BUDGET_PROPOSAL, (*it1).second.GetHash()));
            nInvCount++;

            //send votes
            std::map<uint256, CBudgetVote>::iterator it2 = pbudgetProposal->mapVotes.begin();
            while (it2 != pbudgetProposal->mapVotes.end()) {
                if ((*it2).second.fValid) {
                    if ((fPartial && !(*it2).second.fSynced) || !fPartial) {
                        pfrom->PushInventory(CInv(MSG_BUDGET_VOTE, (*it2).second.GetHash()));
                        nInvCount++;
                    }
                }
                ++it2;
            }
        }
        ++it1;
    }

    pfrom->PushMessage(NetMsgType::SYNCSTATUSCOUNT, MASTERNODE_SYNC_BUDGET_PROP, nInvCount);
    LogPrint(BCLog::MNBUDGET, "%s: sent %d items\n", __func__, nInvCount);
}

bool CGovernanceManager::UpdateProposal(CBudgetVote& vote, CNode* pfrom, std::string& strError)
{
    LOCK(cs);

    if (!mapProposals.count(vote.nProposalHash)) {
        if (pfrom) {
            // only ask for missing items after our syncing process is complete --
            //   otherwise we'll think a full sync succeeded when they return a result
            if (!masternodeSync.IsSynced()) return false;

            LogPrint(BCLog::MNBUDGET,"%s: Unknown proposal %d, asking for source proposal\n", __func__, vote.nProposalHash.ToString());
            mapOrphanMasternodeBudgetVotes[vote.nProposalHash] = vote;

            if (!askedForSourceProposalOrBudget.count(vote.nProposalHash)) {
                pfrom->PushMessage(NetMsgType::BUDGETVOTESYNC, vote.nProposalHash);
                askedForSourceProposalOrBudget[vote.nProposalHash] = GetTime();
            }
        }

        strError = "Proposal not found!";
        return false;
    }


    return mapProposals[vote.nProposalHash].AddOrUpdateVote(vote, strError);
}

CAmount CGovernanceManager::GetTotalBudget(int nHeight)
{
    if (chainActive.Tip() == NULL) return 0;

    if (Params().NetworkID() == CBaseChainParams::TESTNET) {
        CAmount nSubsidy = 500 * COIN;
        return ((nSubsidy / 100) * 10) * 146;
    }

    //get block value and calculate from that
    CAmount nSubsidy = 0;
    const Consensus::Params& consensus = Params().GetConsensus();
    const bool isPoSActive = consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_POS);
    if (nHeight >= 151200 && !isPoSActive) {
        nSubsidy = 50 * COIN;
    } else if (isPoSActive && nHeight <= 302399) {
        nSubsidy = 50 * COIN;
    } else if (nHeight <= 345599 && nHeight >= 302400) {
        nSubsidy = 45 * COIN;
    } else if (nHeight <= 388799 && nHeight >= 345600) {
        nSubsidy = 40 * COIN;
    } else if (nHeight <= 431999 && nHeight >= 388800) {
        nSubsidy = 35 * COIN;
    } else if (nHeight <= 475199 && nHeight >= 432000) {
        nSubsidy = 30 * COIN;
    } else if (nHeight <= 518399 && nHeight >= 475200) {
        nSubsidy = 25 * COIN;
    } else if (nHeight <= 561599 && nHeight >= 518400) {
        nSubsidy = 20 * COIN;
    } else if (nHeight <= 604799 && nHeight >= 561600) {
        nSubsidy = 15 * COIN;
    } else if (nHeight <= 647999 && nHeight >= 604800) {
        nSubsidy = 10 * COIN;
    } else if (consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_ZC_V2)) {
        nSubsidy = 10 * COIN;
    } else {
        nSubsidy = 5 * COIN;
    }

    // Amount of blocks in a months period of time (using 1 minutes per) = (60*24*30)
    if (nHeight <= 172800) {
        return 648000 * COIN;
    } else {
        return ((nSubsidy / 100) * 10) * 1440 * 30;
    }
}

std::string CGovernanceManager::ToString() const
{
    return strprintf("Proposals: %d, Seen Budget Proposals: %d, Seen Budget Votes: %d",
            (int)mapProposals.size(), (int)mapSeenMasternodeBudgetProposals.size(), (int)mapSeenMasternodeBudgetVotes.size());
}

CBudgetProposal::CBudgetProposal()
{
    strProposalName = "unknown";
    nBlockStart = 0;
    nBlockEnd = 0;
    nAmount = 0;
    nTime = 0;
    fValid = true;
    strInvalid = "";
}

CBudgetProposal::CBudgetProposal(std::string strProposalNameIn, std::string strURLIn, int nBlockStartIn, int nBlockEndIn, CScript addressIn, CAmount nAmountIn, uint256 nFeeTXHashIn)
{
    strProposalName = strProposalNameIn;
    strURL = strURLIn;
    nBlockStart = nBlockStartIn;
    nBlockEnd = nBlockEndIn;
    address = addressIn;
    nAmount = nAmountIn;
    nFeeTXHash = nFeeTXHashIn;
    fValid = true;
    strInvalid = "";
}

CBudgetProposal::CBudgetProposal(const CBudgetProposal& other)
{
    strProposalName = other.strProposalName;
    strURL = other.strURL;
    nBlockStart = other.nBlockStart;
    nBlockEnd = other.nBlockEnd;
    address = other.address;
    nAmount = other.nAmount;
    nTime = other.nTime;
    nFeeTXHash = other.nFeeTXHash;
    mapVotes = other.mapVotes;
    fValid = true;
    strInvalid = "";
}

bool CBudgetProposal::UpdateValid(int& nConf, bool fSkipCollateral)
{
    fValid = false;
    if (GetNays() - GetYeas() > mnodeman.CountEnabled(ActiveProtocol()) / 10) {
        strInvalid = "Proposal " + strProposalName + ": Active removal";
        return false;
    }

    if (nBlockStart < 0) {
        strInvalid = "Invalid Proposal";
        return false;
    }

    if (nBlockEnd < nBlockStart) {
        strInvalid = "Proposal " + strProposalName + ": Invalid nBlockEnd (end before start)";
        return false;
    }

    if (nAmount < 10 * COIN) {
        strInvalid = "Proposal " + strProposalName + ": Invalid nAmount";
        return false;
    }

    if (address == CScript()) {
        strInvalid = "Proposal " + strProposalName + ": Invalid Payment Address";
        return false;
    }

    std::string strError = "";
    if (!fSkipCollateral && !IsBudgetCollateralValid(nFeeTXHash, GetHash(), strError, nTime, nConf)) {
        strInvalid = "Proposal " + strProposalName + ": Invalid collateral - " + strError;
        return false;
    }

    /*
        TODO: There might be an issue with multisig in the coinbase on mainnet, we will add support for it in a future release.
    */
    if (address.IsPayToScriptHash()) {
        strInvalid = "Proposal " + strProposalName + ": Multisig is not currently supported.";
        return false;
    }

    //if proposal doesn't gain traction within 2 weeks, remove it
    // nTime not being saved correctly
    // -- TODO: We should keep track of the last time the proposal was valid, if it's invalid for 2 weeks, erase it
    // if(nTime + (60*60*24*2) < GetAdjustedTime()) {
    //     if(GetYeas()-GetNays() < (mnodeman.CountEnabled(ActiveProtocol())/10)) {
    //         strError = "Not enough support";
    //         return false;
    //     }
    // }

    //can only pay out 10% of the possible coins (min value of coins)
    if (nAmount > governanceManager.GetTotalBudget(nBlockStart)) {
        strInvalid = "Proposal " + strProposalName + ": Payment more than max";
        return false;
    }

    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev == NULL) {
        strInvalid = "Proposal " + strProposalName + ": Tip is NULL";
        return false;
    }

    // Calculate maximum block this proposal will be valid, which is start of proposal + (number of payments * cycle)
    int nProposalEnd = GetBlockStart() + (Params().GetConsensus().nBudgetCycleBlocks * GetTotalPaymentCount());

    // if (GetBlockEnd() < pindexPrev->nHeight - GetBudgetPaymentCycleBlocks() / 2) {
    if(nProposalEnd < pindexPrev->nHeight){
        strInvalid = "Proposal " + strProposalName + ": Invalid nBlockEnd (" + std::to_string(nProposalEnd) + ") < current height (" + std::to_string(pindexPrev->nHeight) + ")";
        return false;
    }

    fValid = true;
    return fValid;
}

bool CBudgetProposal::IsEstablished() const
{
    return nTime < GetAdjustedTime() - Params().GetConsensus().nProposalEstablishmentTime;
}

bool CBudgetProposal::IsPassing(const CBlockIndex* pindexPrev, int nBlockStartBudget, int nBlockEndBudget, int mnCount)
{
    if (!fValid)
        return false;

    if (!pindexPrev)
        return false;

    if (this->nBlockStart > nBlockStartBudget)
        return false;

    if (this->nBlockEnd < nBlockEndBudget)
        return false;

    if (GetYeas() - GetNays() <= mnCount / 10)
        return false;

    if (!IsEstablished())
        return false;

    return true;
}

bool CBudgetProposal::AddOrUpdateVote(CBudgetVote& vote, std::string& strError)
{
    std::string strAction = "New vote inserted:";
    LOCK(cs);

    uint256 hash = vote.vin.prevout.GetHash();

    if (mapVotes.count(hash)) {
        if (mapVotes[hash].nTime > vote.nTime) {
            strError = strprintf("new vote older than existing vote - %s\n", vote.GetHash().ToString());
            LogPrint(BCLog::MNBUDGET, "CBudgetProposal::AddOrUpdateVote - %s\n", strError);
            return false;
        }
        if (vote.nTime - mapVotes[hash].nTime < BUDGET_VOTE_UPDATE_MIN) {
            strError = strprintf("time between votes is too soon - %s - %lli sec < %lli sec\n", vote.GetHash().ToString(), vote.nTime - mapVotes[hash].nTime,BUDGET_VOTE_UPDATE_MIN);
            LogPrint(BCLog::MNBUDGET, "CBudgetProposal::AddOrUpdateVote - %s\n", strError);
            return false;
        }
        strAction = "Existing vote updated:";
    }

    if (vote.nTime > GetTime() + (60 * 60)) {
        strError = strprintf("new vote is too far ahead of current time - %s - nTime %lli - Max Time %lli\n", vote.GetHash().ToString(), vote.nTime, GetTime() + (60 * 60));
        LogPrint(BCLog::MNBUDGET, "CBudgetProposal::AddOrUpdateVote - %s\n", strError);
        return false;
    }

    mapVotes[hash] = vote;
    LogPrint(BCLog::MNBUDGET, "CBudgetProposal::AddOrUpdateVote - %s %s\n", strAction.c_str(), vote.GetHash().ToString().c_str());

    return true;
}

// If masternode voted for a proposal, but is now invalid -- remove the vote
void CBudgetProposal::CleanAndRemove()
{
    std::map<uint256, CBudgetVote>::iterator it = mapVotes.begin();

    while (it != mapVotes.end()) {
        CMasternode* pmn = mnodeman.Find((*it).second.GetVin());
        (*it).second.fValid = (pmn != nullptr);
        ++it;
    }
}

double CBudgetProposal::GetRatio() const
{
    int yeas = 0;
    int nays = 0;

    auto it = mapVotes.begin();

    while (it != mapVotes.end()) {
        if ((*it).second.nVote == VOTE_YES) yeas++;
        if ((*it).second.nVote == VOTE_NO) nays++;
        ++it;
    }

    if (yeas + nays == 0) return 0.0f;

    return ((double)(yeas) / (double)(yeas + nays));
}

UniValue CBudgetProposal::GetVotesArray() const
{
    UniValue ret(UniValue::VARR);
    auto it = mapVotes.begin();
    while (it != mapVotes.end()) {
        UniValue bObj(UniValue::VOBJ);
        bObj.push_back(Pair("mnId", (*it).second.vin.prevout.hash.ToString()));
        bObj.push_back(Pair("nHash", (*it).first.ToString().c_str()));
        bObj.push_back(Pair("Vote", (*it).second.GetVoteString()));
        bObj.push_back(Pair("nTime", (int64_t)(*it).second.nTime));
        bObj.push_back(Pair("fValid", (*it).second.fValid));

        ret.push_back(bObj);

        it++;
    }

    return ret;
}

int CBudgetProposal::GetYeas() const
{
    int ret = 0;

    std::map<uint256, CBudgetVote>::const_iterator it = mapVotes.begin();
    while (it != mapVotes.end()) {
        if ((*it).second.nVote == VOTE_YES && (*it).second.fValid) ret++;
        ++it;
    }

    return ret;
}

int CBudgetProposal::GetNays() const
{
    int ret = 0;

    std::map<uint256, CBudgetVote>::const_iterator it = mapVotes.begin();
    while (it != mapVotes.end()) {
        if ((*it).second.nVote == VOTE_NO && (*it).second.fValid) ret++;
        ++it;
    }

    return ret;
}

int CBudgetProposal::GetAbstains() const
{
    int ret = 0;

    std::map<uint256, CBudgetVote>::const_iterator it = mapVotes.begin();
    while (it != mapVotes.end()) {
        if ((*it).second.nVote == VOTE_ABSTAIN && (*it).second.fValid) ret++;
        ++it;
    }

    return ret;
}

int CBudgetProposal::GetBlockStartCycle() const
{
    //end block is half way through the next cycle (so the proposal will be removed much after the payment is sent)

    return nBlockStart - nBlockStart % Params().GetConsensus().nBudgetCycleBlocks;
}

int CBudgetProposal::GetBlockCurrentCycle() const
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev == NULL) return -1;

    if (pindexPrev->nHeight >= GetBlockEndCycle()) return -1;

    return pindexPrev->nHeight - pindexPrev->nHeight % Params().GetConsensus().nBudgetCycleBlocks;
}

int CBudgetProposal::GetBlockEndCycle() const
{
    // Right now single payment proposals have nBlockEnd have a cycle too early!
    // switch back if it break something else
    // end block is half way through the next cycle (so the proposal will be removed much after the payment is sent)
    // return nBlockEnd - GetBudgetPaymentCycleBlocks() / 2;

    // End block is half way through the next cycle (so the proposal will be removed much after the payment is sent)
    return nBlockEnd;

}

int CBudgetProposal::GetTotalPaymentCount() const
{
    return (GetBlockEndCycle() - GetBlockStartCycle()) / Params().GetConsensus().nBudgetCycleBlocks;
}

int CBudgetProposal::GetRemainingPaymentCount() const
{
    // If this budget starts in the future, this value will be wrong
    int nPayments = (GetBlockEndCycle() - GetBlockCurrentCycle()) / Params().GetConsensus().nBudgetCycleBlocks - 1;
    // Take the lowest value
    return std::min(nPayments, GetTotalPaymentCount());
}

CBudgetProposalBroadcast::CBudgetProposalBroadcast(std::string strProposalNameIn, std::string strURLIn, int nPaymentCount, CScript addressIn, CAmount nAmountIn, int nBlockStartIn, uint256 nFeeTXHashIn)
{
    strProposalName = strProposalNameIn;
    strURL = strURLIn;

    nBlockStart = nBlockStartIn;

    const int nBlocksPerCycle = Params().GetConsensus().nBudgetCycleBlocks;
    int nCycleStart = nBlockStart - nBlockStart % nBlocksPerCycle;

    // Right now single payment proposals have nBlockEnd have a cycle too early!
    // switch back if it break something else
    // calculate the end of the cycle for this vote, add half a cycle (vote will be deleted after that block)
    // nBlockEnd = nCycleStart + GetBudgetPaymentCycleBlocks() * nPaymentCount + GetBudgetPaymentCycleBlocks() / 2;

    // Calculate the end of the cycle for this vote, vote will be deleted after next cycle
    nBlockEnd = nCycleStart + (nBlocksPerCycle + 1)  * nPaymentCount;

    address = addressIn;
    nAmount = nAmountIn;

    nFeeTXHash = nFeeTXHashIn;
}

void CBudgetProposalBroadcast::Relay()
{
    CInv inv(MSG_BUDGET_PROPOSAL, GetHash());
    RelayInv(inv);
}

CBudgetVote::CBudgetVote() :
        CSignedMessage(),
        fValid(true),
        fSynced(false),
        vin(),
        nProposalHash(),
        nVote(VOTE_ABSTAIN),
        nTime(0)
{ }

CBudgetVote::CBudgetVote(CTxIn vinIn, uint256 nProposalHashIn, int nVoteIn) :
        CSignedMessage(),
        fValid(true),
        fSynced(false),
        vin(vinIn),
        nProposalHash(nProposalHashIn),
        nVote(nVoteIn)
{
    nTime = GetAdjustedTime();
}

void CBudgetVote::Relay()
{
    CInv inv(MSG_BUDGET_VOTE, GetHash());
    RelayInv(inv);
}

uint256 CBudgetVote::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << nProposalHash;
    ss << nVote;
    ss << nTime;
    return ss.GetHash();
}

std::string CBudgetVote::GetStrMessage() const
{
    return vin.prevout.ToStringShort() + nProposalHash.ToString() +
            std::to_string(nVote) + std::to_string(nTime);
}
