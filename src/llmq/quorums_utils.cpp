// Copyright (c) 2018-2019 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "llmq/quorums_utils.h"

#include "bls/bls_wrapper.h"
#include "chainparams.h"
#include "random.h"
#include "spork.h"
#include "validation.h"

namespace llmq
{

namespace utils
{

std::vector<CDeterministicMNCPtr> GetAllQuorumMembers(Consensus::LLMQType llmqType, const CBlockIndex* pindexQuorum)
{
    auto& params = Params().GetConsensus().llmqs.at(llmqType);
    auto allMns = deterministicMNManager->GetListForBlock(pindexQuorum);
    auto modifier = ::SerializeHash(std::make_pair(static_cast<uint8_t>(llmqType), pindexQuorum->GetBlockHash()));
    return allMns.CalculateQuorum(params.size, modifier);
}

uint256 BuildCommitmentHash(Consensus::LLMQType llmqType, const uint256& blockHash, const std::vector<bool>& validMembers, const CBLSPublicKey& pubKey, const uint256& vvecHash)
{
    CHashWriter hw(SER_NETWORK, 0);
    hw << static_cast<uint8_t>(llmqType);
    hw << blockHash;
    hw << DYNBITSET(validMembers);
    hw << pubKey;
    hw << vvecHash;
    return hw.GetHash();
}

uint256 BuildSignHash(Consensus::LLMQType llmqType, const uint256& quorumHash, const uint256& id, const uint256& msgHash)
{
    CHashWriter h(SER_GETHASH, 0);
    h << static_cast<uint8_t>(llmqType);
    h << quorumHash;
    h << id;
    h << msgHash;
    return h.GetHash();
}

std::string ToHexStr(const std::vector<bool>& vBits)
{
    std::vector<uint8_t> vBytes((vBits.size() + 7) / 8);
    for (size_t i = 0; i < vBits.size(); i++) {
        vBytes[i / 8] |= vBits[i] << (i % 8);
    }
    return HexStr(vBytes);
}

static std::set<uint256> GetQuorumConnections(Consensus::LLMQType llmqType, const CBlockIndex* pindexQuorum, const uint256& forMember)
{
    auto& params = Params().GetConsensus().llmqs.at(llmqType);

    auto mns = GetAllQuorumMembers(llmqType, pindexQuorum);
    std::set<uint256> result;

    auto calcOutbound = [&](size_t i, const uint256 proTxHash) {
        // Relay to nodes at indexes (i+2^k)%n, where
        //   k: 0..max(1, floor(log2(n-1))-1)
        //   n: size of the quorum/ring
        std::set<uint256> r;
        int gap = 1;
        int gap_max = (int)mns.size() - 1;
        int k = 0;
        while ((gap_max >>= 1) || k <= 1) {
            size_t idx = (i + gap) % mns.size();
            auto& otherDmn = mns[idx];
            if (otherDmn->proTxHash == proTxHash) {
                continue;
            }
            r.emplace(otherDmn->proTxHash);
            gap <<= 1;
            k++;
        }
        return r;
    };

    for (size_t i = 0; i < mns.size(); i++) {
        auto& dmn = mns[i];
        auto r = calcOutbound(i, dmn->proTxHash);
        if (dmn->proTxHash == forMember) {
            result.insert(r.begin(), r.end());
        } else if (r.count(forMember)) {
            result.emplace(dmn->proTxHash);
        }
    }
    return result;
}

void EnsureQuorumConnections(Consensus::LLMQType llmqType, const CBlockIndex *pindexQuorum, const uint256& myProTxHash)
{
    auto members = GetAllQuorumMembers(llmqType, pindexQuorum);
    bool isMember = std::find_if(members.begin(), members.end(),
            [&](const CDeterministicMNCPtr& dmn) { return dmn->proTxHash == myProTxHash; }) != members.end();

    if (!isMember) {
        return;
    }

    std::set<uint256> connections = GetQuorumConnections(llmqType, pindexQuorum, myProTxHash);
    if (!connections.empty()) {
        if (!g_connman->HasMasternodeQuorumNodes(llmqType, pindexQuorum->GetBlockHash())) {
            LogPrint(BCLog::DKG, "%s: Adding %d quorum connections for %s\n", __func__, connections.size(), pindexQuorum->GetBlockHash().ToString());
        }
        g_connman->SetMasternodeQuorumNodes(llmqType, pindexQuorum->GetBlockHash(), connections);
    }
}

} // namespace llmq::utils

} // namespace llmq
