// Copyright (c) 2018-2020 The Dash Core developers
// Copyright (c) 2020 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_PROVIDERTX_H
#define PIVX_PROVIDERTX_H

#include "consensus/validation.h"
#include "key.h"
#include "netbase.h"
#include "primitives/transaction.h"
#include "pubkey.h"
#include "univalue.h"

class CBlockIndex;

/*
 * Provider-Register transaction PayLoad
 */
class CProRegPL
{
public:
    static const uint16_t CURRENT_VERSION = 1;

    uint16_t nVersion{CURRENT_VERSION};                    // message version
    uint16_t nType{0};                                     // only 0 supported for now
    uint16_t nMode{0};                                     // only 0 supported for now
    COutPoint collateralOutpoint{UINT256_ZERO, (uint32_t)-1}; // if hash is null, we refer to a ProRegTx output
    CService addr{};
    CKeyID keyIDOwner;
    CKeyID keyIDOperator;
    CKeyID keyIDVoting;
    CScript scriptPayout{};
    uint8_t nOperatorReward{0};
    CScript scriptOperatorPayout{};
    // signature (to prove ownership of external collateral). Empty for internal collateral.
    std::vector<unsigned char> vchSig;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(nVersion);
        READWRITE(nType);
        READWRITE(nMode);
        READWRITE(collateralOutpoint);
        READWRITE(addr);
        READWRITE(keyIDOwner);
        READWRITE(keyIDOperator);
        READWRITE(keyIDVoting);
        READWRITE(*(CScriptBase*)(&scriptPayout));
        READWRITE(nOperatorReward);
        READWRITE(*(CScriptBase*)(&scriptOperatorPayout));
        if (!(s.GetType() & SER_GETHASH)) {
            READWRITE(vchSig);
        }
    }

    // When signing with the collateral key, we don't sign the hash but a generated message instead
    // This is needed for HW wallet support which can only sign text messages as of now
    std::string GetSignString() const;
    bool SignProofOfOwnership(const CKey& key);

    std::string ToString() const;
    UniValue ToJSON() const;

    // Tests performed inside CheckProRegPL
    bool checkMode(CValidationState& state) const;
    bool checkKeys(CValidationState& state) const;
    bool checkPayee(CValidationState& state) const;
    bool checkService(CValidationState& state) const;
    bool checkSig(const CKeyID& keyID, CValidationState& state) const;
    bool checkType(CValidationState& state) const;
    bool checkVersion(CValidationState& state) const;
};

bool CheckProRegPL(const CTransaction& tx, CValidationState& state);

#endif //PIVX_PROVIDERTX_H
