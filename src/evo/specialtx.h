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

// returns true for TRANSACTION_NORMAL
bool CheckSpecialTx(const CTransaction& tx, const CBlockIndex* pindexPrev, CValidationState& state);
bool ProcessSpecialTxsInBlock(const CBlock& block, const CBlockIndex* pindexPrev, CValidationState& state);
bool UndoSpecialTxsInBlock(const CBlock& block, const CBlockIndex* pindexPrev);

uint256 CalcTxInputsHash(const CTransaction& tx);

template <typename T>
inline bool GetTxPayload(const std::vector<unsigned char>& payload, T& obj)
{
    CDataStream ds(payload, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ds >> obj;
    } catch (std::exception& e) {
        return false;
    }
    return ds.empty();
}

template <typename T>
inline bool GetTxPayload(const CMutableTransaction& tx, T& obj)
{
    return GetTxPayload(tx.vExtraPayload, obj);
}

template <typename T>
inline bool GetTxPayload(const CTransaction& tx, T& obj)
{
    return GetTxPayload(tx.vExtraPayload, obj);
}

template <typename T>
void SetTxPayload(CMutableTransaction& tx, const T& payload)
{
    CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
    ds << payload;
    tx.vExtraPayload.assign(ds.begin(), ds.end());
}

#endif //PIVX_SPECIALTX_H
