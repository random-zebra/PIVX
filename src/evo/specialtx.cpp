// Copyright (c) 2017 The Dash Core developers
// Copyright (c) 2020 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "evo/specialtx.h"

#include "chain.h"
#include "chainparams.h"
#include "clientversion.h"
#include "consensus/validation.h"
#include "primitives/block.h"


bool CheckSpecialTx(const CTransaction& tx, const CBlockIndex* pindexPrev, CValidationState& state)
{
    if (!tx.isSpecialTx() || !Params().GetConsensus().NetworkUpgradeActive(
            pindexPrev->nHeight + 1, Consensus::UPGRADE_V5_DUMMY)) {
        // Nothing to check
        return true;
    }

    switch (tx.nType) {
    /* per-tx-type checking */
    }

    return state.DoS(10, error("%s : special tx %s with invalid type %d",
            __func__, tx.GetHash().ToString(), tx.nType), REJECT_INVALID, "bad-tx-type");
}

bool ProcessSpecialTxsInBlock(const CBlock& block, const CBlockIndex* pindexPrev, CValidationState& state)
{
    if (!Params().GetConsensus().NetworkUpgradeActive(pindexPrev->nHeight + 1, Consensus::UPGRADE_V5_DUMMY)) {
        // Nothing to process
        return true;
    }
    /* process special txes in batches */
    return true;
}

bool UndoSpecialTxsInBlock(const CBlock& block, const CBlockIndex* pindexPrev)
{
    if (!Params().GetConsensus().NetworkUpgradeActive(pindexPrev->nHeight + 1, Consensus::UPGRADE_V5_DUMMY)) {
        // Nothing to undo
        return true;
    }
    /* undo special txes in batches */
    return true;
}


/*
 * Payload Get/Set
 */
template <typename T>
static bool GetTxPayload(const std::vector<unsigned char>& payload, T& obj)
{
    CDataStream ds(payload, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ds >> obj;
    } catch (std::exception& e) {
        return error("%s: %s", __func__, e.what());
    }
    return ds.empty();
}

template <typename T>
bool GetTxPayload(const CMutableTransaction& tx, T& obj) { return tx.vExtraPayload && GetTxPayload(*(tx.vExtraPayload), obj); }

template <typename T>
bool GetTxPayload(const CTransaction& tx, T& obj) { return tx.vExtraPayload && GetTxPayload(*(tx.vExtraPayload), obj); }

template <typename T>
void SetTxPayload(CMutableTransaction& tx, const T& payload)
{
    CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
    ds << payload;
    tx.vExtraPayload = std::vector<unsigned char>(ds.begin(), ds.end());
}

