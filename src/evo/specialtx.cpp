
// Copyright (c) 2017 The Dash Core developers
// Copyright (c) 2020 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "evo/specialtx.h"

#include "chain.h"
#include "chainparams.h"
#include "clientversion.h"
#include "consensus/validation.h"
#include "evo/deterministicmns.h"
#include "evo/providertx.h"
#include "primitives/transaction.h"
#include "primitives/block.h"

bool CheckSpecialTx(const CTransaction& tx, const CBlockIndex* pindexPrev, CValidationState& state)
{
    bool hasExtraPayload = tx.hasExtraPayload();
    bool isNormalType = tx.IsNormalType();

    // v1/v2 can only be Type=0
    if (!tx.isSaplingVersion() && !isNormalType) {
        return state.DoS(100, error("%s: Type %d not supported with version %d", __func__, tx.nType, tx.nVersion),
                         REJECT_INVALID, "bad-txns-type-version");
    }
    if (isNormalType) {
        // Type-0 txes don't have extra payload
        if (hasExtraPayload) {
            return state.DoS(100, error("%s: Type 0 doesn't support extra payload", __func__),
                             REJECT_INVALID, "bad-txns-type-payload");
        }
        // Normal transaction. Nothing to check
        return true;
    }

    // --- From here on, tx has nVersion>=2 and nType!=0

    if (pindexPrev && !Params().GetConsensus().NetworkUpgradeActive(pindexPrev->nHeight + 1, Consensus::UPGRADE_V6_0)) {
        return state.DoS(100, error("%s: Special tx when EVO upgrade not enforced yet", __func__),
                         REJECT_INVALID, "bad-txns-evo-not-active");
    }

    // Cannot be coinbase/coinstake tx
    if (tx.IsCoinBase() || tx.IsCoinStake()) {
        return state.DoS(10, error("%s: Special tx is coinbase or coinstake", __func__),
                         REJECT_INVALID, "bad-txns-special-coinbase");
    }

    // Special txes must have a non-empty payload
    if (!hasExtraPayload) {
        return state.DoS(100, error("%s: Special tx (type=%d) without extra payload", __func__, tx.nType),
                         REJECT_INVALID, "bad-txns-payload-empty");
    }

    // Size limits
    if (tx.extraPayload->size() > MAX_SPECIALTX_EXTRAPAYLOAD) {
        return state.DoS(100, error("%s: Special tx payload oversize (%d)", __func__, tx.extraPayload->size()),
                         REJECT_INVALID, "bad-txns-payload-oversize");
    }

    switch (tx.nType) {
        case CTransaction::TxType::PROREG:
            return CheckProRegTx(tx, pindexPrev, state);
    }

    return state.DoS(10, error("%s : special tx %s with invalid type %d",
            __func__, tx.GetHash().ToString(), tx.nType), REJECT_INVALID, "bad-tx-type");
}

bool ProcessSpecialTxsInBlock(const CBlock& block, const CBlockIndex* pindex, CValidationState& state, bool fJustCheck)
{
    for (const CTransactionRef& tx: block.vtx) {
        if (!CheckSpecialTx(*tx, pindex->pprev, state)) {
            // pass the state returned by the function above
            return false;
        }
    }
    // !TODO: ProcessBlock llmq quorum block processor
    if (!deterministicMNManager->ProcessBlock(block, pindex, state, fJustCheck)) {
        // pass the state returned by the function above
        return false;
    }
    return true;
}

bool UndoSpecialTxsInBlock(const CBlock& block, const CBlockIndex* pindex)
{
    if (!deterministicMNManager->UndoBlock(block, pindex)) {
        return false;
    }
    // !TODO: UndoBlock llmq quorum block processor
    return true;
}

uint256 CalcTxInputsHash(const CTransaction& tx)
{
    CHashWriter hw(CLIENT_VERSION, SER_GETHASH);
    for (const auto& in : tx.vin) {
        hw << in.prevout;
    }
    return hw.GetHash();
}