// Copyright (c) 2017-2019 The Dash Core developers
// Copyright (c) 2020 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "evo/specialtx.h"

#include "chain.h"
#include "chainparams.h"
#include "clientversion.h"
#include "consensus/validation.h"
#include "evo/providertx.h"
#include "primitives/block.h"


bool CheckSpecialTx(const CTransaction& tx, CValidationState& state)
{
    if (!tx.isSpecialTx()) {
        // Safe-check: Only special txes can have extra payload (wouldn't be serialized otherwise)
        if (tx.hasExtraPayload()) {
            return state.DoS(100, error("%s : non-special tx %s with extra payload",
                    __func__, tx.GetHash().ToString()), REJECT_INVALID, "bad-tx-type");
        }
        // Nothing to do
        return true;
    }

    switch (tx.nType) {
    case TRANSACTION_PROVIDER_REGISTER:
        return CheckProRegPL(tx, state);
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


