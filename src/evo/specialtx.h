// Copyright (c) 2017 The Dash Core developers
// Copyright (c) 2020 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_SPECIALTX_H
#define PIVX_SPECIALTX_H

#include "streams.h"
#include "version.h"
#include "primitives/transaction.h"

class CBlock;
class CBlockIndex;
class CValidationState;
class uint256;

// CheckSpecialTx returns true for non-special txes, otherwise the result of per-tx-type consistency checks
bool CheckSpecialTx(const CTransaction& tx, const CBlockIndex* pindexPrev, CValidationState& state);
// Update internal data when blocks containing special txes get connected/disconnected
bool ProcessSpecialTxsInBlock(const CBlock& block, const CBlockIndex* pindexPrev, CValidationState& state);
bool UndoSpecialTxsInBlock(const CBlock& block, const CBlockIndex* pindexPrev);

// Payload Getters/Setter
template <typename T> bool GetTxPayload(const CMutableTransaction& tx, T& obj);
template <typename T> bool GetTxPayload(const CTransaction& tx, T& obj);
template <typename T> void SetTxPayload(CMutableTransaction& tx, const T& payload);

#endif //PIVX_SPECIALTX_H
