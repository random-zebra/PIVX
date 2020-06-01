// Copyright (c) 2017 The Dash Core developers
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


bool CheckSpecialTx(const CTransaction& tx, const CBlockIndex* pindexPrev, CValidationState& state)
{
    if (!Params().GetConsensus().NetworkUpgradeActive(pindexPrev->nHeight + 1, Consensus::UPGRADE_V5_DUMMY)) {
        return state.DoS(10, error("%s : special tx arrived too early at height %d",
                __func__, pindexPrev->nHeight + 1), REJECT_INVALID, "bad-tx-type");
    }

    switch (tx.nType) {
        case TRANSACTION_PROVIDER_REGISTER:
            return CheckProRegTx(tx, state);
    }

    return state.DoS(10, error("%s : special tx with invalid type %d",
            __func__, tx.nType), REJECT_INVALID, "bad-tx-type");
}

bool ProcessSpecialTxsInBlock(const CBlock& block, const CBlockIndex* pindexPrev, CValidationState& state)
{
    for (const CTransaction& tx : block.vtx) {
        if (tx.IsSpecialTx() && !CheckSpecialTx(tx, pindexPrev, state))
            return false;
    }
    /* process special txes in batches */

    return true;
}

bool UndoSpecialTxsInBlock(const CBlock& block, const CBlockIndex* pindexPrev)
{
    /* undo special txes in batches */
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
